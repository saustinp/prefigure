#include "prefigure/label_tools.hpp"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

// POSIX bits for the daemon pipe
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#ifdef PREFIGURE_HAS_CAIRO
#include <cairo/cairo.h>
#include <cairo/cairo-svg.h>
#endif

#ifdef PREFIGURE_HAS_LIBLOUIS
#include <liblouis/liblouis.h>
#endif

namespace prefigure {

// ===========================================================================
// LocalMathLabels
// ===========================================================================
//
// Static cache definitions.  See the header for the rationale; in short, the
// Node.js MathJax invocation costs ~700 ms per call regardless of label
// count, completely dominating wall-clock build time, and the LaTeX→SVG
// mapping is a pure function so it's safe to memoise across builds.
std::unordered_map<std::string, std::string> LocalMathLabels::s_svg_cache_;
std::unordered_map<std::string, std::string> LocalMathLabels::s_braille_cache_;

// ---------------------------------------------------------------------------
// On-disk MathJax cache (shared file format with Python)
// ---------------------------------------------------------------------------
//
// File path:  ~/.cache/prefigure/mathjax-<format>-v<mathjax_version>.json
// Format:     { "<latex>": "<rendered SVG string>", ... }
//
// This is the SAME file the Python LocalMathLabels reads and writes — both
// backends share a single user-cache directory.  A first build with the
// Python backend warms the cache for any subsequent C++ build that uses
// the same labels, and vice versa.
//
// The file is loaded once per process per format (idempotent), and a flush
// runs at process exit via std::atexit (registered lazily on the first
// dirty entry, so we don't pay the registration cost on read-only runs).
namespace {

bool s_disk_cache_loaded_svg = false;
bool s_disk_cache_loaded_braille = false;
std::unordered_set<std::string> s_disk_cache_dirty_svg;
std::unordered_set<std::string> s_disk_cache_dirty_braille;
bool s_disk_cache_atexit_registered = false;
std::mutex s_disk_cache_mutex;  // protects the dirty sets and the file write

std::string read_mathjax_version() {
    // Locate mj_sre/node_modules/mathjax-full/package.json relative to the
    // current working directory or by walking up from it.  Mirrors the C++
    // search logic for mj-sre-page.js.
    auto cwd = std::filesystem::current_path();
    std::vector<std::filesystem::path> candidates = {
        cwd / "prefig" / "core" / "mj_sre" / "node_modules" / "mathjax-full" / "package.json",
        cwd / "mj_sre" / "node_modules" / "mathjax-full" / "package.json",
    };
    for (auto p = cwd; p != p.parent_path(); p = p.parent_path()) {
        candidates.push_back(p / "prefig" / "core" / "mj_sre" / "node_modules" / "mathjax-full" / "package.json");
    }
    if (const char* env = std::getenv("PREFIGURE_MJ_DIR")) {
        candidates.insert(candidates.begin(),
                          std::filesystem::path(env) / "node_modules" / "mathjax-full" / "package.json");
    }
    for (const auto& path : candidates) {
        if (!std::filesystem::exists(path)) continue;
        try {
            std::ifstream f(path);
            auto j = nlohmann::json::parse(f);
            if (j.contains("version") && j["version"].is_string()) {
                return j["version"].get<std::string>();
            }
        } catch (...) {
            continue;
        }
    }
    return "unknown";
}

std::filesystem::path disk_cache_path(OutputFormat format) {
    // Honour XDG_CACHE_HOME, fall back to $HOME/.cache, fall back to /tmp.
    std::filesystem::path base;
    if (const char* xdg = std::getenv("XDG_CACHE_HOME")) {
        base = xdg;
    } else if (const char* home = std::getenv("HOME")) {
        base = std::filesystem::path(home) / ".cache";
    } else {
        base = std::filesystem::temp_directory_path();
    }
    auto dir = base / "prefigure";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) {
        dir = std::filesystem::temp_directory_path() / "prefigure-cache";
        std::filesystem::create_directories(dir, ec);
    }

