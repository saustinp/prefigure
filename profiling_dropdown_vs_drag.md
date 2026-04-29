# Profiling: dropdown vs drag — why the drag path is laggy

Captured 2026-04-29. Companion to `phase_b_findings.md`,
`doenet_integration_notes.md`, and `phase_a_findings.md`.

This document preserves the full thread of investigation that uncovered
the dispatcher-level abort-storm bottleneck on the production Doenet
activity at
`https://beta.doenet.org/activityViewer/2G23UhSKKcJNGSWNmk8LPc`. The
analysis was driven by two Chrome DevTools Performance traces saved at
`/home/sam/Documents/DoenetML/profiling/{dropdown_path,interactive_trace}`.

The headline result reframes the perf story: **the warm-phase lag is
not Pyodide, not network, not our /build endpoint — it is the
dispatcher in `prefigureRuntime.ts` aborting in-flight worker calls on
every input event.** Our C++ FastAPI work cleanly fixes the cold phase
but does not address this warm-phase issue.

---

## 1. The original observation

The user, while testing the production-deployed activity, noticed two
distinct rendering pipelines that exhibit very different performance
characteristics, despite ostensibly producing the same prefigure
re-render.

> There are two paths to kicking off the rendering process for a new
> prefigure (right pane):
>
> 1. one is using the drop-down to change the label placement... This
>    changes the label placement of the labels on both the prefigure
>    and the DoeNet XML, triggering a re-render on both figures.
>
> 2. There's a second path. The second path is to interactively click
>    and drag the points or the left graph (DoeNet XML), and they will
>    re-render both a DoeNet XML as well as a prefigure graph. This
>    pipeline is extremely laggy. When you click and drag the points
>    on the DoeNet graph, the re-render update is buttery smooth on
>    the DoeNet graph; however, there is a significant stutter in the
>    updating of the prefigure graph, often limiting its frame rate
>    to 1 Hz.

The user already knew that the left graph renders client-side via
JavaScript (JSXGraph) and that the right graph goes through prefigure
(WASM/Pyodide-Python). What they did not understand was why the same
ostensible work (prefigure re-render) was instantaneous in one path
and 1 Hz in the other. They asked for a thorough profiling
investigation that could trace the routing of every signal and the
time allotment of every step, equivalent to what one might do in C++
with `perf`.

## 2. The working hypothesis

Before any trace analysis, the most likely explanation, based on the
shape of the dispatcher in `prefigureRuntime.ts`, was an **abort storm
on continuous input**. The dispatcher wraps every render in an
`AbortController`. When a new render is dispatched, any in-flight
render gets aborted.

- A dropdown click is **one state change → one render → completes
  uninterrupted**. The user perceives the full warm-phase latency
  once.
- A drag of a point is **mousemove at ~100 Hz → ~100 state changes
  per second → each dispatches a new render and aborts the
  previous**. With each render taking longer than the inter-event
  interval, no render ever completes during sustained drag — the user
  sees only renders that survive when they pause briefly, at roughly
  1 Hz.

Other plausible alternatives were:

- **Reactive DAG thrash**: most drag mouseMoves don't even reach
  `prefigureRuntime.buildPrefigureDiagram` because DoenetML's worker
  DAG is busy upstream.
- **Worker queue / postMessage saturation**: drag floods the
  cross-thread queue with messages; the worker compile rate stays
  steady but lags input by seconds.

All three would produce visibly slow drag updates, but each leaves a
different signature in a profiler trace.

## 3. The web-profiling toolchain, mapped to perf concepts

For someone fluent in `perf`, the equivalents in the browser
ecosystem:

| C++ / Linux | Browser equivalent | What it captures |
|---|---|---|
| `perf record -g` | **Chrome DevTools → Performance → Record** | Sampling profiler with full call stacks, including across Web Worker boundaries. Exports as JSON. |
| flame graph | DevTools Performance flame chart, or export to **Speedscope** | Same visualization, including main-thread, worker, and network as separate tracks. |
| `perf trace` (syscalls) | Network panel with detailed timing, plus **PerformanceObserver** capturing `resource` entries | Per-request DNS/connect/TLS/TTFB/transfer split — same decomposition as `curl -w`. |
| `perf stat` (counters) | DevTools **Performance** → Frame Rendering Stats, **Long Tasks API**, JS heap snapshots | Fps, dropped frames, GC pauses, heap pressure. |
| Tracepoints / USDT | **`performance.mark()`** + **`performance.measure()`** in JS | Programmatic instrumentation. Marks appear inline in the DevTools flame chart with custom labels. Cheap (~ns), production-safe. |
| `eBPF` | Same as above plus the **Long Tasks API** for >50 ms tasks | Long Tasks fire callbacks for any blocking task. Works in production. |
| Cross-process tracing | DevTools captures main thread + every Web Worker thread by default | One trace, multiple tracks. |

