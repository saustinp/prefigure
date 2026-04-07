import abc
import atexit
import copy
import json
import lxml.etree as ET
import subprocess
import sys
import tempfile
import threading
import logging
import inspect
import os
from pathlib import Path
from pathlib import Path

log = logging.getLogger("prefigure")
ns = {'svg': 'http://www.w3.org/2000/svg'}
_xlink_href = '{http://www.w3.org/1999/xlink}href'


# ---------------------------------------------------------------------------
# On-disk MathJax cache
# ---------------------------------------------------------------------------
#
# The in-process LaTeX→SVG cache on LocalMathLabels eliminates the MathJax
# subprocess cost for repeated builds within a single Python process.  But
# many real workflows are *cross-process*: a CI script that runs
# ``prefig build foo.xml`` 100 times in 100 separate processes, a watch
# loop that rebuilds on file change, a GNU make rule that builds each
# diagram independently.  In every one of those cases the in-process cache
# is useless because it dies with the process.
#
# This module-level disk cache solves that.  It serialises the in-process
# cache to a JSON file under the user cache directory, keyed by MathJax
# version (so a MathJax upgrade automatically invalidates everything),
# loaded once at the start of every LocalMathLabels instance, and written
# back asynchronously when new entries arrive.  The format is:
#
#   ~/.cache/prefigure/mathjax-<format>-v<version>.json
#       { "<latex>": "<rendered_serialised_xml>", ... }
#
# The cache is bounded by the number of unique LaTeX strings the user
# actually compiles — typically dozens to thousands, never gigabytes.
# We use plain JSON (not SQLite) for two reasons: (1) zero new dependencies
# and (2) the file is readable/diff-able/diagnose-able with `cat`.
# ---------------------------------------------------------------------------

def _user_cache_dir() -> Path:
    """Return the prefigure cache directory, creating it on demand.

    Honors XDG_CACHE_HOME (Linux convention) and falls back to
    ``~/.cache`` otherwise.  Falls back to a temp directory if the
    cache dir is not writable so a broken filesystem doesn't take
    down a build.
    """
    base = os.environ.get("XDG_CACHE_HOME") or str(Path.home() / ".cache")
    cache_dir = Path(base) / "prefigure"
    try:
        cache_dir.mkdir(parents=True, exist_ok=True)
        return cache_dir
    except OSError:
        return Path(tempfile.gettempdir()) / "prefigure-cache"


def _mathjax_version() -> str:
    """Read the installed MathJax version, or 'unknown' if it can't be found.

    Used to namespace the on-disk cache file so that upgrading MathJax
    automatically invalidates every cached entry.
    """
    here = Path(os.path.dirname(os.path.abspath(inspect.getfile(inspect.currentframe()))))
    pkg_json = here / "mj_sre" / "node_modules" / "mathjax-full" / "package.json"
    try:
        with open(pkg_json, "r", encoding="utf-8") as f:
            return json.load(f).get("version", "unknown")
    except (OSError, json.JSONDecodeError):
        return "unknown"


def _disk_cache_path(format_name: str) -> Path:
    """Path to the disk-cache JSON file for the given output format."""
    return _user_cache_dir() / f"mathjax-{format_name}-v{_mathjax_version()}.json"


# Module-level state for the disk cache.  These are populated lazily on
# first use and shared by every LocalMathLabels instance in the process.
_disk_cache_loaded: dict = {}      # {format_name: True}  -> already merged into the in-mem cache
_disk_cache_dirty: dict = {}       # {format_name: set(latex)}  -> entries added since last flush
_disk_cache_atexit_registered = False