    static const std::string version = read_mathjax_version();
    std::string fname = "mathjax-" +
        std::string(format == OutputFormat::Tactile ? "tactile" : "svg") +
        "-v" + version + ".json";
    return dir / fname;
}

void load_disk_cache(OutputFormat format,
                     std::unordered_map<std::string, std::string>& target) {
    bool& flag = (format == OutputFormat::Tactile)
        ? s_disk_cache_loaded_braille
        : s_disk_cache_loaded_svg;
    if (flag) return;
    flag = true;

    auto path = disk_cache_path(format);
    if (!std::filesystem::exists(path)) {
        spdlog::debug("MathJax disk cache: no file at {} (cold start)",
                      path.string());
        return;
    }
    try {
        std::ifstream f(path);
        auto j = nlohmann::json::parse(f);
        if (!j.is_object()) return;
        size_t loaded = 0;
        for (auto it = j.begin(); it != j.end(); ++it) {
            if (it.value().is_string()) {
                target.emplace(it.key(), it.value().get<std::string>());
                ++loaded;
            }
        }
        spdlog::debug("MathJax disk cache: loaded {} entries from {}",
                      loaded, path.string());
    } catch (const std::exception& exc) {
        spdlog::warn("MathJax disk cache: failed to load {}: {}",
                     path.string(), exc.what());
    }
}

void flush_disk_cache(OutputFormat format,
                      const std::unordered_map<std::string, std::string>& source,
                      std::unordered_set<std::string>& dirty) {
    std::lock_guard<std::mutex> lock(s_disk_cache_mutex);
    if (dirty.empty()) return;
    auto path = disk_cache_path(format);

    // Re-read the existing file so we don't clobber updates from another
    // process that may have run between our load and our write.
    nlohmann::json existing = nlohmann::json::object();
    if (std::filesystem::exists(path)) {
        try {
            std::ifstream f(path);
            existing = nlohmann::json::parse(f);
            if (!existing.is_object()) {
                existing = nlohmann::json::object();
            }
        } catch (...) {
            existing = nlohmann::json::object();
        }
    }
    for (const auto& latex : dirty) {
        auto it = source.find(latex);
        if (it != source.end()) {
            existing[latex] = it->second;
        }
    }

    // Atomic write: temp file + rename.
    auto tmp = path;
    tmp += ".tmp";
    try {
        {
            std::ofstream f(tmp);
            f << existing.dump();
        }
        std::filesystem::rename(tmp, path);
    } catch (const std::exception& exc) {
        spdlog::warn("MathJax disk cache: failed to write {}: {}",
                     path.string(), exc.what());
        return;
    }
    spdlog::debug("MathJax disk cache: flushed {} new entries to {}",
                  dirty.size(), path.string());
    dirty.clear();
}

extern "C" void prefigure_flush_disk_cache_atexit() {
    flush_disk_cache(OutputFormat::SVG,
                     LocalMathLabels::s_svg_cache_for_atexit(),
                     s_disk_cache_dirty_svg);
    flush_disk_cache(OutputFormat::Tactile,
                     LocalMathLabels::s_braille_cache_for_atexit(),
                     s_disk_cache_dirty_braille);
}

// ---------------------------------------------------------------------------
// Persistent MathJax daemon client (C++ side)
// ---------------------------------------------------------------------------
//
// Spawns mj-sre-daemon.js as a child process with bidirectional pipes,
// keeps it alive for the lifetime of the host process, and rents
// per-render JSON requests over its stdin/stdout.  Mirrors the Python
// _MathJaxDaemonClient class in prefig/core/label_tools.py: same daemon
// binary, same protocol, same fall-through to the legacy one-shot
// node mj-sre-page.js path on any failure.
//
// Per-render cost after the ~570 ms one-time startup is ~5–15 ms (vs
// ~700 ms for the legacy one-shot path), so for batch builds with novel
// labels the daemon turns N × 700 ms into 570 ms + N × ~10 ms.
//
// Lifecycle: spawned lazily on first cache miss in the process, kept
// alive until program exit, where an atexit handler closes its stdin
// (the daemon detects EOF and exits cleanly).  Errors at any stage
// fall back to the one-shot path so a broken daemon never breaks builds.

class MathJaxDaemonClient {
public:
    static MathJaxDaemonClient* get() {
        static std::mutex init_mutex;
        std::lock_guard<std::mutex> lock(init_mutex);
        static MathJaxDaemonClient* instance = nullptr;
        static bool spawn_failed = false;
        if (instance != nullptr || spawn_failed) {
            return instance;
        }
        auto* client = new MathJaxDaemonClient();
        if (!client->spawn()) {
            delete client;
            spawn_failed = true;
            return nullptr;
        }
        instance = client;
        std::atexit(&MathJaxDaemonClient::shutdown_atexit);
        s_singleton.store(instance, std::memory_order_release);
        return instance;
    }