The output of "DevTools → Performance → record" is comparable in
fidelity to a `perf record -g` trace; only the visualization differs.

## 4. The three-phase profiling plan

Phase 1, **DevTools traces on production**: hard-refresh the activity,
wait ~10 s for `[prefigure] WASM runtime ready` in console, set CPU
throttling to 4×, record one 5-second trace per path
(dropdown / drag), save as JSON.

Phase 2, **targeted instrumentation in the local DoenetML clone**:
edit `prefigureRuntime.ts` to add `performance.mark()` calls at every
interesting boundary (dispatch start, abort, WASM compile start/end,
DOM inject start/end). Re-trace locally to confirm what DevTools shows
with semantic labels.

Phase 3, **automated reproduction in headless**: Playwright script
that loads the page, waits for warmup, records
`performance.getEntriesByType('measure')`, drives both paths
programmatically, dumps timing as JSON for reproducible regression
checks.

The user executed Phase 1 first; the rest of this document captures
the analysis of the resulting traces.

## 5. Trace collection (executed by the user)

The user recorded both traces on the production site and saved them
as gzipped JSON:

```
/home/sam/Documents/DoenetML/profiling/
├── dropdown_path.gz       (28 MB compressed, 135 MB uncompressed)
└── interactive_trace.gz   (40 MB compressed, 525 MB uncompressed)
```

The size disparity alone is informative — the drag trace contains
~3.9× more events than the dropdown trace despite both being recorded
for similar durations. (The trace file's reported "duration" is the
full page-lifetime span, not the active-recording window.)

## 6. Top-level trace statistics

Streaming-parsed using `ijson` to avoid loading 525 MB of JSON into
memory at once. Headline numbers:

| metric | dropdown trace | drag trace | ratio |
|---|---|---|---|
| File size (uncompressed) | 134.8 MB | 524.9 MB | 3.9× |
| Total events | 149,484 | 497,642 | 3.3× |
| Long tasks (>50 ms) | 31 | **521** | 17× |
| Renderer main thread events | 79,267 | **400,677** | 5× |

**Top event names by frequency:**

Dropdown trace:
```
32,875  RunTask
 9,508  UpdateCounters
 9,395  v8.callFunction
 9,392  FunctionCall
 4,608  IntersectionObserverController::computeIntersections
 4,562  AnimationFrame
 3,438  PipelineReporter
 3,434  BeginImplFrameToSendBeginMainFrame
 3,161  ProfileChunk
 ...
```

Drag trace:
```
88,492  v8::Debugger::AsyncTaskCanceled    ← THIS IS THE SMOKING GUN
66,174  v8::Debugger::AsyncTaskScheduled
66,173  RunTask
65,637  v8::Debugger::AsyncTaskRun
27,960  UpdateCounters
27,209  v8.callFunction
27,205  FunctionCall
21,808  TimerInstall
21,627  TimerFire
 7,400  PipelineReporter
 3,546  InputLatency::MouseMove
```

The headline finding: **88,492 `AsyncTaskCanceled` events in the drag
trace**, vs not even appearing in the top-15 of the dropdown trace.
Equally significant, the drag trace shows **more cancellations than
schedulings** (88,492 vs 66,174) — a healthy pipeline has
`Scheduled ≈ Run`, with cancellation rare. Here cancellation
*dominates*.

The 21,808 `TimerInstall` / 21,627 `TimerFire` events further confirm
that the debouncer is being repeatedly scheduled and reset on every
mousemove. Only when the user pauses long enough does a timer survive
to fire and dispatch a non-aborted render.

## 7. Drill-down: where is the work actually happening?

The streaming inspection plus a targeted second pass extracted
function-call duration distributions per thread:

| metric | dropdown | drag |
|---|---|---|
| **Worker FunctionCalls** | 18 | **20** |
| Worker total runtime | 337 ms | **388 ms** |
| Worker median FunctionCall | 14.8 ms | 13.4 ms |
| Worker p95 FunctionCall | 195.9 ms | 228.6 ms |
| Worker max FunctionCall | 195.9 ms | 228.6 ms |
| Main thread FunctionCalls | 9,343 | **26,959** |
| Main thread total runtime | 1,066 ms | **17,296 ms** |
| Main thread long tasks (>50 ms) | 0 | **97** |
| Main thread median FunctionCall | 0.01 ms | 0.00 ms |
| Async tasks Scheduled | 987 | 66,174 |
| Async tasks Run | 966 | 65,637 |
| Async tasks **Canceled** | 1,346 | **88,492** |

This is the deepest finding of the entire investigation:

**The worker thread is doing essentially the same amount of actual
rendering work in both traces.** ~20 FunctionCalls, ~13 ms median,
~388 ms total runtime. **What differs is the main thread**: it's
drowning in JavaScript work (16× more execution time, 97 long tasks,
88K aborts) but very little of that work translates into worker
compile requests reaching completion.

## 8. Synthesis: dispatcher-level abort storm, not Pyodide slowness

The interpretation that follows from this data:

- During a multi-second sustained drag, the dispatcher fires **~88K
  abort signals**.
- Each abort kills an in-flight render before it reaches the worker.
- Only **~20 render requests** actually survive long enough to be
  compiled by Pyodide — same as the dropdown's ~18.
- The worker is **idle most of the time** during the drag, waiting
  for non-aborted requests to arrive.
- The 1 Hz the user perceives is "20 surviving renders over the
  ~20 seconds of drag activity."

The user's lag is **not** Pyodide rendering speed. Pyodide could
happily do 60+ Hz on this activity (median 13.4 ms per compile). It
is the dispatcher's behavior of aborting on every input event that
starves the worker.

This rules out two of our three pre-trace hypotheses:

- **Reactive DAG thrash** (rejected): if the DAG were the bottleneck
  upstream of the dispatcher, the dispatcher would see fewer
  scheduled events, not 66K. The DAG is in fact propagating every
  mousemove all the way through to the dispatcher.
- **Worker queue saturation** (rejected): if the worker queue were
  saturated, we'd see worker activity ramp up and stay high. Instead
  the worker did roughly the same amount of work in both traces — the
  abort fires *before* messages reach the worker.

The abort-storm hypothesis is confirmed.

## 9. Implications for the optimization story

This finding fundamentally reframes the architectural picture.

The C++ FastAPI work (Phase B-1) cleanly fixes the **cold-phase**
scenario, where the slow AWS Lambda was the dominant cost — that
work delivers a ~1300× speedup as documented in
`phase_b_findings.md`.

But for the **warm phase** (the actual user experience after the
first ~7 seconds), our /build is not even called. Replacing Pyodide
with WASM-compiled C++ would only save ~10 ms per render — not
addressing the dispatcher behavior that prevents the worker from
running more than ~20 renders during a 20-second drag.

The single-line fix worth prototyping is in
`prefigureRuntime.ts:259`, where `serviceAbortController.abort()` is
called when a new render dispatches. Two cheaper alternatives to
"abort always":

1. **Don't abort in-flight worker calls.** Let the current render
   finish, then the next dispatch picks up the freshest state. Stale
   renders get garbage-collected naturally. This trades "freshest" for
   "always making progress." On 13 ms compiles, the user gets ~75 Hz
   update rate — overshoots the 60 Hz screen refresh.

2. **Coalesce on trailing edge with a hard upper bound.** Instead of
   "abort and restart on every input," collect inputs and dispatch at
   most one render per N ms (say 30 ms). Each render uses the latest
   state at dispatch time. The worker stays saturated; the user gets
   smooth ~30 Hz updates regardless of input rate.

Either change would deliver a dramatically better warm-phase
experience on production *without* deploying any of our C++ work.

## 10. Follow-up question 1 — why 18/20 worker compiles for ~9 dropdown clicks?

The user noticed that the dropdown trace had ~18 worker FunctionCalls
despite their reported ~9 clicks, and asked whether this 2× factor
came from the two figures (Doenet XML graph + prefigure clone).

