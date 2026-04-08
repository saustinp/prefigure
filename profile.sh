#!/usr/bin/env bash
#
# profile.sh — record a perf profile of the C++ extension and post-process
#              it into a pruned hierarchical call tree (JSON) for offline
#              inspection.
#
# Run from the repository root:   ./profile.sh [diagram] [runs]
#
# Arguments (both optional):
#   diagram   — name of the example to profile.  Default: implicit
#               (use any name from `profiling_comparison.py --list`)
#   runs      — number of timed runs to perform under perf.  Default: 30
#
# Examples:
#   ./profile.sh                       # implicit, 30 runs
#   ./profile.sh implicit              # same
#   ./profile.sh tangent 50            # tangent, 50 runs
#   ./profile.sh diffeqs 10            # diffeqs, 10 runs (quick smoke test)
#
# What this script does:
#   1. Sanity-checks that the .so is built with debug symbols (RelWithDebInfo).
#   2. Opens kernel.perf_event_paranoid for the user (one-time per boot, via
#      sudo — prompts for password only if needed).
#   3. Wipes ~/.cache/prefigure and warms it once so the daemon spawn cost
#      doesn't dominate the profile.
#   4. Records a perf profile pinned to a single core (avoids the Intel
#      hybrid-CPU PMU issue) using frame-pointer unwinding (avoids the
#      addr2line storm caused by GCC 14's DWARF v5 output vs system addr2line).
#   5. Verifies the resulting .perf file is non-empty and well-formed.
#   6. Converts it to per-sample script text via `perf script --no-inline`.
#   7. Folds the per-sample stacks via `~/FlameGraph/stackcollapse-perf.pl`.
#   8. Builds a pruned hierarchical call tree JSON via `export_tree.py`,
#      rooted at the first pybind11 dispatcher frame, suitable for offline
#      inspection or comparison against a previous run.
#
# Outputs:
#   /tmp/<diagram>.perf                 — raw perf data
#   /tmp/<diagram>_script.txt           — per-sample stack text from perf script
#   /tmp/<diagram>.speedscope           — same content, drag-and-drop ready for
#                                          https://speedscope.app
#   /tmp/folded_stacks.txt              — folded stacks (clobbered each run)
#   /tmp/<diagram>_pruned_trace.json    — pruned hierarchical call tree (per-diagram)
#
# Prerequisites:
#   - Brendan Gregg's FlameGraph tools cloned to ~/FlameGraph/
#       git clone https://github.com/brendangregg/FlameGraph.git ~/FlameGraph
#   - export_tree.py present in the repository root.
#
# A non-zero exit means at least one step failed; the step name is printed.

set -uo pipefail

# ---- argument parsing ----

DIAGRAM="${1:-implicit}"
RUNS="${2:-30}"

if ! [[ "$RUNS" =~ ^[0-9]+$ ]]; then
    echo "ERROR: runs argument must be a positive integer (got: $RUNS)" >&2
    echo "Usage: $0 [diagram] [runs]" >&2
    exit 2
fi

# ---- paths ----

REPO_ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$REPO_ROOT"

VENV_PY="$REPO_ROOT/.venv/bin/python"
PROFILER_SCRIPT="$REPO_ROOT/profiling_comparison.py"
SO_PATH="$REPO_ROOT/.venv/lib/python3.12/site-packages/prefig/_prefigure.cpython-312-x86_64-linux-gnu.so"

PERF_DATA="/tmp/${DIAGRAM}.perf"
PERF_SCRIPT_TXT="/tmp/${DIAGRAM}_script.txt"
SPEEDSCOPE_FILE="/tmp/${DIAGRAM}.speedscope"
# stackcollapse-perf.pl + export_tree.py expect these exact (un-parameterised)
# names in cwd, so we always write to these and the per-diagram pruned JSON
# is renamed at the end of the run.
FOLDED_STACKS="/tmp/folded_stacks.txt"
PRUNED_JSON="/tmp/${DIAGRAM}_pruned_trace.json"