    // render() returns the JSON response object on success, or an empty
    // optional on any failure (caller falls back to one-shot path).
    std::optional<nlohmann::json> render(const std::string& latex,
                                         const std::string& format_name) {
        std::lock_guard<std::mutex> lock(io_mutex_);
        if (!alive_.load(std::memory_order_acquire)) return std::nullopt;

        nlohmann::json req = {
            {"id", "x"},
            {"format", format_name},
            {"latex", latex},
        };
        std::string req_line = req.dump();
        req_line.push_back('\n');

        ssize_t total_written = 0;
        const char* buf = req_line.data();
        size_t remaining = req_line.size();
        while (remaining > 0) {
            ssize_t n = ::write(stdin_fd_, buf + total_written, remaining);
            if (n < 0) {
                if (errno == EINTR) continue;
                spdlog::debug("MathJax daemon: write failed: {}", std::strerror(errno));
                alive_.store(false, std::memory_order_release);
                return std::nullopt;
            }
            total_written += n;
            remaining -= static_cast<size_t>(n);
        }

        // Read one line from stdout (responses are newline-delimited).
        std::string line;
        for (;;) {
            char c;
            ssize_t n = ::read(stdout_fd_, &c, 1);
            if (n == 0) {
                spdlog::debug("MathJax daemon: stdout EOF");
                alive_.store(false, std::memory_order_release);
                return std::nullopt;
            }
            if (n < 0) {
                if (errno == EINTR) continue;
                spdlog::debug("MathJax daemon: read failed: {}", std::strerror(errno));
                alive_.store(false, std::memory_order_release);
                return std::nullopt;
            }
            if (c == '\n') break;
            line.push_back(c);
        }
        try {
            return nlohmann::json::parse(line);
        } catch (const std::exception& exc) {
            spdlog::debug("MathJax daemon: bad JSON response: {}", exc.what());
            return std::nullopt;
        }
    }

private:
    MathJaxDaemonClient() = default;

    static inline std::atomic<MathJaxDaemonClient*> s_singleton{nullptr};

    bool spawn() {
        // Find mj-sre-daemon.js relative to the current directory or via
        // PREFIGURE_MJ_DIR.  Mirrors the search in process_math_labels().
        std::filesystem::path daemon_path;
        auto cwd = std::filesystem::current_path();
        std::vector<std::filesystem::path> candidates = {
            cwd / "prefig" / "core" / "mj_sre" / "mj-sre-daemon.js",
            cwd / "core" / "mj_sre" / "mj-sre-daemon.js",
            cwd / "mj_sre" / "mj-sre-daemon.js",
        };
        for (auto p = cwd; p != p.parent_path(); p = p.parent_path()) {
            candidates.push_back(p / "prefig" / "core" / "mj_sre" / "mj-sre-daemon.js");
        }
        if (const char* env = std::getenv("PREFIGURE_MJ_DIR")) {
            candidates.insert(candidates.begin(),
                              std::filesystem::path(env) / "mj-sre-daemon.js");
        }
        for (const auto& c : candidates) {
            if (std::filesystem::exists(c)) {
                daemon_path = c;
                break;
            }
        }
        if (daemon_path.empty()) {
            spdlog::debug("MathJax daemon: mj-sre-daemon.js not found, falling back");
            return false;
        }
        auto mj_dir = daemon_path.parent_path();
        std::string node_path_env = "NODE_PATH=" + (mj_dir / "node_modules").string();

        int in_pipe[2];   // parent writes -> child reads
        int out_pipe[2];  // child writes -> parent reads
        if (pipe(in_pipe) != 0 || pipe(out_pipe) != 0) {
            spdlog::debug("MathJax daemon: pipe() failed: {}", std::strerror(errno));
            return false;
        }

        pid_t pid = fork();
        if (pid < 0) {
            spdlog::debug("MathJax daemon: fork() failed: {}", std::strerror(errno));
            ::close(in_pipe[0]); ::close(in_pipe[1]);
            ::close(out_pipe[0]); ::close(out_pipe[1]);
            return false;
        }
        if (pid == 0) {
            // Child: wire stdin/stdout to the pipes, keep stderr as-is so
            // daemon errors land on the host process's stderr where the
            // user can see them.
            ::dup2(in_pipe[0], STDIN_FILENO);
            ::dup2(out_pipe[1], STDOUT_FILENO);
            ::close(in_pipe[0]); ::close(in_pipe[1]);
            ::close(out_pipe[0]); ::close(out_pipe[1]);

            // Build env: copy current environ + override NODE_PATH.
            std::vector<std::string> env_holder;
            std::vector<char*> env_argv;
            for (char** e = environ; *e; ++e) {
                if (std::strncmp(*e, "NODE_PATH=", 10) == 0) continue;
                env_holder.emplace_back(*e);
            }
            env_holder.push_back(node_path_env);
            for (auto& s : env_holder) env_argv.push_back(s.data());
            env_argv.push_back(nullptr);

            std::string daemon_path_str = daemon_path.string();
            char* argv[] = {
                const_cast<char*>("node"),
                const_cast<char*>(daemon_path_str.c_str()),
                nullptr,
            };
            ::execvpe("node", argv, env_argv.data());
            // If we get here exec failed; bail out so the parent's read
            // returns EOF.
            ::_exit(127);
        }

        // Parent: close the unused ends and stash the file descriptors.
        ::close(in_pipe[0]);
        ::close(out_pipe[1]);
        stdin_fd_ = in_pipe[1];
        stdout_fd_ = out_pipe[0];
        child_pid_ = pid;
        alive_.store(true, std::memory_order_release);

        // Wait for the {"ready":true} handshake.
        std::string ready_line;
        for (;;) {
            char c;
            ssize_t n = ::read(stdout_fd_, &c, 1);
            if (n == 0) {
                spdlog::debug("MathJax daemon: handshake EOF");
                shutdown_internal();
                return false;
            }
            if (n < 0) {
                if (errno == EINTR) continue;
                spdlog::debug("MathJax daemon: handshake read failed: {}", std::strerror(errno));
                shutdown_internal();
                return false;
            }
            if (c == '\n') break;
            ready_line.push_back(c);
        }
        try {
            auto j = nlohmann::json::parse(ready_line);
            if (!j.value("ready", false)) {
                spdlog::debug("MathJax daemon: bad handshake content: {}", ready_line);
                shutdown_internal();
                return false;
            }
        } catch (const std::exception& exc) {
            spdlog::debug("MathJax daemon: bad handshake: {}", exc.what());
            shutdown_internal();
            return false;
        }
        spdlog::debug("MathJax daemon: spawned successfully (pid={})", child_pid_);
        return true;
    }

