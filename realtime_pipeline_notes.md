# Real-time pipeline feasibility — open question

**Context:** the webdemo (FastAPI server in `webdemo/`, committed in `ee26cbb`)
exposes the C++ renderer over HTTP. Open question: is it fast enough to drive
a slider in the browser at ~30 Hz with a client→server→render→refresh loop?

Short answer from the back-of-envelope analysis: **yes for simple diagrams,
marginal for the heavy ones, and the bottleneck likely moves to the browser
rather than the server.** This file records the analysis and what to measure
before committing to an architecture.

## Budget for 30 Hz (33.3 ms / frame)

### Server-side (already measured)
Per the Phase 2 perf benchmarks (warm cache, n=10):

| Diagram         | C++ render |
|-----------------|------------|
| tangent         | 0.6 ms     |
| derivatives     | 1.1 ms     |
| de-system       | 0.6 ms     |
| diffeqs         | 1.6 ms     |
| **implicit**    | **4.7 ms** |
| projection      | 0.7 ms     |
| riemann         | 0.8 ms     |
| roots_of_unity  | 0.8 ms     |

FastAPI + Pydantic + uvicorn loopback adds ~1–2 ms. So **server round-trip
is ~2–7 ms warm**, well inside the 30 Hz budget.

### Client-side (NOT yet measured)
- `innerHTML = svg` on a 119 KB SVG with 31 paths and inline MathJax glyphs:
  expected ~10–30 ms on a mid-range laptop based on typical browser
  layout/parse cost for SVGs of this complexity. This is the unverified
  estimate — must be measured before trusting.
- Browser JSON parse / fetch overhead: ~1 ms.

### Estimated end-to-end (warm)
- **implicit.xml**: ~15–35 ms — borderline 30 Hz, comfortable 20 Hz.
- **lighter diagrams (tangent, projection)**: ~10–15 ms — easy 60 Hz.

## Three things that will bite under a slider workload

1. **MathJax cache misses.** If the slider only varies geometry (e.g., a `k`
   value driving an `<implicit-curve>`), labels are cached and C++ render
   stays ~5 ms. If the slider changes label *text* (LaTeX in a `<label>`),
   each new string costs 5–30 ms through the MathJax daemon. Need to design
   slider semantics so labels stay stable.

2. **`innerHTML` thrash.** Replacing the entire SVG every frame destroys and
   rebuilds the DOM. The proper fix is server-side patching: return a delta
   of which elements changed and patch in place. Or render geometry
   server-side and draw lines client-side via Canvas/SVG primitives.

3. **Backpressure.** A single uvicorn worker serializes the blocking C++
   calls (the C++ module is not audited for thread safety, hence
   `--workers 1`). If the slider fires faster than the render time,
   requests pile up. Mitigate with one of:
   - Client-side throttling (~30 ms debounce) — simplest.
   - `AbortController` to cancel in-flight requests when a new slider value
     arrives — keeps latency bounded.
   - WebSockets with a "latest value wins" queue server-side — best for
     genuine real-time but more code.

## What we don't yet know

- Real browser cost of `innerHTML = svg` for our actual SVG output. The
  10–30 ms estimate is a guess; could easily be 5 ms or 50 ms.
- Whether the MathJax daemon hits the disk cache vs. going through Node
  for typical slider inputs.
- Whether two concurrent browser tabs (i.e. concurrent FastAPI requests)
  serialize cleanly on the C++ side without crashes — the global state in
  exprtk parser cache, label dedup, etc. is unaudited.
- Real-world network latency budget if the server isn't on localhost.

## Action items when we come back to this

1. **Add a benchmark endpoint** to `webdemo/server.py`, e.g.
   `GET /api/bench?n=100&example=implicit`, that times N renders end-to-end
   and returns mean/p50/p95/p99 in JSON. Pure server-side timing, no
   browser involved.

2. **Add a slider demo page** to `webdemo/static/`, e.g. `slider.html`,
   with a numeric `<input type="range">` bound to a query param the server
   threads into the implicit curve's `k` value. Measure achievable FPS in
   the browser via `performance.now()` deltas, log to console / on-page.
   This catches the `innerHTML` cost we haven't measured.

3. **Also measure with `AbortController`-based cancellation** so we know
   the realistic cadence under back-to-back drags, not just the
   single-request RTT.

4. Decide based on data:
   - If the slider page hits 30 Hz: ship the simple HTTP architecture.
   - If 10–20 Hz: add throttling + request cancellation, probably enough.
   - If <10 Hz: redesign — server-side diff patching, WebSockets, or
     server-as-geometry-source with client-side rendering.

5. Audit `_prefigure` for thread safety if multi-worker / concurrent
   request handling becomes a requirement. Otherwise keep `--workers 1`.

## Where the code lives (for when context is gone)

- FastAPI server: `webdemo/server.py`
- C++ binding (recently patched to return `(svg, annotations_or_None)`):
  `prefigure-cpp/{include,src,bindings}/...parse...`
- Python wrapper: `prefig/engine.py:build_from_string`
- Phase 2 perf numbers cited above: commit `73b4986` message body
- Branch: `cpp-packaging`, last shipping commit `ee26cbb`