# External tools used by the post-processing pipeline
FLAMEGRAPH_COLLAPSE="$HOME/FlameGraph/stackcollapse-perf.pl"
EXPORT_TREE_PY="$REPO_ROOT/export_tree.py"

# Prune-target arguments for export_tree.py.  These are the empirically-known
# coordinates of the first pybind11 dispatcher frame inside the implicit.xml
# trace and they happen to work for every diagram in the example suite under
# the current build.  If a future build inlines or reorders frames, override
# these via the environment:
#   PRUNE_FUNC=... PRUNE_LEVEL=... ./profile.sh implicit
PRUNE_FUNC="${PRUNE_FUNC:-pybind11::cpp_function::dispatcher}"
PRUNE_LEVEL="${PRUNE_LEVEL:-12}"

# ---- pretty printing ----

# Color codes (only if stdout is a terminal)
if [[ -t 1 ]]; then
    BOLD=$'\033[1m'; DIM=$'\033[2m'; RED=$'\033[31m'
    GREEN=$'\033[32m'; YELLOW=$'\033[33m'; CYAN=$'\033[36m'; RESET=$'\033[0m'
else
    BOLD=''; DIM=''; RED=''; GREEN=''; YELLOW=''; CYAN=''; RESET=''
fi

step() { echo; echo "${BOLD}${CYAN}==>${RESET} ${BOLD}$*${RESET}"; }
ok()   { echo "${GREEN}    OK${RESET}  $*"; }
warn() { echo "${YELLOW}  WARN${RESET}  $*"; }
err()  { echo "${RED} ERROR${RESET}  $*" >&2; }
info() { echo "${DIM}        $*${RESET}"; }

die() { err "$1"; exit "${2:-1}"; }

# ---- preflight checks ----

step "Preflight checks"

if [[ ! -x "$VENV_PY" ]]; then
    die "virtualenv not found at $VENV_PY
       Create it with:  python3 -m venv .venv && .venv/bin/pip install -e ."
fi
ok "venv python: $VENV_PY"

if [[ ! -f "$PROFILER_SCRIPT" ]]; then
    die "profiling_comparison.py not found at $PROFILER_SCRIPT"
fi
ok "profiler script: $PROFILER_SCRIPT"

if ! command -v perf >/dev/null; then
    die "perf is not installed.  Install with:  sudo apt install linux-tools-generic"
fi
ok "perf: $(perf --version 2>&1 | head -1)"

if ! command -v taskset >/dev/null; then
    die "taskset is not installed.  Install with:  sudo apt install util-linux"
fi

# Check that the diagram name is recognized by profiling_comparison.py
if ! "$VENV_PY" "$PROFILER_SCRIPT" --list 2>/dev/null | grep -qiw "$DIAGRAM"; then
    err "diagram '$DIAGRAM' is not a valid example."
    echo "Available diagrams:" >&2
    "$VENV_PY" "$PROFILER_SCRIPT" --list 2>/dev/null | sed 's/^/  /' >&2
    exit 2
fi
ok "diagram: $DIAGRAM ($RUNS runs)"

# ---- verify the .so has debug symbols ----

step "Verifying the C++ extension has debug symbols"

if [[ ! -f "$SO_PATH" ]]; then
    die "extension not found at $SO_PATH
       Rebuild with:
         pip uninstall -y prefig && rm -rf build/ prefigure-cpp/build/
         SKBUILD_CMAKE_ARGS=\"-DCMAKE_BUILD_TYPE=RelWithDebInfo;-DCMAKE_CXX_FLAGS_RELWITHDEBINFO=-O2 -g -DNDEBUG -fno-omit-frame-pointer;-DCMAKE_INSTALL_DO_STRIP=OFF;-DCMAKE_STRIP=:\" \\
             .venv/bin/pip install -e . --no-deps --config-settings=install.strip=false"
fi

SO_INFO=$(file "$SO_PATH")
info "$SO_INFO"

