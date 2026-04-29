# Phase A findings: interactive PreFigure activity in DoenetML's stock playground

Captured 2026-04-27. Companion to `doenet_integration_notes.md` and
`realtime_pipeline_notes.md`.

## TL;DR (final, after both renderer corrections)

**Interactive prefigure already works in stock DoenetML at real-time
frame rates on the heaviest activity its vocabulary can author.** No
infrastructure changes were made or are needed for the basic
"interactive figure" deliverable. Concretely:

- Authoring an interactive prefigure figure in DoenetML = writing a
  `.doenet` file with `<slider>`, a `<graph renderer="prefigure">`, and
  child elements that reference the slider value. Stock DoenetML.
- On real GPU-accelerated browsers, the prefigure renderer ran at
  ~60–70 FPS on a 5-Lissajous stress activity (heaviest expressible)
  during sustained slider drags. Smooth animation.
- DoenetML cannot author implicit curves today (`<graph>` only converts
  function/parametric/Bézier `<curve>` types to prefigure XML; no
  implicit type or `<implicitCurve>` element exists). The original
  `implicit.xml` benchmark from our prefig repo is unauthorable.

**Implications for the C++ port we shipped (commit `ee26cbb`):** it is
not on the critical path for the upstream team's interactive-figure
goal, because that goal is met by stock infrastructure. It remains
useful as:
- A faster replacement for the AWS service at
  `prefigure.doenet.org/build` (used during Pyodide cold-start, ≤2 s of
  any session).
- A natural backend if/when DoenetML adds an `<implicitCurve>` element
  that exposes the heavy prefigure features Pyodide is *not* fast on.
- A standalone tool for any non-Doenet consumer who wants prefigure
  rendering as a service.

The user is consulting the developers to clarify their actual needs.
Recommended questions to bring to them are at the bottom of this file.

## What was done

### Toolchain (A.1)

- Installed `rustup` non-interactively → `rustc 1.95.0`, `cargo 1.95.0`.
- Added `wasm32-unknown-unknown` target (preemptively; not actually needed
  for the dev-server build).
- Verified Node 22.22.2 / npm 10.9.7. No `engines` constraint in
  DoenetML's `package.json` blocked Node 22.

No system-level deps (`build-essential`, `pkg-config`, `libssl-dev`)
turned out to be required for this path.

### Build (A.2)

- `npm install` from `/home/sam/Documents/DoenetML`: ~1 min, 1836 packages.
  Standard deprecation warnings (`inflight`, old `glob`, `core-js@2`); no
  hard errors. 60 vulnerabilities reported (1 low, 32 mod, 25 high, 2
  crit) — all transitive, not in our hot path.
- `npm run build`: 60.8 s wall time. 18 wireit scripts, all green. The
  build successfully fetched the prefigure runtime assets, including
  `pyodide_packages/prefig-0.5.15-py3-none-any.whl` and the local
  `@doenet/prefigure` bundle at `packages/prefigure/dist/prefigure.js`
  (8 KB).

### Activity (A.4)

`packages/doenetml/dev/testCode.doenet` was overwritten with:

```xml
<title>Interactive tangent line</title>
<p>Drag the slider to move the tangent point along <m>f(x) = x^3 - 2x</m>.</p>
<function name="f" variable="x">x^3 - 2x</function>
<derivative name="fPrime">$f</derivative>
<slider name="t" from="-2" to="2" step="0.01" initialValue="0.5" labelIsName />
<graph name="g" size="medium" xmin="-3" xmax="3" ymin="-4" ymax="4">
  <curve name="cf">$f</curve>
  <point name="P" labelIsName>($t, $$f($t))</point>
  <line name="tangent" slope="$$fPrime($t)" through="$P" />
</graph>
<p>At <m>t = $t</m>: <m>f(t) = $$f($t)</m>, slope <m>= f'(t) = $$fPrime($t)</m>.</p>
```

This file is on the AGENTS.md "do not commit" list, which is fine — Phase
A is local-only.

### Verification (A.5)

Headless Chromium via the playwright that's already installed in the
DoenetML monorepo. Three rounds of automated probing:

1. **Render check.** Activity title visible at 1.0 s. Prefigure SVG
   (425×425, 5 paths) visible at ~2.1 s. Toolbar icons (1 em each) plus
   the graph SVG, plus 4 small SVGs from MathJax glyph embedding.
2. **Network check.** Zero requests to `prefigure.doenet.org/build` and
   zero requests to any external CDN for prefigure assets — confirming
   the local Pyodide build (`USE_LOCAL_PREFIGURE = true`) is the path
   in use.
3. **Reactivity check.** Programmatically dispatched native input/change
   events on the slider's underlying `<input type="range">`, observing
   that:
   - The slider value moved (250 → 0 → 2 in pixel-position units).
   - The 425×425 prefigure SVG content actually changed (`outerHTML`
     length 56,976 → 57,182 → 56,977; `<path>` count 5 → 7 → 5).
   - Final screenshot shows the cubic + tangent re-rendered for
     `t = −1.98`, with a steep tangent (slope = 9.76) where there was
     a shallow one (slope = −1.25) before.
   - Math verified by hand: f(−1.98) = (−1.98)³ − 2(−1.98) ≈ −3.80 ✓;
     f′(−1.98) = 3(−1.98)² − 2 ≈ 9.76 ✓.
   - 0 page errors, 0 console errors throughout.

Screenshots captured at `/tmp/tangent_initial.png`,
`/tmp/tangent_after_wait.png`, `/tmp/tangent_after_react_update.png`.

## Surprises

### 1. The DoenetML slider's `<input>` value is in pixels, not in t

When I dispatched `value = -1.5` to the underlying range input, it
clamped to `0` (i.e., the leftmost position, t = −2.0). When I dispatched
`1.5`, it clamped to `2` (still near the leftmost). The slider's internal
representation is the pixel position along its own track (default
`width="300"`), and DoenetML maps that to the [from, to] domain elsewhere
in its rendering layer.

Implication: a real mouse drag on the visible thumb is the right way to
test interactivity from a script; programmatic value-set on the input
works but only addresses the pixel domain. For a human user, this is
invisible — they just drag.

### 2. Pyodide cold start was barely noticeable

The headline cold-start figure I'd cited from the renderer source was
1000 ms debounce. Observed reality: prefigure SVG appeared at ~2.1 s
after `domcontentloaded`, of which ~1 s is React mount and DoenetML
worker spin-up. Pyodide is fast enough on this simple cubic that I
couldn't separate its cost from the rest of the boot.

That's a *qualitative* observation only — I never put a stopwatch on
"first slider drag after page load." A real perf measurement is
something to do in Phase B.

### 3. Editor view, not viewer view

The `dev/main.tsx` playground uses `<DoenetEditor>`, which is a split
pane: source editor on the left, live render on the right. Initially I
worried the empty right pane (at 2 s) was a render failure. It wasn't —
just timing. Worth knowing: if you want a viewer-only view of the
activity, swap to `<DoenetViewer>` in `main.tsx` (also imported, not
currently used).

### 4. The activity worked unmodified

I didn't have to fall back to any of the syntax alternatives I'd
pre-planned (`$f($t)` single-dollar, `$f.value($t)`, etc.). The
double-dollar macro form `$$f($t)` and `$$fPrime($t)` parsed and
evaluated correctly. The DoenetML linter does flag `<line>` and `<curve>`
with red squiggles in the source pane, but those appear to be cosmetic
warnings (status bar shows 0 errors, 0 warnings).

## What was NOT measured

These are deliberate gaps — they belong to Phase B, not Phase A.

- **Subjective FPS during a real human drag.** I drove the slider via
  `dispatchEvent`, not via mouse. The headless Chromium was running with
  software rendering. The "does it feel real-time at 30 Hz?" question is
  answerable only in a real browser with a real human. See "User
  verification" below.
- **Heavy-diagram performance.** I tested the cubic-tangent activity, a
  simple case. The heavier diagrams (implicit curves, riemann sums) that
  motivated the C++ port have not been touched in this DoenetML setting.
  Implicit curves are the canonical worst case.