    void shutdown_internal() {
        if (stdin_fd_ >= 0) { ::close(stdin_fd_); stdin_fd_ = -1; }
        if (stdout_fd_ >= 0) { ::close(stdout_fd_); stdout_fd_ = -1; }
        if (child_pid_ > 0) {
            int status = 0;
            if (::waitpid(child_pid_, &status, WNOHANG) == 0) {
                ::kill(child_pid_, SIGTERM);
                ::waitpid(child_pid_, &status, 0);
            }
            child_pid_ = -1;
        }
        alive_.store(false, std::memory_order_release);
    }

    static void shutdown_atexit() {
        auto* self = s_singleton.load(std::memory_order_acquire);
        if (self) self->shutdown_internal();
    }

    int stdin_fd_ = -1;
    int stdout_fd_ = -1;
    pid_t child_pid_ = -1;
    std::atomic<bool> alive_{false};
    std::mutex io_mutex_;
};

}  // anonymous namespace

// Friend-style accessors for the atexit handler (which sits in an anonymous
// namespace and would otherwise have no way to reach the private static
// caches).  These are intentionally not exposed via the public header.
const std::unordered_map<std::string, std::string>&
LocalMathLabels::s_svg_cache_for_atexit() { return s_svg_cache_; }
const std::unordered_map<std::string, std::string>&
LocalMathLabels::s_braille_cache_for_atexit() { return s_braille_cache_; }

bool LocalMathLabels::try_render_via_daemon() {
    auto* client = MathJaxDaemonClient::get();
    if (client == nullptr) {
        return false;
    }

    // Lazily register the disk-cache atexit hook (matches the one-shot
    // path) the first time the daemon path actually fills cache entries.
    if (!s_disk_cache_atexit_registered) {
        s_disk_cache_atexit_registered = true;
        std::atexit(prefigure_flush_disk_cache_atexit);
    }
    auto& dirty = (format_ == OutputFormat::Tactile)
        ? s_disk_cache_dirty_braille
        : s_disk_cache_dirty_svg;

    const std::string format_name =
        (format_ == OutputFormat::Tactile) ? "braille" : "svg";

    for (const auto& missing_id : missing_ids_) {
        auto it = id_to_text_.find(missing_id);
        if (it == id_to_text_.end()) return false;
        const std::string& latex = it->second;

        auto resp_opt = client->render(latex, format_name);
        if (!resp_opt.has_value()) return false;
        const auto& resp = *resp_opt;

        if (resp.contains("error")) {
            spdlog::debug("MathJax daemon: render error for {}: {}",
                          latex, resp["error"].dump());
            return false;
        }

        if (format_ == OutputFormat::Tactile) {
            if (!resp.contains("braille") || !resp["braille"].is_string()) {
                return false;
            }
            s_braille_cache_[latex] = resp["braille"].get<std::string>();
            dirty.insert(latex);
        } else {
            if (!resp.contains("svg") || !resp["svg"].is_string()) {
                return false;
            }
            s_svg_cache_[latex] = resp["svg"].get<std::string>();
            dirty.insert(latex);
        }
    }
    return true;
}