if echo "$SO_INFO" | grep -q "stripped"; then
    if echo "$SO_INFO" | grep -q "not stripped"; then
        ok ".so is not stripped (good)"
    else
        die ".so is stripped — perf will show unresolved addresses.
       Rebuild with the SKBUILD_CMAKE_ARGS line shown in profile.sh."
    fi
fi

if echo "$SO_INFO" | grep -q "with debug_info"; then
    ok "DWARF debug info present"
else
    warn ".so does not declare 'with debug_info' — symbol resolution may be partial"
fi

SYM_COUNT=$(nm -C "$SO_PATH" 2>/dev/null | grep -c "prefigure::" || true)
if [[ "$SYM_COUNT" -lt 100 ]]; then
    warn "only $SYM_COUNT prefigure:: symbols visible (expected ~1000)"
else
    ok "$SYM_COUNT prefigure:: symbols visible"
fi

# ---- open up perf for non-root sampling ----

step "Checking kernel.perf_event_paranoid"

PARANOID=$(cat /proc/sys/kernel/perf_event_paranoid 2>/dev/null || echo 4)
info "current value: $PARANOID"

if [[ "$PARANOID" -gt 1 ]]; then
    warn "perf_event_paranoid=$PARANOID is too restrictive for user sampling"
    info "lowering to -1 (requires sudo, lasts until reboot)"
    if sudo sysctl -w kernel.perf_event_paranoid=-1 >/dev/null; then
        sudo sysctl -w kernel.kptr_restrict=0 >/dev/null || true
        ok "perf_event_paranoid lowered"
    else
        die "could not lower perf_event_paranoid — run manually:
             sudo sysctl -w kernel.perf_event_paranoid=-1"
    fi
else
    ok "perf_event_paranoid=$PARANOID is permissive enough"
fi

# ---- warm the disk cache ----

step "Warming the MathJax disk cache (so daemon spawn does not pollute the profile)"

rm -rf "$HOME/.cache/prefigure"
info "wiped ~/.cache/prefigure"

if "$VENV_PY" "$PROFILER_SCRIPT" -d "$DIAGRAM" --runs 1 --no-python >/dev/null 2>&1; then
    ok "cache warmed"
else
    warn "warmup run failed — proceeding anyway, but the profile may include daemon-spawn cost"
fi

# ---- clean up any stale perf output from a previous run ----

for stale in "$PERF_DATA" "$PERF_SCRIPT_TXT" "$SPEEDSCOPE_FILE" "$FOLDED_STACKS" "$PRUNED_JSON"; do
    if [[ -e "$stale" ]]; then
        rm -f "$stale"
        info "removed stale $stale"
    fi
done

# ---- record ----

step "Recording perf profile (frame-pointer unwinding, pinned to core 0)"
info "command:  taskset -c 0 perf record -F 999 -g --call-graph=fp -o $PERF_DATA -- $VENV_PY $PROFILER_SCRIPT -d $DIAGRAM --runs $RUNS --no-python"

# Notes on the flags:
#   -F 999                  sample at 999 Hz (high enough for hot loops, sub-1kHz to avoid beating)
#   -g --call-graph=fp      frame-pointer unwinding — fast, accurate, and crucially does NOT
#                           invoke addr2line (which storms on DWARF v5 .so files with binutils <= 2.42)
#   taskset -c 0            pin to core 0 — avoids the Intel hybrid-CPU PMU bug where perf
#                           tries to attach to both cpu_atom and cpu_core simultaneously and
#                           silently produces a 0-byte file
#   --no-python             skip the Python backend so 100% of samples land in C++
#
# We do NOT redirect stderr — perf's "Woken up N times" / "Captured X MB" messages are
# important for diagnosing whether sampling actually worked.

if ! taskset -c 0 perf record \
        -F 999 \
        -g --call-graph=fp \
        -o "$PERF_DATA" \
        -- "$VENV_PY" "$PROFILER_SCRIPT" -d "$DIAGRAM" --runs "$RUNS" --no-python; then
    die "perf record failed (exit $?).  See messages above."
fi

# ---- verify the .perf file is well-formed ----