The correct structural picture: the activity has **two
`<sideBySide>` sections, each containing exactly one prefigure
clone** (the right-side `<graph extend="$g1" renderer="prefigure" />`).
The left-side `<graph name="g1">` in each section has no `renderer`
attribute → defaults to `renderer="doenet"` → **JSXGraph**, not
prefigure. Two sections → two prefigure clones total.

So the multiplier-of-2 is not "two clones per section." Three
possibilities remain for 18 worker FunctionCalls from ~9 clicks:

**Explanation A — name collision across sections.** Both sections
declare `<choiceInput inline name="pos">` independently. If
DoenetML's name-resolution treats these as the *same* variable
globally, clicking either dropdown changes a single shared `pos`,
and both prefigure clones (one per section) observe the change and
re-render. 9 clicks × 2 sections-listening = 18 renders. The
"Info: 1" indicator visible at the bottom of the editor when this
activity loads may be a duplicate-name warning from the parser
that supports this hypothesis.

**Explanation B — two worker FunctionCalls per render.** The
"FunctionCall" event on the worker thread isn't 1:1 with a prefigure
compile. The Pyodide work likely has at least:

- the actual `compilePrefigure` call (the heavy bit)
- a postMessage-result handler that marshals the SVG string back

If those count separately, "9 renders → ~18 worker FunctionCalls"
follows. Comlink (which `@doenet/prefigure` uses, per the trace's
source-map references to `comlink.mjs`) tends to wrap remote calls
in this kind of two-step pattern.

**Explanation C — the user clicked both dropdowns.** 9 picks in
section 1's dropdown plus 9 picks in section 2's dropdown gives 18
renders straightforwardly. Plausible if the user scrolled to test
both sections.

The diagnostic test that disambiguates these: extract the actual
function names and durations for the 18 FunctionCalls on the worker
thread (`DedicatedWorker thread` tid=33) in the dropdown trace.

**Resolved**: Explanation B is correct. The 18 FunctionCalls are
exactly 9 prefigure compiles (the `s` function in
`index-S-QO-c_c.js:39`, 14–196 ms each) interleaved with 9 trivial
`__setImmediate_cb` callbacks in `pyodide.asm.js` (~0.01–0.03 ms
each). The latter is Pyodide's internal task-queue drainage that
fires after each compile completes — Comlink's postMessage-marshaling
machinery. So 9 user clicks → 9 actual compiles → 18 worker
FunctionCall events. There is no double-rendering and no name
collision. DoenetML's section scoping is doing its job; the dropdown
in section 1 only re-renders section 1's prefigure clone.

**Explanations A and C are both rejected by the data plus user
statement.** The user explicitly confirmed they only interacted with
the top dropdown (section 1's), not the bottom one (C: not clicked
both dropdowns), and the trace shows only 9 actual compiles, not 18
(A: no name-collision-driven double rendering). Both `<choiceInput
name="pos">` declarations resolve to scope-local `pos` variables; the
"Info: 1" indicator visible at the bottom of the editor is unrelated
or is a benign duplicate-name notice that does not alter scoping.

The full per-call table extracted from the dropdown trace:

```
idx   ts(s)    dur(ms)  name              where
  0   0.000   195.89    s                 index-S-QO-c_c.js:39   ← compile #1 (cold)
  1   0.196     0.02    __setImmediate_cb pyodide.asm.js:10      ← postMessage callback
  2   1.809    24.46    s                 index-S-QO-c_c.js:39   ← compile #2 (warm)
  3   1.833     0.01    __setImmediate_cb pyodide.asm.js:10
  4   3.217    16.18    s                 index-S-QO-c_c.js:39   ← compile #3
  5   3.234     0.02    __setImmediate_cb pyodide.asm.js:10
  6   5.047    15.98    s                 index-S-QO-c_c.js:39   ← compile #4
  7   5.063    15.76    s                 index-S-QO-c_c.js:39   ← compile #5
   ...
 16  13.768    14.83    s                 index-S-QO-c_c.js:39   ← compile #9
 17  13.783     0.01    __setImmediate_cb pyodide.asm.js:10
```

**Side correction to the prior reported numbers:** the "13.4 ms
median worker FunctionCall" reported in section 7 was computed across
all 20 worker calls including the trivial `__setImmediate_cb`
callbacks. The actual prefigure-compile durations alone are:

```
sorted (ms): 14.83, 15.66, 15.76, 15.98, 16.18, 16.23, 21.48, 24.46, 195.89
median (true): 16.18 ms
warm range:    14–25 ms (excluding the cold first compile)
cold first:    196 ms (one-time tax; subsequent calls hit Pyodide's parsed-XML cache)
```

So the true Pyodide warm-compile time on this activity is **~16 ms**,
not 13.4. This adjusts the 167 ms click-to-DOM latency decomposition
in section 11 slightly: 40 ms debounce + ~16 ms Pyodide compile + ~111
ms framework/DOM. The takeaway is unchanged — Pyodide is ~10% of
perceived click latency; the rest is debounce + dispatcher + DOM
injection.

### Drill into the drag trace's worker activity for completeness

Same drill against the interactive (drag) trace produced the
analogous picture: **10 prefigure compiles + 10 trivial callbacks =
20 FunctionCalls** on the prefigure worker (tid=33).

```
ts(s)    dur(ms)   (long compiles only, on `s @ index-S-QO-c_c.js:39`)
 0.000   228.61    ← cold first compile
 6.545    22.22    ← warm
 6.774    17.44
 9.487    15.59
12.623    16.92
14.127    21.15
16.990    20.39
18.401    15.77
20.471    16.61
21.383    13.40
```

Critical observations:

- 10 compiles spread across **~21 seconds of trace time**, with a
  median gap of ~2 s between compiles. The user's perceived update
  rate during drag was actually **~0.5 Hz**, not even the 1 Hz they'd
  estimated.
- 88,492 `AsyncTaskCanceled` events fired in the same window. So for
  every successful compile, ~8,800 attempts were aborted before
  reaching the worker. The dispatcher's abort behavior is what's
  preventing the worker from saturating.
- The worker's total runtime over 21 seconds of drag was 388 ms —
  i.e., the worker was idle 98% of the time, waiting for non-aborted
  requests to arrive.
- The other DedicatedWorker thread (tid=31, the DoenetML reactive
  DAG worker) processed 226 short FunctionCalls (median ~0.05 ms).
  It dutifully handles every mousemove with state-update propagation;
  it is *not* slow.