# ---------------------------------------------------------------------------
# Persistent MathJax daemon client
# ---------------------------------------------------------------------------
#
# When ``LocalMathLabels.process_math_labels()`` discovers cache misses, the
# old code path used to spawn a fresh ``node mj-sre-page.js`` subprocess for
# each call.  Empirically that's ~700 ms per call, of which ~620 ms is fixed
# Node + MathJax + SRE startup overhead — work that's exactly the same
# regardless of how many labels you actually want to render.
#
# This daemon client keeps a single ``mj-sre-daemon.js`` process alive for
# the lifetime of the Python process.  Each cache miss sends a JSON request
# over stdin and reads a JSON response from stdout.  Per-request cost after
# warmup is ~5–15 ms.  Net effect on a batch with N novel labels:
#
#   old:  N × 700 ms                  (one fresh node per parse() call)
#   new:  ~700 ms once + N × ~10 ms   (one persistent daemon, hot)
#
# The daemon is spawned lazily on the first miss in the process so that
# read-only workflows (everything cached) pay nothing for spawning Node at
# all.  An ``atexit`` hook closes its stdin so it shuts down cleanly.
#
# If the daemon fails to spawn (Node missing, mj_sre dir missing, the
# script crashes on startup), we silently fall back to the legacy one-shot
# ``node mj-sre-page.js`` path so a broken daemon never breaks builds.

class _MathJaxDaemonClient:
    """Singleton wrapper around a long-running mj-sre-daemon.js process."""

    _instance: "Optional[_MathJaxDaemonClient]" = None
    _lock = threading.Lock()

    @classmethod
    def get(cls) -> "Optional[_MathJaxDaemonClient]":
        """Return the (lazily spawned) singleton, or None if spawn failed.

        Subsequent calls after a failed spawn return None without retrying;
        a broken daemon shouldn't keep getting re-summoned on every miss.
        """
        with cls._lock:
            if cls._instance is None and not cls._spawn_failed:
                cls._instance = cls._try_spawn()
                if cls._instance is None:
                    cls._spawn_failed = True
            return cls._instance

    _spawn_failed = False

    @classmethod
    def _try_spawn(cls) -> "Optional[_MathJaxDaemonClient]":
        """Spawn the daemon and wait for its `{"ready": true}` handshake."""
        here = Path(os.path.dirname(os.path.abspath(inspect.getfile(inspect.currentframe()))))
        mj_dir = here / "mj_sre"
        daemon_script = mj_dir / "mj-sre-daemon.js"
        if not daemon_script.exists():
            log.debug("MathJax daemon: %s not found, falling back to one-shot path", daemon_script)
            return None

        env = os.environ.copy()
        # Make sure the daemon can resolve mathjax-full and friends.
        node_modules = mj_dir / "node_modules"
        env["NODE_PATH"] = str(node_modules)

        try:
            proc = subprocess.Popen(
                ["node", str(daemon_script)],
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                bufsize=1,  # line-buffered
                env=env,
            )
        except (FileNotFoundError, OSError) as exc:
            log.debug("MathJax daemon: failed to spawn: %s", exc)
            return None

        # Wait for the ready handshake (or stderr if it crashed during init).
        try:
            ready_line = proc.stdout.readline()
        except Exception as exc:
            log.debug("MathJax daemon: stdout read failed: %s", exc)
            proc.kill()
            return None
        if not ready_line:
            err = proc.stderr.read() if proc.stderr else ""
            log.debug("MathJax daemon: empty stdout (stderr=%s)", err.strip())
            proc.kill()
            return None
        try:
            handshake = json.loads(ready_line)
        except json.JSONDecodeError:
            log.debug("MathJax daemon: bad handshake: %r", ready_line)
            proc.kill()
            return None
        if not handshake.get("ready"):
            log.debug("MathJax daemon: bad handshake content: %r", handshake)
            proc.kill()
            return None

        log.debug("MathJax daemon: spawned successfully (pid=%d)", proc.pid)
        client = cls.__new__(cls)
        client.proc = proc
        client._lock = threading.Lock()  # serialise per-call access (req/resp pairs)
        atexit.register(client.shutdown)
        return client

    def render(self, latex: str, format_name: str) -> "Optional[dict]":
        """Send one request to the daemon and return the parsed response.

        Returns ``None`` on any I/O error.  The caller is expected to fall
        back to the one-shot ``node mj-sre-page.js`` path on failure.
        """
        if self.proc.poll() is not None:
            return None
        req = json.dumps({"id": "x", "format": format_name, "latex": latex}) + "\n"
        with self._lock:
            try:
                self.proc.stdin.write(req)
                self.proc.stdin.flush()
                line = self.proc.stdout.readline()
            except (BrokenPipeError, OSError):
                return None
        if not line:
            return None
        try:
            return json.loads(line)
        except json.JSONDecodeError:
            return None

    def shutdown(self) -> None:
        """Close stdin so the daemon exits cleanly, then reap the process."""
        try:
            if self.proc.stdin and not self.proc.stdin.closed:
                self.proc.stdin.close()
        except Exception:
            pass
        try:
            self.proc.wait(timeout=2)
        except (subprocess.TimeoutExpired, Exception):
            try:
                self.proc.kill()
            except Exception:
                pass