step "Verifying $PERF_DATA"

if [[ ! -f "$PERF_DATA" ]]; then
    die "$PERF_DATA was not created"
fi

PERF_SIZE=$(stat -c '%s' "$PERF_DATA")
PERF_SIZE_HUMAN=$(numfmt --to=iec "$PERF_SIZE" 2>/dev/null || echo "${PERF_SIZE}B")
info "size: $PERF_SIZE_HUMAN"

if [[ "$PERF_SIZE" -lt 4096 ]]; then
    die "$PERF_DATA is suspiciously small ($PERF_SIZE_HUMAN).
       This usually means perf collected zero samples.  Common causes:
         - perf was killed before it could finalize the file
         - the workload was too short for any sample to land
         - the CPU PMU is unavailable (try without taskset, or check dmesg)"
fi

# perf report --header-only validates that the file is structurally sound
if ! perf report -i "$PERF_DATA" --header-only >/dev/null 2>&1; then
    die "perf report could not parse $PERF_DATA — file is corrupt"
fi
ok "$PERF_DATA is valid ($PERF_SIZE_HUMAN)"

SAMPLE_COUNT=$(perf report -i "$PERF_DATA" --stdio --header-only 2>&1 \
    | grep -oP 'sample_count\s*=\s*\K[0-9]+' || true)
if [[ -n "$SAMPLE_COUNT" ]]; then
    info "sample count: $SAMPLE_COUNT"
fi

# ---- post-process: perf script -> stackcollapse -> export_tree ---------------
#
# This is the pipeline the user runs to produce the pruned hierarchical call
# tree that diagnoses the C++ hot path.  It replaces the previous in-terminal
# exprtk-fraction summary with a more thorough JSON trace that can be inspected
# offline.  The three steps:
#
#   1. perf script              perf.data -> per-sample stack text
#   2. stackcollapse-perf.pl    per-sample stacks -> folded "stack;stack;stack count" lines
#   3. export_tree.py           folded stacks    -> pruned JSON tree rooted at the
#                                                   first pybind11 dispatcher frame

# ---- step 1: perf script ----

step "Converting perf data to per-sample script text"
info "command:  perf script -i $PERF_DATA --no-inline > $PERF_SCRIPT_TXT"

# --no-inline is critical: without it perf script tries to expand inlined functions
# via DWARF and can take MULTIPLE MINUTES on a profile this size.  The output is
# byte-identical to the un-flagged version for stackcollapse-perf.pl's purposes.
if ! perf script -i "$PERF_DATA" --no-inline > "$PERF_SCRIPT_TXT" 2>/dev/null; then
    die "perf script failed (exit $?).  Try running it manually:
       perf script -i $PERF_DATA --no-inline > $PERF_SCRIPT_TXT"
fi

SCRIPT_LINES=$(wc -l < "$PERF_SCRIPT_TXT")
SCRIPT_SIZE=$(stat -c '%s' "$PERF_SCRIPT_TXT")
SCRIPT_SIZE_HUMAN=$(numfmt --to=iec "$SCRIPT_SIZE" 2>/dev/null || echo "${SCRIPT_SIZE}B")
info "lines: $SCRIPT_LINES   size: $SCRIPT_SIZE_HUMAN"

if [[ "$SCRIPT_LINES" -lt 100 ]]; then
    die "$PERF_SCRIPT_TXT has only $SCRIPT_LINES lines — perf collected too few samples"
fi
ok "$PERF_SCRIPT_TXT ready"

# Export a copy under a speedscope-friendly name.  speedscope.app reads
# `perf script` text directly, so this file is drag-and-droppable as-is.
cp -f "$PERF_SCRIPT_TXT" "$SPEEDSCOPE_FILE"
ok "$SPEEDSCOPE_FILE ready (drag onto https://speedscope.app)"

# ---- step 2: stackcollapse-perf.pl ----

step "Folding stacks with stackcollapse-perf.pl"
info "command:  $FLAMEGRAPH_COLLAPSE $PERF_SCRIPT_TXT > $FOLDED_STACKS"