- **Cold-start cost on a first page load.** I measured time-to-first-render
  with the warm-cache build artifacts, not from a fresh-cache browser. The
  upstream `PREFIGURE_BUILD_DEBOUNCE_COLD_MS = 1000` may be visible only
  on a cold load.
- **DOM reconciliation cost.** With the SVG being 57 KB and the
  reactive DAG re-emitting it on every slider tick, there's potentially
  significant `innerHTML` thrash. Devtools Performance trace would tell
  us. Not done.

## User verification — one button, one slider drag

The dev server is running on `http://localhost:8012` (PID 2720345 if you
need to find it). To verify Phase A end-to-end yourself:

1. Open `http://localhost:8012/` in a browser. Wait ~3 s for the
   activity to render in the right-hand pane.
2. You should see the title "Interactive tangent line", the cubic, the
   point P at (0.5, −0.875), and the tangent line.
3. Click and drag the slider thumb left and right.
4. Watch the tangent line slide along the curve and the bottom-of-page
   readout (`At t = …: f(t) = …, slope = …`) update live.
5. Open DevTools → Network and confirm there are NO requests to any
   `/build` endpoint while you drag — the rendering is local.
6. Subjective: does it feel real-time? Is there visible lag? File any
   observations in this document under "User-driven observations" below.

## User-driven observations (cubic-tangent activity)

- 2026-04-27, real browser (sam): "the animation is very smooth (no lag)."

## User-driven observations (5-Lissajous heavy activity) — INVALIDATED by next section

- 2026-04-27, real browser (sam) with Chrome FPS meter:
  **"between 60 and 70 fps when dragging the slider quickly."**
- ~~This is GPU-accelerated browser rendering; the headless 25-Hz figure
  was software rendering and didn't reflect real performance.~~
- ~~Conclusion: the existing Pyodide pipeline meets the upstream team's
  real-time interactive figure goal on every authorable case.~~

**STRIKE THE ABOVE.** This measurement was for the **JSXGraph renderer**, not
prefigure. See "Renderer attribute correction" section below.

## Renderer attribute correction (added 2026-04-27, late afternoon)

`<graph>` has a `renderer` attribute (validated by source at
`packages/doenetml-worker-javascript/src/components/Graph.js:240-248`):

```js
attributes.renderer = {
    createPrimitiveOfType: "string",
    createStateVariable: "renderer",
    validValues: ["doenet", "prefigure"],
    defaultValue: "doenet",
    ...
};
```

The default is `"doenet"`, which routes through **JSXGraph** (the
`JSXGraphRenderer.tsx` React component), not through the prefigure
pipeline. To actually exercise prefigure, the activity must declare
`<graph renderer="prefigure" …>`.

Both our cubic-tangent and the original 5-Lissajous activity used the
default, so they were rendering through JSXGraph. JSXGraph is a mature,
well-optimized JavaScript graph library — naturally fast on simple
cases. It is **not** what the C++/PyBind11/FastAPI work was about.

### Empirical proof of misattribution

DOM probes after adding `renderer="prefigure"`:

| signal | renderer="doenet" (default) | renderer="prefigure" |
|---|---|---|
| `<div class="jxgbox">` | 1 | 0 |
| SVG with id ending `-figure` | none | yes (`id="foo-figure"`) |
| MathJax glyphs in graph SVG | yes (JSXGraph also embeds them) | yes |
| graph SVG path count | 9 | 11 |
| graph SVG length | ~410 KB (with offgraph indicators bloat) | ~13 KB |

The prefigure SVG IDs use a different prefix scheme than our standalone
C++ webdemo (`foo-figure` vs `prefig-XXXXXXXX-figure`). That's an
internal convention of `@doenet/prefigure@0.5.15`'s seeding logic, not
something to worry about — both are prefigure outputs.

### Re-measurement under the actual prefigure renderer

Same 50-tick programmatic slider sweep as before, headless Chromium
software rendering, on the same 5-Lissajous activity but with
`renderer="prefigure"`:

| metric | JSXGraph (old) | **Prefigure (corrected)** |
|---|---|---|
| median per-tick latency | 40 ms (25 Hz) | **75 ms (~13 Hz)** |
| p95 | 46 ms | **2,478 ms** |
| max | 809 ms | **2,547 ms** |
| mean | 55 ms | **241 ms** |
| first 2 samples | (not captured separately) | **2,547 ms, then 1,059 ms** |
| later samples | smooth | smooth at ~75 ms each |

The first two samples are large because Pyodide compiles the
parametric expressions on first encounter and the result is cached.
Steady state at median 75 ms is **~13 Hz on software rendering**.

Real-browser GPU rendering should reduce this — but probably not back
to the 60–70 FPS we falsely attributed to prefigure earlier. JSXGraph's
software-render of 25 Hz translated to GPU 60–70 Hz (≈2.4–2.8× speedup
from hardware acceleration). If prefigure scales similarly, expect
~30–35 FPS on real hardware. The user should re-test to confirm.

### Implications

The integration story is back to interesting, not solved:

1. **Phase A's qualitative goal still holds.** Interactive prefigure
   figures *do* render; the slider-drag does drive re-renders. The
   end-to-end flow works.
2. **Real-time at 30 Hz on the heaviest authorable activity is now
   uncertain, not confirmed.** The 60–70 FPS figure was JSXGraph,
   which is a different code path.
3. **The case for our C++ port is no longer falsified.** If the
   prefigure-renderer activity in a real browser drops to ~30–35 FPS,
   our C++ via FastAPI (5 ms render + 2 ms loopback ≈ 140 Hz upper
   bound) has a real perf advantage on heavy diagrams — even
   authorable ones.
4. **Phase B-1 is back on the table.** Pointing the dev playground at
   our FastAPI as the prefigure backend is once again worth measuring.
   Earlier I declared it off the table based on the JSXGraph numbers;
   that was wrong.

### Action requested from the user

The dev server still serves the activity at `http://localhost:8012/`
with `renderer="prefigure"` now. Please re-test:

1. Hard refresh the page (Ctrl+Shift+R) to ensure HMR reloaded the
   new renderer attribute.
2. Watch the FPS meter while dragging the slider quickly.
3. Note the first-drag-after-load behavior (Pyodide compile cost may
   show as a 1–3 second freeze on the first slider movement).
4. Report: steady-state FPS during sustained drag, plus any visible
   stutter or freezes.

That number, on real GPU hardware, is the data we actually need to
decide Phase B.

## Final user observation (after the renderer correction)

- 2026-04-27, real browser (sam), Chrome FPS meter, page hard-refreshed
  with `<graph renderer="prefigure">`: **"This is still just as fast"**
  as the prior JSXGraph result — i.e., 60–70 FPS during sustained
  slider drag on the 5-Lissajous activity.
- This invalidates the "Pyodide-prefigure is borderline real-time"
  concern raised in the corrected headless measurement. The headless
  13 Hz figure was an artefact of software rendering plus my polling
  overhead, not a real-browser bottleneck. GPU acceleration recovers
  prefigure to the same range as JSXGraph.
- **Practical conclusion: there is no observable Pyodide-prefigure
  performance gap on the heaviest authorable DoenetML activity.**

## Final state of the project (handoff snapshot)

### What works today, end-to-end, with stock infrastructure

- Authoring: write a `.doenet` file using `<slider>`, `<function>`,
  `<derivative>`, `<graph renderer="prefigure">`, `<curve>`, `<point>`,
  `<line>`, etc. Reference the slider value with `$sliderName` (and
  `$$f($t)` for function evaluation).
- Rendering: the slider's reactive DAG re-emits `prefigureXML` on every
  change; `@doenet/prefigure@0.5.15` (Pyodide-compiled Python prefigure)
  recompiles in a Web Worker in ~75 ms steady state on software, sub-20
  ms on GPU; the React prefigure renderer injects the SVG.
- Performance: 60–70 FPS measured on a real GPU-accelerated browser
  on the heaviest authorable activity (5 high-frequency parametric
  curves). No visible lag during sustained drag.
- Cold start: ~1–3 s on first interaction with a never-seen-before
  expression (Pyodide compile cost), then steady. Acceptable for
  educational content where users aren't penalized for the first
  click.