LocalMathLabels::LocalMathLabels(OutputFormat format)
    : format_(format)
{
    auto html = html_doc_.append_child("html");
    html_body_ = html.append_child("body");
    // Load the on-disk cache once per process per format.  This warms the
    // in-memory cache from any previous Python or C++ run that wrote to
    // the same file under ~/.cache/prefigure, eliminating MathJax cost for
    // labels that have been seen before — even across separate processes.
    load_disk_cache(format,
                    format == OutputFormat::Tactile ? s_braille_cache_ : s_svg_cache_);
}

void LocalMathLabels::add_macros(const std::string& macros) {
    auto div = html_body_.append_child("div");
    div.append_attribute("id").set_value("latex-macros");
    std::string content = "\\(" + macros + "\\)";
    div.text().set(content.c_str());
}

void LocalMathLabels::register_math_label(const std::string& id, const std::string& text) {
    auto div = html_body_.append_child("div");
    div.append_attribute("id").set_value(id.c_str());
    std::string content = "\\(" + text + "\\)";
    div.text().set(content.c_str());
    labels_present_ = true;
    // Remember the LaTeX text for this label so we can look it up in the
    // process-lifetime cache during process_math_labels() and get_math_label().
    id_to_text_[id] = text;
}

void LocalMathLabels::process_math_labels() {
    if (!labels_present_) {
        return;
    }

    // ----------------------------------------------------------------------
    // Partition labels into cache hits and misses.  If everything is
    // cached, we can skip the Node.js subprocess entirely (the hot path
    // for repeated builds in the same Python process).  If anything is
    // missing, we still have to invoke MathJax — but we minimise the work
    // by sending only the missing labels in a fresh HTML batch.
    // ----------------------------------------------------------------------
    auto& cache = (format_ == OutputFormat::Tactile)
        ? s_braille_cache_
        : s_svg_cache_;

    missing_ids_.clear();
    for (const auto& [id, text] : id_to_text_) {
        if (cache.find(text) == cache.end()) {
            missing_ids_.insert(id);
        }
    }

    if (missing_ids_.empty()) {
        spdlog::debug("MathJax cache: all {} labels hit, skipping subprocess",
                      id_to_text_.size());
        return;
    }

    spdlog::debug("MathJax cache: {}/{} labels missing, invoking subprocess",
                  missing_ids_.size(), id_to_text_.size());

    // ----------------------------------------------------------------------
    // Fast path: try the persistent MathJax daemon first.  If it's
    // available, every cache miss is rendered in ~5–15 ms instead of
    // paying the ~700 ms cost of spawning a fresh node mj-sre-page.js.
    // The daemon is spawned lazily on first call within this process.
    // On any failure (daemon missing, dead, partial render error) we
    // silently fall back to the one-shot path below.
    // ----------------------------------------------------------------------
    if (try_render_via_daemon()) {
        return;
    }

    // Create temp directory
    auto tmp_dir = std::filesystem::temp_directory_path() / "prefigure-labels";
    std::filesystem::create_directories(tmp_dir);

    std::string input_filename = "prefigure-labels.html";
    std::string fmt_str = (format_ == OutputFormat::Tactile) ? "tactile" : "svg";
    std::string output_filename = "prefigure-" + fmt_str + ".html";

    auto mj_input = tmp_dir / input_filename;
    auto mj_output = tmp_dir / output_filename;

    // Build a *minimal* HTML containing only the labels we still need to
    // render.  This way the MathJax invocation cost scales with the number
    // of misses rather than the size of the diagram.  We preserve any
    // latex-macros block (it has no id collision risk because it's used by
    // every <m> in the document).
    pugi::xml_document partial_doc;
    auto partial_html = partial_doc.append_child("html");
    auto partial_body = partial_html.append_child("body");
    for (auto div = html_body_.first_child(); div; div = div.next_sibling()) {
        std::string div_id = div.attribute("id").as_string();
        if (div_id == "latex-macros" || missing_ids_.count(div_id) > 0) {
            partial_body.append_copy(div);
        }
    }

    // Write the partial HTML file
    if (!partial_doc.save_file(mj_input.c_str(), "  ",
                               pugi::format_indent | pugi::format_no_declaration)) {
        spdlog::error("Failed to write MathJax input file {}", mj_input.string());
        return;
    }

    // Determine options
    std::string options;
    std::string format_flag;
    if (format_ == OutputFormat::Tactile) {
        format_flag = "braille";
    } else {
        options = "--svgenhanced --depth deep";
        format_flag = "svg";
    }

    // Find mj-sre-page.js relative to this library's location.
    // We search a few standard places.
    std::string mj_script;
    std::vector<std::filesystem::path> search_paths;

    // Look relative to the executable or relative to the source tree
    // The Python version looks relative to the .py file in core/mj_sre/
    // For the C++ port we look in a few standard locations.
    auto exe_path = std::filesystem::current_path();
    search_paths.push_back(exe_path / "mj_sre");
    search_paths.push_back(exe_path / "core" / "mj_sre");

    // Also try relative to the project source
    // Walk up from cwd looking for prefig/core/mj_sre
    for (auto p = exe_path; p != p.parent_path(); p = p.parent_path()) {
        search_paths.push_back(p / "prefig" / "core" / "mj_sre");
        search_paths.push_back(p / "mj_sre");
    }

    // Also check PREFIGURE_MJ_DIR environment variable
    const char* mj_env = std::getenv("PREFIGURE_MJ_DIR");
    if (mj_env) {
        search_paths.insert(search_paths.begin(), std::filesystem::path(mj_env));
    }

    for (const auto& sp : search_paths) {
        auto candidate = sp / "mj-sre-page.js";
        if (std::filesystem::exists(candidate)) {
            mj_script = candidate.string();
            break;
        }
    }

    if (mj_script.empty()) {
        spdlog::error("Cannot find mj-sre-page.js for MathJax processing");
        spdlog::error("Set PREFIGURE_MJ_DIR environment variable to the directory containing mj-sre-page.js");
        return;
    }

    std::string mj_command = "node " + mj_script +
        " --" + format_flag + " " + options + " " +
        mj_input.string() + " > " + mj_output.string();

    spdlog::debug("Using MathJax to produce mathematical labels");
    spdlog::debug("MathJax command: {}", mj_command);

    int ret = std::system(mj_command.c_str());
    if (ret != 0) {
        spdlog::error("Production of mathematical labels with MathJax was unsuccessful (exit code {})", ret);
        return;
    }

    // Parse the output
    auto result = label_tree_.load_file(mj_output.c_str());
    if (!result) {
        spdlog::error("Failed to parse MathJax output: {}", result.description());
        return;
    }
    label_tree_valid_ = true;

    // ----------------------------------------------------------------------
    // Populate the static cache from this MathJax run.  For each missing
    // id, navigate to the rendered <svg> (or mjx-braille) element, serialise
    // it to a string, and store under the LaTeX text key.  Subsequent
    // builds that use the same LaTeX will skip the MathJax subprocess
    // entirely.  We also record each new entry in the disk-cache dirty set
    // so the atexit flush hook can persist it to ~/.cache/prefigure.
    // ----------------------------------------------------------------------
    if (!s_disk_cache_atexit_registered) {
        s_disk_cache_atexit_registered = true;
        std::atexit(prefigure_flush_disk_cache_atexit);
    }

    auto& dirty = (format_ == OutputFormat::Tactile)
        ? s_disk_cache_dirty_braille
        : s_disk_cache_dirty_svg;

    for (const auto& missing_id : missing_ids_) {
        auto text_it = id_to_text_.find(missing_id);
        if (text_it == id_to_text_.end()) continue;
        const std::string& latex = text_it->second;

        std::string xpath = "//div[@id='" + missing_id + "']";
        auto div = label_tree_.select_node(xpath.c_str()).node();
        if (!div) continue;
        auto mjx_data = div.child("mjx-data");
        if (!mjx_data) continue;

        if (format_ == OutputFormat::Tactile) {
            auto mjx_braille = mjx_data.child("mjx-braille");
            if (mjx_braille) {
                s_braille_cache_[latex] = mjx_braille.text().get();
                dirty.insert(latex);
            }
        } else {
            auto mjx_container = mjx_data.child("mjx-container");
            if (!mjx_container) continue;
            auto svg = mjx_container.child("svg");
            if (!svg) continue;

            // Serialise the <svg> subtree to a string for the cache.
            // We use a custom xml_writer that appends to a std::string.
            struct StringWriter : public pugi::xml_writer {
                std::string out;
                void write(const void* data, size_t size) override {
                    out.append(static_cast<const char*>(data), size);
                }
            } writer;
            svg.print(writer, "", pugi::format_raw | pugi::format_no_declaration);
            s_svg_cache_[latex] = std::move(writer.out);
            dirty.insert(latex);
        }
    }

    // Clean up temp files
    std::error_code ec;
    std::filesystem::remove_all(tmp_dir, ec);
}