if [[ ! -x "$FLAMEGRAPH_COLLAPSE" ]]; then
    die "stackcollapse-perf.pl not found or not executable at $FLAMEGRAPH_COLLAPSE
       Install Brendan Gregg's FlameGraph tools with:
         git clone https://github.com/brendangregg/FlameGraph.git ~/FlameGraph"
fi

if ! "$FLAMEGRAPH_COLLAPSE" "$PERF_SCRIPT_TXT" > "$FOLDED_STACKS"; then
    die "stackcollapse-perf.pl failed (exit $?)"
fi

FOLDED_LINES=$(wc -l < "$FOLDED_STACKS")
FOLDED_SIZE=$(stat -c '%s' "$FOLDED_STACKS")
FOLDED_SIZE_HUMAN=$(numfmt --to=iec "$FOLDED_SIZE" 2>/dev/null || echo "${FOLDED_SIZE}B")
info "lines: $FOLDED_LINES   size: $FOLDED_SIZE_HUMAN"

if [[ "$FOLDED_LINES" -lt 10 ]]; then
    die "$FOLDED_STACKS has only $FOLDED_LINES lines — stackcollapse produced no useful output"
fi
ok "$FOLDED_STACKS ready"

# ---- step 3: export_tree.py ----

step "Building pruned hierarchical call tree (export_tree.py)"
info "command:  $VENV_PY $EXPORT_TREE_PY $FOLDED_STACKS --func '$PRUNE_FUNC' --level $PRUNE_LEVEL --out $PRUNED_JSON"

if [[ ! -f "$EXPORT_TREE_PY" ]]; then
    die "export_tree.py not found at $EXPORT_TREE_PY"
fi

# Capture both the JSON dump (which export_tree.py prints to stdout) and any
# error output, but only show them on failure or if export_tree.py reports the
# prune target wasn't found.
EXPORT_OUTPUT=/tmp/_export_tree_output.txt
if ! "$VENV_PY" "$EXPORT_TREE_PY" \
        "$FOLDED_STACKS" \
        --func "$PRUNE_FUNC" \
        --level "$PRUNE_LEVEL" \
        --out "$PRUNED_JSON" \
        > "$EXPORT_OUTPUT" 2>&1; then
    cat "$EXPORT_OUTPUT" >&2
    rm -f "$EXPORT_OUTPUT"
    die "export_tree.py failed"
fi

# export_tree.py prints "Error: Could not find function ... at level ..." to
# stdout (not stderr) and still exits 0 in that case.  Detect it explicitly so
# the script fails loudly instead of silently producing no JSON.
if grep -q "^Error: Could not find function" "$EXPORT_OUTPUT"; then
    grep "^Error:" "$EXPORT_OUTPUT" >&2
    rm -f "$EXPORT_OUTPUT"
    die "prune target '$PRUNE_FUNC' at level $PRUNE_LEVEL not found in $FOLDED_STACKS
       Override the target with environment variables, e.g.:
         PRUNE_FUNC='your::frame' PRUNE_LEVEL=N ./profile.sh $DIAGRAM"
fi
rm -f "$EXPORT_OUTPUT"

if [[ ! -f "$PRUNED_JSON" ]]; then
    die "export_tree.py did not produce $PRUNED_JSON"
fi

JSON_SIZE=$(stat -c '%s' "$PRUNED_JSON")
JSON_SIZE_HUMAN=$(numfmt --to=iec "$JSON_SIZE" 2>/dev/null || echo "${JSON_SIZE}B")
info "size: $JSON_SIZE_HUMAN"
ok "$PRUNED_JSON ready"

# ---- final summary ----

step "Done"

echo "${BOLD}Outputs:${RESET}"
echo "  raw perf data       $PERF_DATA"
echo "  perf script text    $PERF_SCRIPT_TXT"
echo "  speedscope input    $SPEEDSCOPE_FILE"
echo "  folded stacks       $FOLDED_STACKS"
echo "  pruned trace JSON   $PRUNED_JSON"