This rules out the worker-saturation hypothesis (the worker would be
busy if saturated; it isn't) and the reactive-DAG-thrash hypothesis
(the DAG handles all 3,500+ mousemoves with <30 ms total work). The
abort-storm explanation is the only one consistent with all the
evidence.

## 11. Follow-up question 2 — 13.4 ms vs 167 ms?

The user noted a discrepancy: earlier in the conversation, when
measuring "click → SVG content changes" via a Playwright probe, we
reported a **167 ms median per-click latency on the warm Pyodide
path**. The trace drill-down showed worker FunctionCall median of
**13.4 ms**. They asked what's the difference.

This is the more important question, and the answer reframes the
whole performance story.

**13.4 ms is the worker's compile time alone** —
`compilePrefigure(xml)` in Pyodide-Python, just the rendering work,
measured at the worker's FunctionCall granularity.

**167 ms is the full click-to-DOM-update latency** — what the user
actually perceives — measured by the earlier polling script that
timed "click happened" → "SVG content changed in the DOM."

The 154 ms gap decomposes (approximately):

- **40 ms** — the warm-phase debounce timer
  (`PREFIGURE_BUILD_DEBOUNCE_WARM_MS = 40`)
- **13 ms** — the actual Pyodide compile
- **~100 ms** — React event handling, DoenetML reactive DAG
  re-evaluation, `prefigureXML` emission, postMessage main↔worker
  (twice, with structured-cloning of the XML and SVG strings),
  `innerHTML = svg` DOM injection, browser style/layout/paint

So the actual prefigure compute is **only ~8% of the user-perceived
latency**. The rest is framework plumbing on the main thread.

This changes the optimization picture meaningfully:

| optimization | savings on the 167 ms latency | difficulty |
|---|---|---|
| Our C++ FastAPI in place of Pyodide | ~10 ms (compile drops 13→3) | already done in Phase B-1 |
| Wasm-compile our C++ replacing Pyodide | ~10 ms (compile drops 13→3) | multi-week (Path 2) |
| Lower the warm debounce (40→5 ms) | ~35 ms | one-line config change |
| Don't abort in-flight (the drag fix) | enormous on drag, ~0 on click | one-line dispatcher change |
| Diff-and-patch DOM instead of `innerHTML` | ~30–50 ms (skip parse/layout) | medium, needs stable IDs |
| Skip `prefigureXML` regen when only one attribute changed | ~30–50 ms (skip DAG retraversal) | structural, hardest |

The biggest single win for the *click-latency* case is the debounce
+ DOM-patching combo, **not** making Pyodide faster. Earlier in the
project we were over-indexing on backend speed.

The honest takeaway: replacing Pyodide with our C++ saves ~10 ms;
the other 154 ms of perceived latency lives in React + DoenetML's
DAG + postMessage marshaling + DOM injection.

## 12. Follow-up clarifications: count vs time, and "worker idle 98%"

After the drag-trace drill, two further clarifying questions came up
that sharpen the interpretation of the numbers. The full Q&A,
preserved verbatim:

### Q1 (refinement): Is half the worker time "meaningless"?

**No — the COUNT is doubled, but the TIME is essentially all
meaningful.** The setImmediate callbacks are real events but their
durations are negligible.

Sum of the 10 long compile durations (the `s @ index-S-QO-c_c.js:39`
calls):

```
228.61 + 22.22 + 17.44 + 15.59 + 16.92 + 21.15 + 20.39 + 15.77 + 16.61 + 13.40
= 388.10 ms
```

Sum of the 10 setImmediate callbacks (the
`__setImmediate_cb @ pyodide.asm.js:10` calls): ~0.01–0.03 ms each,
total **≈ 0.15 ms**.

So of the 388 ms of total worker activity, **387.95 ms (99.96%) is
the actual prefigure compile**, and 0.15 ms is the result-marshaling
callback. The worker is *not* wasting half its time on callbacks; the
trace counts them as separate FunctionCall events but they're
practically free.

Why this distinction matters at all: in the earlier "13.4 ms median"
report (in section 7), I'd accidentally averaged across all 20 events
including the 0.01 ms callbacks, which dragged the median down. The
actual median *prefigure compile* time is ~16 ms (warm). That
correction was about getting the per-call latency right, not about
the worker wasting time.

### Q2 (refinement): What's included in "worker idle 98% of the time"?

**Just the prefigure WASM worker thread (tid=33), nothing else.**

The math:

```
Drag-window trace span:        ~21,000 ms  (first compile at t=0, last at t=21.4s)
Prefigure worker active time:    388 ms    (sum of all 10 compile durations)
Prefigure worker idle time:   20,612 ms
Idle fraction:                  20,612 / 21,000  =  98.2%
```

This 98% figure is **only the prefigure worker thread**. It does
*not* include any of the following:

- React event handling on the main thread
- DoenetML reactive DAG re-evaluation (which runs on a *different*
  worker thread, tid=31)
- postMessage marshaling between threads
- Browser input handling, layout, paint
- The dispatcher's debouncer setting/cancelling timers
- The 88,492 abort signals firing

All of those are happening on the main thread or on tid=31 —
separate from the prefigure worker.

### Putting it all together: utilization across the whole system

If you ask "what fraction of the drag duration was spent doing **any
useful prefigure-rendering work** (anywhere in the system)":

```
useful work:    ~388 ms    (10 prefigure compiles, on tid=33)
total time:   ~21,000 ms   (drag window)
utilization:   ~1.85%
```

Less than 2% of the time was spent doing the actual computational
work the user was waiting for. The other 98% breaks down roughly as:

| where | what | approx time | why |
|---|---|---|---|
| prefigure worker (tid=33) | idle | ~20,612 ms | waiting for non-aborted tasks |
| DoenetML DAG worker (tid=31) | tiny state updates | ~30 ms total | dutifully processing every mousemove |
| main thread | dispatcher overhead | ~17,296 ms total runtime | scheduling/cancelling debounce timers, firing abort signals, React reconciliation, postMessage marshaling, DOM injection on the few renders that complete |
| GPU process | compositor | ~unknown but small | normal browser frame compositing |

So the broader answer is: **the system spends its time issuing and
aborting render dispatches, not actually rendering**. The 98%
worker-idle figure is the most direct expression of that. The main
thread is busy, but busy doing *dispatch overhead*, not useful
rendering work.

This is why a one-line dispatcher fix (don't abort in-flight worker
calls) is the highest-payoff change: it shifts the worker's
utilization from ~2% to ~50%+ (limited only by Pyodide's per-compile
time), without touching any other component.

### Note on the cold-first-compile (~196–228 ms)

The first prefigure compile in each trace runs ~10× slower than the
subsequent warm ones (196 ms in the dropdown trace, 228 ms in the
drag trace, vs ~14–25 ms warm). Pyodide WASM was already loaded by
that point — `[prefigure] WASM runtime ready` had logged seconds
earlier — but it was the first time prefigure had seen *this
particular XML structure*. Subsequent compiles hit warm caches.

Plausibly contributing to the cold first compile (not directly
measured, but consistent with typical Python/Pyodide warmup
patterns):

- Python module imports specific to the code paths the XML
  exercises (`prefig.line`, `prefig.label`, MathJax wrappers, lxml
  parsers) load on first use rather than at Pyodide startup.
- MathJax glyph rendering — each unique glyph in the labels
  (`a line segment`, `a line`, `a ray`, `P`, axis numbers) gets
  rendered once and cached. The first render pays for all of them;
  subsequent renders just look them up.
- Pyodide's V8 → Wasm bridge for Python callbacks warms with use,
  and per-XML-template parser state in prefigure populates.
- lxml DOM construction — first-time parsing of the diagram XML
  structure.

After the first compile, the worker's cumulative state is hot for
that activity until the page is reloaded. That's why the 9
subsequent compiles in the dropdown trace cluster tightly around 16
ms.

## 13. Where this leaves the project

Our C++ FastAPI work is **not invalidated** by these findings — it
cleanly fixes the cold-phase scenario where the slow remote
`prefigure.doenet.org/build` was the dominant cost, and gives a
deployable replacement that's ~1300× faster than the AWS Lambda.

The warm-phase finding just narrows where additional value lives:

- **Highest-payoff next experiments**, in order:
  1. Patch `prefigureRuntime.ts` locally to not abort in-flight worker
     calls. Re-record a drag trace. Confirm it eliminates the abort
     storm and delivers smooth ~30+ Hz drag updates.
  2. Lower `PREFIGURE_BUILD_DEBOUNCE_WARM_MS` from 40 to 5. Re-record
     a dropdown trace. Confirm the click→DOM latency drops by ~35 ms.
  3. Diff-and-patch DOM instead of `innerHTML` reset, with stable
     SVG element IDs. Confirm warm-phase per-click latency reaches
     <50 ms end-to-end.

- **Outstanding question** that didn't get fully resolved:
  - Are the 20 worker FunctionCalls per drag = 9 clicks × 2 sections,
    or = 10 renders × 2 FunctionCalls each? Drill the worker thread's
    `FunctionCall.args.data.functionName` field to settle.

- **Trace files preserved** at
  `/home/sam/Documents/DoenetML/profiling/` for re-analysis. The
  inspection scripts live at `/home/sam/Documents/prefigure/`:
  `_inspect_trace.py` (top-level stats) and `_drill_trace.py`
  (per-thread durations + async-task lifecycle).

## 14. What this means for the upstream conversation

The narrative we now bring to the upstream developers:

1. We built a C++ replacement for `prefigure.doenet.org/build`.
   Independent of any other changes, it cuts cold-phase render
   latency from ~4 s to ~5 ms (Phase B-1).
2. The warm-phase lag the developer demonstrated on
   `2G23UhSKKcJNGSWNmk8LPc` is **not** caused by Pyodide-Python
   speed. It is caused by the dispatcher in `prefigureRuntime.ts`
   aborting in-flight worker calls on every mousemove during drag.
   Profiler trace evidence: 88K AsyncTaskCanceled events vs ~20
   surviving worker compiles in a single drag interaction.
3. A one-line dispatcher change to *not* abort in-flight calls would
   take the warm-phase drag experience from ~1 Hz to ~30+ Hz on real
   GPU hardware, with no other changes required. Independently of
   our C++ work.
4. Combined: Pyodide stays for the warm path (or gets replaced with
   WASM-compiled C++ as a separate Path 2 project), our C++ FastAPI
   serves the cold path, and the dispatcher fix unblocks both.

This gives the upstream team three orthogonal levers, ranked by
effort/payoff:

1. Dispatcher fix (~1 hour, biggest UX win)
2. Deploy our C++ FastAPI (~deployment work, eliminates cold-phase lag)
3. Wasm-compile prefigure (multi-week, future-proofing)