XmlNode LocalMathLabels::get_math_label(const std::string& id) {
    if (format_ == OutputFormat::Tactile) {
        // For tactile, we don't return SVG nodes
        return XmlNode();
    }

    // ----------------------------------------------------------------------
    // Look up the cached SVG by LaTeX text.  Every label that was either
    // already cached on entry to process_math_labels() or rendered by it
    // should now be in s_svg_cache_.  We parse the cached snippet into a
    // fresh pugi::xml_document owned by this LocalMathLabels instance, so
    // the returned node has independent storage that the caller
    // (mk_m_element in label.cpp) can mutate freely.
    //
    // We then rewrite glyph <defs> ids and <use href> references with a
    // per-instance "-r{N}" suffix so that two cache hits for the same
    // LaTeX text within a single build don't produce duplicate ids in the
    // output document.
    // ----------------------------------------------------------------------
    auto text_it = id_to_text_.find(id);
    if (text_it == id_to_text_.end()) {
        spdlog::error("Cache lookup failed for label id={}", id);
        return XmlNode();
    }
    const std::string& latex = text_it->second;

    auto cached_it = s_svg_cache_.find(latex);
    if (cached_it == s_svg_cache_.end()) {
        spdlog::error("Error in processing label, possibly a LaTeX error: {}", latex);
        return XmlNode();
    }
    const std::string& cached_str = cached_it->second;

    // Parse the cached SVG into a fresh document owned by this instance.
    auto doc = std::make_unique<pugi::xml_document>();
    auto load_result = doc->load_buffer(cached_str.data(), cached_str.size());
    if (!load_result) {
        spdlog::error("Failed to parse cached SVG for id={}: {}",
                      id, load_result.description());
        return XmlNode();
    }
    auto svg = doc->first_child();
    if (!svg) {
        spdlog::error("Cached SVG document is empty for id={}", id);
        return XmlNode();
    }

    // Rewrite glyph def ids and use hrefs with the per-instance counter so
    // multiple instances of the same cached label are id-unique within the
    // output document (see header docs).
    ++render_counter_;
    std::string suffix = "-r" + std::to_string(render_counter_);

    auto defs = svg.child("defs");
    if (defs) {
        for (auto glyph = defs.first_child(); glyph; glyph = glyph.next_sibling()) {
            auto attr = glyph.attribute("id");
            if (attr) {
                std::string new_id = std::string(attr.value()) + suffix;
                attr.set_value(new_id.c_str());
            }
        }
    }

    // Walk the tree and suffix any "#..." href on <use> elements
    std::function<void(pugi::xml_node)> rewrite_uses = [&](pugi::xml_node node) {
        for (auto child = node.first_child(); child; child = child.next_sibling()) {
            if (std::string(child.name()) == "use") {
                for (const char* attr_name : {"xlink:href", "href"}) {
                    auto a = child.attribute(attr_name);
                    if (a) {
                        std::string href = a.value();
                        if (!href.empty() && href[0] == '#') {
                            href += suffix;
                            a.set_value(href.c_str());
                        }
                    }
                }
            }
            rewrite_uses(child);
        }
    };
    rewrite_uses(svg);

    // Hand the document off to the per-instance owner so the returned node
    // stays alive for the rest of this build.  It will be freed when the
    // LocalMathLabels instance is destroyed by label::init() at the start
    // of the next build.
    get_docs_.push_back(std::move(doc));
    return svg;
}

