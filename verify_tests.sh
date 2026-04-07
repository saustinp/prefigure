#!/usr/bin/env bash
#
# verify_tests.sh — run every PreFigure test suite and a backend size diff.
#
# Run from the repository root:   ./verify_tests.sh
#
# Sections:
#   1. Python pytest                       (tests/test_prefigure.py)
#   2. Python pytest, forced Python        (PREFIGURE_USE_PYTHON=1)
#   3. C++ Catch2 suite                    (prefigure-cpp/build via ctest)
#   4. C++ backend pytest                  (prefigure-cpp/tests/test_cpp_backend.py)
#   5. End-to-end size diff for all 8 example diagrams (C++ vs Python)
#
# A non-zero exit means at least one section failed.

set -u

REPO_ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$REPO_ROOT"

VENV_PY="$REPO_ROOT/.venv/bin/python"
VENV_PREFIG="$REPO_ROOT/.venv/bin/prefig"

if [[ ! -x "$VENV_PY" ]]; then
    echo "ERROR: virtualenv not found at $VENV_PY"
    echo "       Create it with:  python3 -m venv .venv && .venv/bin/pip install -e ."
    exit 1
fi

# Track per-section results so we can print a summary at the end.
declare -a SECTION_NAMES SECTION_RESULTS
overall_status=0

run_section() {
    local name="$1"
    shift
    echo
    echo "=============================================================="
    echo "  $name"
    echo "=============================================================="
    if "$@"; then
        SECTION_NAMES+=("$name")
        SECTION_RESULTS+=("PASS")
    else
        SECTION_NAMES+=("$name")
        SECTION_RESULTS+=("FAIL")
        overall_status=1
    fi
}

# ----------------------------------------------------------------------------
# 1. Python pytest (default backend — C++ if the extension is built)
# ----------------------------------------------------------------------------
run_section "1. pytest (default backend)" \
    "$VENV_PY" -m pytest tests/ -v

# ----------------------------------------------------------------------------
# 2. Python pytest, forced to the pure-Python fallback
# ----------------------------------------------------------------------------
run_section "2. pytest (PREFIGURE_USE_PYTHON=1)" \
    env PREFIGURE_USE_PYTHON=1 "$VENV_PY" -m pytest tests/ -v

# ----------------------------------------------------------------------------
# 3. C++ Catch2 suite (CTM, calculus, math_utilities, user_namespace, diagram
#    integration, golden-file regressions)
# ----------------------------------------------------------------------------
cpp_tests() {
    if [[ ! -d prefigure-cpp/build ]]; then
        echo "Configuring prefigure-cpp/build for the first time..."
        cmake -S prefigure-cpp -B prefigure-cpp/build || return 1
    fi
    cmake --build prefigure-cpp/build --parallel || return 1
    ctest --test-dir prefigure-cpp/build --output-on-failure
}
run_section "3. C++ Catch2 suite (ctest)" cpp_tests

# ----------------------------------------------------------------------------
# 4. C++ backend pytest (lives under prefigure-cpp/tests/, not picked up by
#    the rootdir pytest config — invoke it explicitly).
# ----------------------------------------------------------------------------
if [[ -f prefigure-cpp/tests/test_cpp_backend.py ]]; then
    run_section "4. pytest prefigure-cpp/tests/test_cpp_backend.py" \
        "$VENV_PY" -m pytest prefigure-cpp/tests/test_cpp_backend.py -v
fi

# ----------------------------------------------------------------------------
# 5. End-to-end size diff: build every example with both backends and compare
# ----------------------------------------------------------------------------
size_diff() {
    if [[ ! -x "$VENV_PREFIG" ]]; then
        echo "ERROR: prefig CLI not found at $VENV_PREFIG"
        return 1
    fi

    local cpp_dir=/tmp/pf-cpp py_dir=/tmp/pf-py
    rm -rf "$cpp_dir" "$py_dir"
    mkdir -p "$cpp_dir" "$py_dir"

    for f in prefig/resources/examples/*.xml; do
        local base
        base=$(basename "$f" .xml)

        rm -rf prefig/resources/examples/output
        if "$VENV_PREFIG" build "$f" >/dev/null 2>&1; then
            cp "prefig/resources/examples/output/$base.svg" "$cpp_dir/$base.svg" 2>/dev/null
        fi

        rm -rf prefig/resources/examples/output
        if PREFIGURE_USE_PYTHON=1 "$VENV_PREFIG" build "$f" >/dev/null 2>&1; then
            cp "prefig/resources/examples/output/$base.svg" "$py_dir/$base.svg" 2>/dev/null
        fi
    done

    "$VENV_PY" - <<'PY'
import os, sys
cpp_dir = '/tmp/pf-cpp'
py_dir  = '/tmp/pf-py'

if not os.listdir(cpp_dir) or not os.listdir(py_dir):
    print("No SVGs were generated — something is wrong with the build.")
    sys.exit(1)

print(f'{"file":<22} {"C++":>8} {"Python":>8} {"diff%":>8}  status')
print('-' * 60)

worst = 0.0
status = 0
for n in sorted(os.listdir(cpp_dir)):
    cp = os.path.join(cpp_dir, n)
    pp = os.path.join(py_dir,  n)
    if not os.path.exists(pp):
        print(f'{n:<22} {os.path.getsize(cp):>8} {"missing":>8}      —  PY-FAIL')
        status = 1
        continue
    c = os.path.getsize(cp)
    p = os.path.getsize(pp)
    d = (c - p) / p * 100
    worst = max(worst, abs(d))
    flag = '<-- check' if abs(d) > 8 else 'ok'
    print(f'{n:<22} {c:>8} {p:>8} {d:>+7.1f}%  {flag}')

print()
print(f'worst-case absolute size diff: {worst:.1f}%')
print(f'(expected: every file within ~2.5% of Python after the recent fixes)')

# Confirm the C++ outputs carry the backend marker comment we added.
import glob
mark_ok = mark_bad = 0
for f in glob.glob(f'{cpp_dir}/*.svg'):
    with open(f) as fp:
        first = fp.readline()
    if 'PreFigure C++ backend' in first:
        mark_ok += 1
    else:
        mark_bad += 1
print(f'C++ marker comment present in {mark_ok}/{mark_ok + mark_bad} SVG(s)')

if mark_bad or status:
    sys.exit(1)
PY
}
run_section "5. End-to-end size diff (C++ vs Python)" size_diff

# ----------------------------------------------------------------------------
# Summary
# ----------------------------------------------------------------------------
echo
echo "=============================================================="
echo "  Summary"
echo "=============================================================="
for i in "${!SECTION_NAMES[@]}"; do
    printf '  [%s] %s\n' "${SECTION_RESULTS[$i]}" "${SECTION_NAMES[$i]}"
done
echo

if [[ $overall_status -eq 0 ]]; then
    echo "All sections passed."
else
    echo "One or more sections FAILED."
fi
exit $overall_status