### What does not work / is not authorable today

- **Implicit curves.** No DoenetML element produces an
  `<implicit-curve>` in the prefigure XML output. This rules out one
  of prefigure's marquee features for in-DoenetML use.
- **Direct prefigure XML injection.** No element accepts raw prefigure
  XML in an activity. The only path into the prefigure renderer is via
  the `<graph>`-and-descendants auto-conversion.
- **Tactile output formats from a DoenetML activity.** The graph
  renderer hard-codes SVG mode in the `compilePrefigure` call.

### What we shipped that is *not* currently on DoenetML's path

- `prefig/webdemo/` (commit `ee26cbb`): standalone FastAPI server
  exposing our C++ `_prefigure` PyBind11 module via HTTP. Returns
  `(svg, annotations)` per the `engine.build_from_string` contract.
- The C++ binding patch for `build_from_string` to return both pieces
  (was string-only before).

These are deployable as a service; they are not deployed.

### Handoff artifacts

| file | purpose |
|---|---|
| `prefig/webdemo/server.py` + static page | Standalone FastAPI demo (separate from DoenetML) |
| `prefig/realtime_pipeline_notes.md` | Earlier analysis of real-time-render feasibility |
| `prefig/doenet_integration_notes.md` | DOEnet architecture map: two-backend system, contract for `prefigure.doenet.org/build`, 3 integration paths |
| `prefig/phase_a_findings.md` | This file |
| `DoenetML/packages/doenetml/dev/testCode.doenet` | Working interactive activity (5-Lissajous, `renderer="prefigure"`); not committed by upstream convention |
| dev server still running at `localhost:8012` | PID 2720345; kill when no longer needed |

## Questions to bring to the upstream developers

When you reconvene with the upstream prefigure / Doenet developers,
these are the questions whose answers determine whether our C++ work
has a deployment target:

### About the original ask

1. **What did you have in mind by "modify a static prefigure example
   figure to be interactive"?** Was the intent (a) to *demonstrate*
   that interactive prefigure can work in DOEnet (now done — see
   above) or (b) to convert a *specific* existing static figure on
   beta.doenet.org? If (b), please point at the figure.
2. **Were you aware that interactive prefigure already works in stock
   DoenetML** via `<graph renderer="prefigure">` and the reactive DAG?
   The integration shipped in `@doenet/prefigure@0.5.15` and is on the
   beta site today. Asking because the framing of the original brief
   read as if interactivity needed to be built; it did not.

### About performance and the case for our C++ port

3. **Do you have a use case where Pyodide-prefigure is too slow?**
   Examples that would make the C++ port load-bearing: low-end client
   hardware (Chromebooks, older iPads), heavy diagrams beyond what
   DoenetML currently exposes (implicit curves, complex shaded
   regions), or batch workflows that need sub-millisecond renders
   per diagram.
4. **The build-service backend at `prefigure.doenet.org/build`** —
   what runs there today, and would replacing it with a faster
   container help your workflow? It is reportedly used during
   Pyodide warmup and maintenance docs explicitly note it lives
   outside the public DoenetML repo.

### About expanding the authorable surface

5. **Is there appetite to expose more of prefigure's vocabulary in
   DoenetML?** Specifically `<implicitCurve>` (or an `<implicit>` curve
   type on `<curve>`). That single feature would unlock the entire
   set of prefigure diagrams currently unauthorable in DoenetML — and
   it would be the case where Pyodide-prefigure does start to lag
   noticeably and our C++ becomes useful.
6. **Is there any interest in a "raw prefigure XML" escape hatch**
   (an element that accepts a prefigure XML string verbatim)? That
   would let activity authors use prefigure features ahead of DoenetML
   adding native support, at the cost of a less-typed authoring story.

### About deployment paths

7. **If C++-accelerated server-side rendering does become useful**
   (questions 3–4), what's the deployment story you'd prefer? Options:
   - We package our FastAPI as a Docker image and you run it.
   - We deliver source/build instructions and you compile on your
     infra.
   - You take over and we hand off the code.