std::string LocalMathLabels::get_math_braille(const std::string& id) {
    // The braille cache is keyed by LaTeX text (s_braille_cache_), not by
    // label id, so it persists across builds and is shared with every
    // LocalMathLabels instance.  process_math_labels() populates it from
    // the rendered output for any cache misses; reads here look up by
    // LaTeX text via id_to_text_.
    auto text_it = id_to_text_.find(id);
    if (text_it == id_to_text_.end()) {
        spdlog::error("Cache lookup failed for label id={}", id);
        return "";
    }
    auto cached_it = s_braille_cache_.find(text_it->second);
    if (cached_it == s_braille_cache_.end()) {
        spdlog::error("Error in processing braille label, possibly a LaTeX error: {}",
                      text_it->second);
        return "";
    }
    return cached_it->second;
}

// ===========================================================================
// CairoTextMeasurements
// ===========================================================================

#ifdef PREFIGURE_HAS_CAIRO

struct CairoTextMeasurements::Impl {
    cairo_surface_t* surface = nullptr;
    cairo_t* context = nullptr;

    Impl() {
        surface = cairo_svg_surface_create(nullptr, 200, 200);
        context = cairo_create(surface);
    }

    ~Impl() {
        if (context) cairo_destroy(context);
        if (surface) cairo_surface_destroy(surface);
    }
};

CairoTextMeasurements::CairoTextMeasurements()
    : impl_(std::make_unique<Impl>())
{
    spdlog::info("Cairo text measurement initialized");
}

CairoTextMeasurements::~CairoTextMeasurements() = default;