class AbstractMathLabels(abc.ABC):
    @abc.abstractmethod
    def add_macros(self, macros):
        pass

    @abc.abstractmethod
    def register_math_label(self, id, text):
        pass

    @abc.abstractmethod
    def process_math_labels(self):
        pass

    @abc.abstractmethod
    def get_math_label(self, id):
        pass


class AbstractTextMeasurements(abc.ABC):
    @abc.abstractmethod
    def measure_text(self, text, font_data):
        pass

class AbstractBrailleTranslator(abc.ABC):
    @abc.abstractmethod
    def initialized(self):
        pass
    
    @abc.abstractmethod
    def translate(self, text, typeform):
        pass

class LocalMathLabels(AbstractMathLabels):
    # ------------------------------------------------------------------
    # Process-lifetime LaTeX -> rendered-label cache.
    #
    # The Node.js MathJax invocation that this class shells out to in
    # ``process_math_labels`` costs ~700 ms per call (Node startup +
    # V8 boot + MathJax library load + LaTeX typesetting), and that
    # cost is the same regardless of how many labels are in the batch.
    # That single subprocess invocation completely dominates the wall
    # clock of any prefigure build that contains math labels — see the
    # diagnosis in the C++ port plan for hard numbers.
    #
    # Since the LaTeX-to-rendered-SVG mapping is a pure function of the
    # input string and the (fixed) installed MathJax version, we cache
    # the result by LaTeX text, share the cache across every
    # ``LocalMathLabels`` instance via a class attribute, and skip the
    # subprocess entirely when every label in a build is already cached.
    # The cache is bounded by the number of unique LaTeX strings the
    # caller actually uses, which in practice is small.
    #
    # ``_svg_cache`` for sighted output stores ``lxml`` Element trees;
    # ``_braille_cache`` for tactile output stores plain strings.
    # The cache key is just the LaTeX text — the format determines
    # which dict we look in.
    #
    # ``_render_counter`` is a *per-instance* counter used to suffix
    # glyph ``<defs>`` ids on every cache hit.  MathJax assigns
    # sequence-numbered ids like ``MJX-1-TEX-N-31`` within a single
    # render, and ``mk_m_element`` in label.py later rewrites those ids
    # by prepending the diagram-id prefix.  If two labels in the same
    # build share the same LaTeX text, they would both come out of the
    # cache with identical glyph ids and produce duplicate ids in the
    # output SVG document.  By appending a fresh ``-r{N}`` suffix on
    # every get, we keep every emitted glyph id unique within the build.
    # The counter is per-instance (not class-level) so every build gets
    # a fresh sequence starting at 1, which makes warm-cache output
    # byte-identical to cold-cache output.
    # ------------------------------------------------------------------
    _svg_cache: dict = {}      # {latex_text: lxml.etree._Element}
    _braille_cache: dict = {}  # {latex_text: str}

    def __init__(self, format):
        self.format = format

        html = ET.Element('html')
        body = ET.SubElement(html, 'body')
        self.html_tree = html
        self.html_body = body
        self.labels_present = False
        # id -> latex text, used to look up the cache by content for
        # each labelled element after MathJax has run (or instead of it)
        self._id_to_text: dict = {}
        # ids whose cache miss caused us to invoke MathJax — get_math_label
        # for these reads from self.label_tree, others read from the cache
        self._missing_ids: set = set()
        # Set when process_math_labels finds every requested label in
        # the cache and skips the Node.js invocation entirely
        self._all_cached: bool = False
        self.label_tree = None  # populated only on a cache miss
        # per-build counter for the -r{N} glyph id suffix (see class doc)
        self._render_counter = 0
        # Lazy-load the on-disk cache (once per process per format).  This
        # populates the in-memory cache from the JSON file under the user
        # cache directory, so that even a brand-new Python process started
        # against a previously-built diagram pays no MathJax cost at all.
        LocalMathLabels._load_disk_cache(format)

    @classmethod
    def _load_disk_cache(cls, format_name: str) -> None:
        """Merge the on-disk JSON cache into the in-memory class caches.

        Idempotent: each format is loaded at most once per process.
        Quietly tolerates a missing or corrupt file (just starts empty).
        """
        if _disk_cache_loaded.get(format_name):
            return
        _disk_cache_loaded[format_name] = True
        path = _disk_cache_path(format_name)
        if not path.exists():
            log.debug("MathJax disk cache: no file at %s (cold start)", path)
            return
        try:
            with open(path, "r", encoding="utf-8") as f:
                data = json.load(f)
        except (OSError, json.JSONDecodeError) as exc:
            log.warning("MathJax disk cache: failed to load %s: %s", path, exc)
            return
        if not isinstance(data, dict):
            return

        target = cls._braille_cache if format_name == "tactile" else cls._svg_cache
        loaded = 0
        for latex, payload in data.items():
            if format_name == "tactile":
                if isinstance(payload, str):
                    target[latex] = payload
                    loaded += 1
            else:
                # Sighted: payload is a serialised XML string.  We parse it
                # back into an lxml Element so the in-mem cache shape matches
                # the cold-render path (which stores Element objects, not
                # strings).  Parsing is microseconds and only happens once
                # per process per cached label.
                if isinstance(payload, str):
                    try:
                        target[latex] = ET.fromstring(payload)
                        loaded += 1
                    except ET.XMLSyntaxError:
                        pass
        log.debug("MathJax disk cache: loaded %d entries from %s", loaded, path)

    @classmethod
    def _flush_disk_cache(cls, format_name: str) -> None:
        """Append any new entries from the in-memory cache to the disk cache.

        Called from an atexit hook so we don't slow down the build with
        synchronous writes after every miss.  Reads the existing file
        (it may have been updated by a parallel process), merges, then
        writes back atomically via a temp file + rename.
        """
        dirty = _disk_cache_dirty.get(format_name)
        if not dirty:
            return
        path = _disk_cache_path(format_name)
        try:
            path.parent.mkdir(parents=True, exist_ok=True)
        except OSError as exc:
            log.warning("MathJax disk cache: cannot create %s: %s", path.parent, exc)
            return

        # Re-read whatever's currently on disk so we don't clobber updates
        # from a parallel process.  Then merge in our dirty entries.
        existing: dict = {}
        if path.exists():
            try:
                with open(path, "r", encoding="utf-8") as f:
                    existing = json.load(f)
                    if not isinstance(existing, dict):
                        existing = {}
            except (OSError, json.JSONDecodeError):
                existing = {}

        source = cls._braille_cache if format_name == "tactile" else cls._svg_cache
        for latex in dirty:
            value = source.get(latex)
            if value is None:
                continue
            if format_name == "tactile":
                existing[latex] = value
            else:
                # Serialise the Element back to a string for on-disk storage.
                try:
                    existing[latex] = ET.tostring(value, encoding="unicode")
                except (TypeError, ValueError):
                    pass

        tmp = path.with_suffix(path.suffix + ".tmp")
        try:
            with open(tmp, "w", encoding="utf-8") as f:
                json.dump(existing, f)
            os.replace(tmp, path)
        except OSError as exc:
            log.warning("MathJax disk cache: failed to write %s: %s", path, exc)
            return
        log.debug("MathJax disk cache: flushed %d new entries to %s",
                  len(dirty), path)
        dirty.clear()

    def add_macros(self, macros):
        macros_div = ET.SubElement(self.html_body, 'div')
        macros_div.set('id', 'latex-macros')
        macros_div.text = fr'\({macros}\)'

    def register_math_label(self, id, text):
        # We keep building self.html_tree even when the cache will end up
        # serving every label, because process_math_labels may still need
        # to invoke MathJax for a partial-miss subset.
        div = ET.SubElement(self.html_body, 'div')
        div.set('id', id)
        div.text = fr'\({text}\)'
        self._id_to_text[id] = text
        self.labels_present = True

    def _cache_for_format(self):
        return (self._braille_cache
                if self.format == "tactile"
                else self._svg_cache)

    def _render_via_daemon(self) -> bool:
        """Render every missing label via the persistent MathJax daemon.

        Returns True if every missing label was rendered successfully and
        the in-memory cache + dirty set were updated, False on any failure
        (daemon unreachable, request error, parse error of returned svg).
        On False the caller falls through to the legacy one-shot subprocess
        path so we never lose builds to a broken daemon.
        """
        client = _MathJaxDaemonClient.get()
        if client is None:
            return False

        cache = self._cache_for_format()

        # Lazily register the disk-cache atexit hook (matches the one-shot
        # path) the first time the daemon path actually fills cache entries.
        global _disk_cache_atexit_registered
        if not _disk_cache_atexit_registered:
            _disk_cache_atexit_registered = True
            atexit.register(LocalMathLabels._flush_disk_cache, "svg")
            atexit.register(LocalMathLabels._flush_disk_cache, "tactile")
        dirty = _disk_cache_dirty.setdefault(self.format, set())

        format_name = "braille" if self.format == "tactile" else "svg"
        for missing_id in list(self._missing_ids):
            text = self._id_to_text[missing_id]
            resp = client.render(text, format_name)
            if not resp or "error" in resp:
                # Daemon failed mid-batch — caller falls back to one-shot
                # path which will fill in everything (the entries we did
                # populate from earlier daemon hits stay valid).
                log.debug("MathJax daemon: render failed for %r: %r", text, resp)
                return False
            if self.format == "tactile":
                braille = resp.get("braille")
                if braille is None:
                    return False
                cache[text] = braille
                dirty.add(text)
            else:
                svg_str = resp.get("svg")
                if svg_str is None:
                    return False
                try:
                    cache[text] = ET.fromstring(svg_str)
                except ET.XMLSyntaxError:
                    return False
                dirty.add(text)
        return True

    def process_math_labels(self):
        if not self.labels_present:
            return

        # ------------------------------------------------------------------
        # Partition labels into cache hits and misses.  If everything is
        # cached, we can skip the Node.js subprocess entirely (the hot path
        # for repeated builds in the same Python process).  If anything is
        # missing, we still have to invoke MathJax — but we minimise the
        # work by sending only the missing labels in a fresh HTML batch.
        # ------------------------------------------------------------------
        cache = self._cache_for_format()
        self._missing_ids = {
            id for id, text in self._id_to_text.items() if text not in cache
        }

        if not self._missing_ids:
            self._all_cached = True
            log.debug("MathJax cache: all %d labels hit, skipping subprocess",
                      len(self._id_to_text))
            return

        log.debug("MathJax cache: %d/%d labels missing, invoking subprocess",
                  len(self._missing_ids), len(self._id_to_text))

        # ------------------------------------------------------------------
        # Fast path: try the persistent MathJax daemon first.  If it's
        # available it can render each missing label in ~5–15 ms instead
        # of the ~700 ms cost of spawning a fresh node mj-sre-page.js.
        # The daemon is spawned lazily on first call within this process.
        # On any failure (daemon missing, dead, or per-request error) we
        # silently fall back to the one-shot path below so a broken daemon
        # never breaks builds.
        # ------------------------------------------------------------------
        if self._render_via_daemon():
            self._all_cached = True  # everything is now in the in-mem cache
            return

        # Build a *minimal* HTML containing only the labels we still need
        # to render.  This way the MathJax invocation cost scales with the
        # number of misses rather than the size of the diagram.
        partial_html = ET.Element('html')
        partial_body = ET.SubElement(partial_html, 'body')
        # Preserve any latex-macros block (it has no id collision risk
        # because it's used by every <m> in the document).
        for div in self.html_body:
            div_id = div.get('id')
            if div_id == 'latex-macros' or div_id in self._missing_ids:
                partial_body.append(copy.deepcopy(div))

        # prepare the MathJax command
        input_filename = "prefigure-labels.html"
        output_filename = f"prefigure-{self.format}.html"
        working_dir = tempfile.TemporaryDirectory()
        mj_input = os.path.join(working_dir.name, input_filename)
        mj_output = os.path.join(working_dir.name, output_filename)

        # write the HTML file (now containing only the missing labels)
        with ET.xmlfile(mj_input, encoding='utf-8') as xf:
            xf.write(partial_html, pretty_print=True)

        options = ''
        if self.format == 'tactile':
            format = 'braille'
        else:
            options = '--svgenhanced --depth deep'
            format = 'svg'

        # have MathJax process the HTML file and load the resulting
        # SVG labels into label_tree
        path = Path(os.path.dirname(os.path.abspath(inspect.getfile(inspect.currentframe()))))
        mj_dir = path.absolute() / 'mj_sre'
        mj_dir_str = str(mj_dir)

        if not (mj_dir / 'mj-sre-page.js').exists():
            log.info("MathJax installation not found so we will install it")
            from .. import scripts
            success = scripts.install_mj.main()
            if not success:
                log.error("Cannot create labels without MathJax")
                return

        mj_command = 'node {}/mj-sre-page.js --{} {} {} > {}'.format(mj_dir_str, format, options, mj_input, mj_output)
        log.debug("Using MathJax to produce mathematical labels")
        try:
            os.system(mj_command)
        except:
            log.error("Production of mathematical labels with MathJax was unsuccessful")
            return
        self.label_tree = ET.parse(mj_output)
        working_dir.cleanup()

        # ------------------------------------------------------------------
        # Populate the cache from this MathJax run.  We deep-copy each
        # rendered element before storing it so the cache entry has no
        # parent pointer back into label_tree (lxml shares parents through
        # those pointers; sharing them across the boundary is a recipe for
        # subtle aliasing bugs).  Each newly-stored entry is also recorded
        # in _disk_cache_dirty so the atexit flush hook can persist it to
        # the JSON file under the user cache directory.
        # ------------------------------------------------------------------
        global _disk_cache_atexit_registered
        if not _disk_cache_atexit_registered:
            _disk_cache_atexit_registered = True
            atexit.register(LocalMathLabels._flush_disk_cache, "svg")
            atexit.register(LocalMathLabels._flush_disk_cache, "tactile")

        dirty = _disk_cache_dirty.setdefault(self.format, set())

        for missing_id in self._missing_ids:
            text = self._id_to_text[missing_id]
            try:
                div = self.label_tree.xpath(
                    "//html/body/div[@id = '{}']".format(missing_id))[0]
            except IndexError:
                continue  # MathJax failed to render this label; will warn on get
            if self.format == "tactile":
                try:
                    braille_node = div.xpath('mjx-data/mjx-braille')[0]
                    self._braille_cache[text] = braille_node.text
                    dirty.add(text)
                except IndexError:
                    pass  # report at get-time
            else:
                try:
                    svg = div.xpath('mjx-data/mjx-container/svg:svg',
                                    namespaces=ns)[0]
                    self._svg_cache[text] = copy.deepcopy(svg)
                    dirty.add(text)
                except IndexError:
                    pass  # report at get-time

    def get_math_label(self, id):
        text = self._id_to_text.get(id)
        if text is None:
            # Defensive fallback to the legacy XPath path on label_tree
            log.error("Cache lookup failed for label id %s", id)
            return None

        # tactile: braille strings are immutable, no copy needed
        if self.format == "tactile":
            cached_braille = self._braille_cache.get(text)
            if cached_braille is not None:
                return cached_braille
            log.error("Error in processing label, possibly a LaTeX error: %s", text)
            return None

        # sighted: deep-copy the cached <svg> and rewrite glyph ids so
        # repeated identical labels do not collide in the output document
        cached_svg = self._svg_cache.get(text)
        if cached_svg is None:
            log.error("Error in processing label, possibly a LaTeX error: %s", text)
            return None

        self._render_counter += 1
        suffix = f"-r{self._render_counter}"
        clone = copy.deepcopy(cached_svg)

        defs = clone.find('svg:defs', namespaces=ns)
        if defs is not None:
            for glyph in defs:
                gid = glyph.get('id')
                if gid is not None:
                    glyph.set('id', gid + suffix)
        for use in clone.findall('.//svg:use', namespaces=ns):
            href = use.get(_xlink_href)
            if href and href.startswith('#'):
                use.set(_xlink_href, href + suffix)
        return clone