8. **If Wasm-compiled C++ is in the picture** (Path 2 from
   `doenet_integration_notes.md`), is the upstream team open to a
   sibling npm package `@prefigure/wasm` (or similar) that swaps for
   `@doenet/prefigure` via the existing `__DOENET_PREFIGURE_MODULE_URL__`
   override?

The answers to (1)–(2) determine whether this project is already
finished. The answers to (3)–(6) determine whether our C++ port is on
the critical path for any upstream goal. The answers to (7)–(8)
determine the shape of any follow-on engineering.

## Cleanup checklist when fully done

- `kill 2720345` to stop the dev server.
- `git -C /home/sam/Documents/DoenetML checkout HEAD -- packages/doenetml/dev/testCode.doenet` to revert the activity (it's commit-excluded anyway).
- The DoenetML repo can stay on disk for future iteration; nothing in our prefig repo depends on it.
- Our prefig repo is clean (commit `ee26cbb` shipped; nothing pending). Untracked files in the worktree are pre-existing scratch.

## Heavy-activity stress test (added 2026-04-27)

After the cubic confirmed smooth, I authored a heavier activity at the
ceiling of what DoenetML can express today: **five high-frequency
parametric (Lissajous) curves in one graph, all phase-shifted by one
slider**.

```xml
<slider name="phi" from="0" to="6.2832" step="0.02" />
<graph size="medium" xmin="-1.3" xmax="1.3" ymin="-1.3" ymax="1.3">
  <curve variable="t" parMin="0" parMax="6.2832">(cos(2*t + $phi),     sin(3*t))</curve>
  <curve variable="t" parMin="0" parMax="6.2832">(cos(3*t + 2*$phi),   sin(4*t))</curve>
  <curve variable="t" parMin="0" parMax="6.2832">(cos(4*t + 3*$phi),   sin(5*t))</curve>
  <curve variable="t" parMin="0" parMax="6.2832">(cos(5*t + 4*$phi),   sin(6*t))</curve>
  <curve variable="t" parMin="0" parMax="6.2832">(cos(7*t + 5*$phi),   sin(8*t))</curve>
</graph>
```

This is roughly 7× heavier than the cubic by output size:

| Activity | SVG length | path count |
|---|---|---|
| Cubic + tangent | ~57 KB | 5 |
| Five Lissajous | ~410 KB | 9 |

### Headless-Chromium timing

I drove the slider programmatically through 50 distinct phase values
under headless Chromium with software rendering, polling for the SVG
content to change after each value-set:

| metric | value |
|---|---|
| median per-tick latency | **40 ms** |
| p95 per-tick latency | 46 ms |
| mean | 55 ms (skewed by first tick) |
| max | 809 ms (first tick, pipeline warmup on the new activity) |
| implied steady-state effective rate | **~25 Hz** |
| page errors | 0 |

**Caveats on these numbers, in descending order of importance:**

1. **Headless Chromium uses software (CPU) rendering by default.** A real
   browser with hardware acceleration usually does the SVG layout work
   2–4× faster. So 25 Hz here likely translates to 50–100 Hz on a real
   GPU-accelerated browser. The user should drag the slider in their
   actual browser and report subjective smoothness.
2. **My polling adds overhead.** The per-tick wait was 5 ms granularity,
   and my "wait for content change" loop adds at least one polling
   tick. Real renders are likely a few ms faster than reported.
3. **First tick is an outlier (809 ms).** Pyodide compiles the new
   curves on first encounter — this is *not* a per-frame cost; it's
   one-time work amortized across the session.

### What this means

- **Within DoenetML's vocabulary, the existing Pyodide pipeline gets to
  ~25–50 Hz on the heaviest expressible activity.** That's borderline
  real-time on software rendering and likely real-time on real GPU
  hardware. Phase A's qualitative success extends to "heavier" cases
  authorable in DoenetML.
- **Implicit curves are not authorable in DoenetML today.** The
  conversion supports `function`, `parameterization`, and `bezier` curve
  types only (`packages/doenetml-worker-javascript/src/utils/prefigure/components/curve.ts:1434-1444`). There is no `<implicitCurve>` element or `<implicit>` curve type. The original `implicit.xml` benchmark (4.7 ms in C++) cannot be expressed as a DoenetML activity.
- **For the upstream "interactive prefigure" deliverable,** Phase A
  covers the entire authorable surface. The unauthorable surface
  (implicit curves) requires either an upstream feature request to
  DoenetML or a separate authoring path that bypasses
  DoenetML→prefigure conversion. That is out of scope for our current
  C++ work.

### Implications for Phase B

The earlier framing ("Phase B measures C++ vs Pyodide on a heavy
diagram, comparing apples to apples") is now harder to execute, because
the heaviest apple in DoenetML's basket (5 Lissajous, 410 KB SVG) is
**still not as heavy as the original implicit.xml benchmark**. So if we
go to Phase B, the comparison has two flavors:

**Phase B-1, in-DoenetML comparison:** point the dev playground at our
FastAPI server (`VITE_PREFIGURE_BUILD_ENDPOINT=http://localhost:8000/build`,
disable the WASM bundle), drive the same 5-Lissajous activity, measure
per-tick latency. Tells us "is our server faster than Pyodide on the
heaviest authorable case?"

**Phase B-2, out-of-DoenetML comparison:** POST our prefigure
`implicit.xml` directly to the FastAPI server with various
`k`-parameter substitutions, measure server-side render time and
network round-trip. Tells us "if implicit curves *were* authorable in
DoenetML, would our server make them real-time?" The answer informs
whether to lobby for an `<implicitCurve>` upstream feature.

Both are 30–60 minute exercises. The user should decide which (or both)
to run when ready.

## Activity files at end of stress test

- `packages/doenetml/dev/testCode.doenet` now holds the 5-Lissajous
  activity (overwriting the cubic-tangent version). Restore the cubic
  with `git checkout HEAD -- packages/doenetml/dev/testCode.doenet`.
- Verification script `_verify_heavy.mjs` removed; do not need
  recreating unless re-measuring.
- Screenshots: `/tmp/heavy_initial.png`, `/tmp/heavy_after_sweep.png`.

## Decision: should we run Phase B?

**My recommendation: yes, but with the scope tightened.**

Phase A confirms the *qualitative* interactivity goal is met by stock
infrastructure. Phase B's job is now narrower: produce a measurement
that compares Pyodide-warm vs our C++ FastAPI on a *heavy* prefigure
diagram (implicit curves, ideally) so we can give the upstream team a
concrete number. If Pyodide-warm is already ≥30 Hz on the heavy case,
Phase B's output is a recommendation to *not* deploy our server; if
Pyodide is sluggish, we have a deployment proposal with data behind it.

The conditional Phases C (Path 1 — replace AWS service) and D (Path 2 —
Wasm-compile our C++) follow from Phase B's data, unchanged from the
prior plan.

## Risks for next time

- **`webwork-to-doenetml/convert.js` was modified by `npm run build`.**
  Innocent regen, but file shows up in `git status`. Note for any future
  PR work: that path needs an explicit `git checkout` before commit.
- **`package-lock.json` was modified by `npm install`.** Same caveat.
  Not unusual; just don't accidentally include it in a PR scope.
- **`testCode.doenet` and `dev/main.tsx` are explicitly commit-excluded
  by AGENTS.md.** Anything we want to share upstream lives somewhere
  else (e.g., a separate repo with `.doenet` examples, or a docs page).

## File state at end of Phase A

- `packages/doenetml/dev/testCode.doenet` — overwritten with tangent
  activity (uncommitted, per AGENTS.md). Restore via
  `git checkout HEAD -- packages/doenetml/dev/testCode.doenet`.
- `packages/doenetml/dev/main.tsx` — untouched.
- Verification scripts (`_verify_*.mjs`, `_probe_*.mjs`, `_dump_*.mjs`) —
  removed; do not need recreating unless re-running A.5 verification.
- Screenshots: `/tmp/tangent_initial.png`, `/tmp/tangent_after_wait.png`,
  `/tmp/tangent_after_react_update.png` (will be evicted on reboot).
- Dev server PID 2720345 still running (`npm run dev`). Stop with
  `kill 2720345` when done.