std::optional<std::array<double, 3>> CairoTextMeasurements::measure_text(
    const std::string& text,
    const std::string& family,
    double size,
    bool italic,
    bool bold)
{
    if (!impl_ || !impl_->context) {
        return std::nullopt;
    }

    cairo_font_slant_t slant = italic ? CAIRO_FONT_SLANT_ITALIC : CAIRO_FONT_SLANT_NORMAL;
    cairo_font_weight_t weight = bold ? CAIRO_FONT_WEIGHT_BOLD : CAIRO_FONT_WEIGHT_NORMAL;

    cairo_select_font_face(impl_->context, family.c_str(), slant, weight);
    cairo_set_font_size(impl_->context, size);

    cairo_text_extents_t extents;
    cairo_text_extents(impl_->context, text.c_str(), &extents);

    double y_bearing = extents.y_bearing;
    double t_height = extents.height;
    double x_advance = extents.x_advance;

    return std::array<double, 3>{x_advance, -y_bearing, t_height + y_bearing};
}

#endif // PREFIGURE_HAS_CAIRO

// ===========================================================================
// LocalLouisBrailleTranslator
// ===========================================================================

#ifdef PREFIGURE_HAS_LIBLOUIS

LocalLouisBrailleTranslator::LocalLouisBrailleTranslator() {
    // Test if liblouis tables are available
    // We simply set the flag; actual translation will test it
    louis_loaded_ = true;
    spdlog::info("liblouis braille translator initialized");
}

bool LocalLouisBrailleTranslator::initialized() const {
    return louis_loaded_;
}

std::optional<std::string> LocalLouisBrailleTranslator::translate(
    const std::string& text,
    const std::vector<int>& typeform)
{
    if (!louis_loaded_ || text.empty()) {
        if (text.empty()) return std::string("");
        return std::nullopt;
    }

    // Convert UTF-8 string to widechar array for liblouis
    std::vector<widechar> inbuf;
    for (size_t i = 0; i < text.size(); ) {
        uint32_t cp = 0;
        unsigned char c = text[i];
        if (c < 0x80) { cp = c; i += 1; }
        else if (c < 0xE0) { cp = (c & 0x1F) << 6 | (static_cast<unsigned char>(text[i+1]) & 0x3F); i += 2; }
        else if (c < 0xF0) { cp = (c & 0x0F) << 12 | (static_cast<unsigned char>(text[i+1]) & 0x3F) << 6 | (static_cast<unsigned char>(text[i+2]) & 0x3F); i += 3; }
        else { cp = (c & 0x07) << 18 | (static_cast<unsigned char>(text[i+1]) & 0x3F) << 12 | (static_cast<unsigned char>(text[i+2]) & 0x3F) << 6 | (static_cast<unsigned char>(text[i+3]) & 0x3F); i += 4; }
        inbuf.push_back(static_cast<widechar>(cp));
    }
    int inlen = static_cast<int>(inbuf.size());
    int outlen = inlen * 4 + 64;  // generous output buffer

    std::vector<widechar> outbuf(outlen);

    // Build typeform array (same length as input)
    std::vector<formtype> tf(inlen, 0);
    for (int i = 0; i < inlen && i < static_cast<int>(typeform.size()); ++i) {
        tf[i] = static_cast<formtype>(typeform[i]);
    }

    int in_len_copy = inlen;
    int out_len_copy = outlen;

    int result = lou_translateString(
        "en-ueb-g2.ctb",
        inbuf.data(), &in_len_copy,
        outbuf.data(), &out_len_copy,
        tf.data(),
        nullptr,  // spacing
        0         // mode
    );

    if (result == 0) {
        spdlog::error("liblouis translation failed");
        return std::nullopt;
    }

    // Convert output widechar back to UTF-8 string
    std::string output;
    output.reserve(out_len_copy * 4);
    for (int i = 0; i < out_len_copy; ++i) {
        widechar wc = outbuf[i];
        if (wc < 0x80) {
            output.push_back(static_cast<char>(wc));
        } else if (wc < 0x800) {
            output.push_back(static_cast<char>(0xC0 | (wc >> 6)));
            output.push_back(static_cast<char>(0x80 | (wc & 0x3F)));
        } else {
            output.push_back(static_cast<char>(0xE0 | (wc >> 12)));
            output.push_back(static_cast<char>(0x80 | ((wc >> 6) & 0x3F)));
            output.push_back(static_cast<char>(0x80 | (wc & 0x3F)));
        }
    }

    // Trim trailing whitespace (matching Python's .rstrip())
    while (!output.empty() && (output.back() == ' ' || output.back() == '\t' ||
                                output.back() == '\n' || output.back() == '\r')) {
        output.pop_back();
    }

    return output;
}

#endif // PREFIGURE_HAS_LIBLOUIS

}  // namespace prefigure