class PyodideMathLabels(AbstractMathLabels):
    def __init__(self, format):
        global prefigBrowserApi
        import prefigBrowserApi

        self.format = format
        self.text_label_dict = {}
        self.math_label_dict = {}
        
    def add_macros(self, macros):
        pass

    def register_math_label(self, id, text):
        self.text_label_dict[id] = text

    def process_math_labels(self):
        for id, text in self.text_label_dict.items():
            if self.format == "tactile":
                insert = prefigBrowserApi.processBraille(text)
            else:
                svg = prefigBrowserApi.processMath(text)
                container = ET.fromstring(svg)
                insert = container.xpath('//svg:svg',
                                     namespaces=ns)[0]
            self.math_label_dict[id] = insert

    def get_math_label(self, id):
        return self.math_label_dict[id]


class CairoTextMeasurements(AbstractTextMeasurements):
    def __init__(self):
        self.cairo_loaded = False
        global cairo
        try:
            import cairo
        except:
            log.info('Error importing Python package cairo, which is required for non-mathemaical labels.')
            log.info('See the PreFigure installation instructions at https://prefigure.org')
            log.info('The rest of the diagram will still be built')
            return

        log.info("cairo imported")
        self.cairo_loaded = True
        surface = cairo.SVGSurface(None, 200, 200)
        self.context = cairo.Context(surface)
        self.italics_dict = {True: cairo.FontSlant.ITALIC,
                             False: cairo.FontSlant.NORMAL}
        self.bold_dict = {True: cairo.FontWeight.BOLD,
                          False: cairo.FontWeight.NORMAL}

    def measure_text(self, text, font_data):
        font, font_size, italics, bold = font_data[:4]
        
        if not self.cairo_loaded:
            return None

        self.context.select_font_face(font,
                                      self.italics_dict[italics],
                                      self.bold_dict[bold])
        self.context.set_font_size(font_size)
        extents = self.context.text_extents(text)
        y_bearing = extents[1]
        t_height  = extents[3]
        xadvance  = extents[4]
        return [xadvance, -y_bearing, t_height+y_bearing]

class LocalLouisBrailleTranslator(AbstractBrailleTranslator):
    def __init__(self):
        self.louis_loaded = False

        try:
            global louis
            import louis
        except:
            log.info('Failed to import louis so we cannot make braille labels')
            log.info('See the installation instructions at https://prefigure.org')
            log.info('The rest of the diagram will still be built.')
            return

        log.info("louis imported")
        self.louis_loaded = True

    def initialized(self):
        return self.louis_loaded

    def translate(self, text, typeform):
        if not self.louis_loaded:
            return None
        if len(text) == 0:
            return ""
        return louis.translateString(
            ["en-ueb-g2.ctb"],
            text,
            typeform=typeform
        ).rstrip()


class PyodideBrailleTranslator(AbstractBrailleTranslator):
    def __init__(self):
        global prefigBrowserApi
        import prefigBrowserApi

    def initialized(self):
        return True

    def translate(self, text, typeform):
        log.info('Called translate text')
        try:
            # `prefigBrowserApi` will return a JsProxy. We want a native python object,
            # so we convert it to a list.
            braille_string = prefigBrowserApi.translate_text(text, typeform)
            return braille_string
        except Exception as e:
            log.error(str(e))
            log.error("Error in translating text")


class PyodideTextMeasurements(AbstractTextMeasurements):
    def measure_text(self, text, font_data):
        log.info('Called measure text')
        try:
            import prefigBrowserApi
            # `prefigBrowserApi` will return a JsProxy. We want a native python object,
            # so we convert it to a list.
            font_string = ""
            if font_data[2]:
                font_string = "italic "
            if font_data[3]:
                font_string += "bold "
            font_string += f"{str(font_data[1])}px {font_data[0]}"
            metrics = prefigBrowserApi.measure_text(text, font_string).to_py()
            return metrics
        except Exception as e:
            log.error(str(e))
            log.error("text_dims not found")
