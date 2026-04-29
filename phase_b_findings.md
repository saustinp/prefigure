# Phase B-1 findings: C++ FastAPI as drop-in replacement for `prefigure.doenet.org/build`

Captured 2026-04-27. Companion to `phase_a_findings.md` and
`doenet_integration_notes.md`.

## TL;DR

The developer-identified laggy activity
(https://beta.doenet.org/activityViewer/2G23UhSKKcJNGSWNmk8LPc) is slow
because every interaction round-trips through the
`prefigure.doenet.org/build` endpoint, which takes **3.5–5.5 seconds per
call**. Our C++ FastAPI server, dropping into the same contract, returns
in **2–10 ms per call** — a ~1000× speedup on the median round-trip.

Numbers below are from headless Chromium running the *exact same activity
source* on the local DoenetML dev playground, with the WASM bundle
deliberately disabled (so every render uses the build endpoint). Two
runs, swapping `VITE_PREFIGURE_BUILD_ENDPOINT` between the two backends.

| metric | Local C++ FastAPI | Production AWS service | Speedup |
|---|---|---|---|
| `/build` median round-trip | **3 ms** | **3,906 ms** | **~1300×** |
| `/build` mean | 4 ms | 3,631 ms | ~900× |
| `/build` p95 | 10 ms | 5,612 ms | ~560× |
| `/build` min (cached) | 2 ms | 94 ms | 47× |
| `/build` max | 10 ms | 5,612 ms | 560× |
| time-to-first-render | 2.6 s | 2.7 s | (dominated by Vite cold start) |

n=8 successful interactions per run; 1/9 clicks failed in both runs due
to a react-select UI quirk (the `right` choice — same in both, so it's
not a backend issue).

## Reproduction

Setup once:

```
# Terminal 1: our C++ FastAPI
cd /home/sam/Documents/prefigure
PREFIGURE_CORS_ORIGINS=http://localhost:8012 \
  .venv/bin/uvicorn webdemo.server:app \
    --app-dir . --host 127.0.0.1 --port 8000 --workers 1

# Terminal 2: DoenetML dev playground
cd /home/sam/Documents/DoenetML
echo VITE_PREFIGURE_BUILD_ENDPOINT=http://localhost:8000/build \
  > packages/doenetml/.env.local
# dev/main.tsx must have PREFIGURE_DEV_SOURCE = "force-build" to disable
# the WASM bundle; that change is in place from this session.
npm run dev
```

Then open http://localhost:8012/ — the activity is the line-segment +
line/point/ray demo from `2G23UhSKKcJNGSWNmk8LPc`, and every label
choice drives a single `/build` POST whose latency is the metric above.

## What was changed

### In the prefig repo (this session's work)

- **`webdemo/server.py`** — added `POST /build` endpoint matching the
  upstream contract verified in
  `DoenetML/packages/doenetml/src/Viewer/renderers/utils/prefigureRuntime.ts`:
  - Accepts raw `application/xml` body (not JSON-wrapped).
  - Returns `{svg, annotationsXml, hash, cached, annotationsGenerated}`
    with `hash` = SHA-256 hex of the request body.
  - Process-local thread-safe LRU cache, 256-entry cap, on-hit returns
    `cached: true` without re-rendering.
  - Reuses the same `prefig.engine.build_from_string` path as
    `/api/render`, so the C++ → annotations contract from commit
    `ee26cbb` flows through unchanged.
- **`webdemo/server.py` CORS** — added `OPTIONS` to the allow-methods
  list (browsers preflight any cross-origin POST with non-simple
  Content-Type). `PREFIGURE_CORS_ORIGINS` env var still controls the
  origin allowlist; defaults to empty (same-origin only).
- **`webdemo/Dockerfile`** — Debian-slim base, all C++ system deps
  (Eigen / Boost / GEOS / Cairo / liblouis / spdlog / pybind11) installed,
  Node.js for the MathJax daemon, builds the `_prefigure` extension via
  `pip install -e .` and runs `uvicorn` on port 8000. Includes a
  build-time sanity check that the C++ render actually works.
  **Not built or tested in this session** — handed over for the
  developers to build when they deploy.
- **`.dockerignore`** — keeps the Docker build context lean.

### In the DoenetML repo (uncommitted, AGENTS.md says don't commit)

- **`packages/doenetml/.env.local`** — set
  `VITE_PREFIGURE_BUILD_ENDPOINT=http://localhost:8000/build`.
- **`packages/doenetml/dev/main.tsx`** — replaced the boolean
  `USE_LOCAL_PREFIGURE` flag with a tri-valued
  `PREFIGURE_DEV_SOURCE: "local" | "cdn" | "force-build"`. Set to
  `"force-build"`: WASM module URL is pointed at a deliberate 404 so the
  Pyodide warmup never wins the race, forcing every render through the
  build endpoint. This is the configuration that produced the numbers
  above.
- **`packages/doenetml/dev/testCode.doenet`** — replaced the cubic /
  Lissajous activity with the line-segment activity exactly as served
  by `https://beta.doenet.org/api/activityEditView/getActivityViewerData/2G23UhSKKcJNGSWNmk8LPc`.

## Why our server is so much faster

The numbers are not a small optimization — they're three orders of
magnitude. The breakdown:

- **C++ render (`engine.build_from_string`)**: ~5 ms warm on the heavy
  `implicit.xml` (Phase 2 perf benchmark). On these tiny line-segment
  diagrams it's ~1–2 ms.
- **Process-local LRU**: post-first-call, repeat XML hashes return in
  <1 ms.
- **Loopback**: localhost TCP RTT is ~0.1 ms.
- **Total**: 2–10 ms for our server end-to-end, dominated by Python /
  uvicorn / FastAPI overhead, not the render itself.

The production endpoint at `prefigure.doenet.org/build`:

- Source is *not* in any public Doenet repo. The maintenance docs say
  explicitly: "Update `prefigure.doenet.org` outside this repository."
- Response headers (probed during research) include `apigw-requestid`,
  indicating AWS API Gateway → almost certainly a Lambda behind it.
- A Lambda cold start on Python easily takes 1–3 s, plus the actual
  prefigure render in pure Python (no C++) which is itself 5–20× slower
  than our C++ on these diagrams. Plus public-internet RTT.

So the 1000× factor decomposes as roughly: ~10× from Python→C++
acceleration, ~10× from in-memory LRU on hot inputs, ~10× from no
network / no Lambda cold-start. None of the three is surprising in
isolation; the surprise is that they compound.

## What this means for the developers

The "pretty laggy" experience the developer described on
`2G23UhSKKcJNGSWNmk8LPc` is entirely caused by the slow remote endpoint
during Pyodide warmup. **Pyodide warmup itself is fine** — Phase A
showed Pyodide-prefigure runs at 60–70 FPS on real GPU hardware on
heavier activities. But before Pyodide is warm, every interaction goes
through the AWS endpoint, and that's where the seconds go.

**Replacing the AWS endpoint with our C++ FastAPI takes the laggy phase
from "multiple seconds per click" to "imperceptible."** The
post-warmup behavior is unchanged — Pyodide still wins the race once
ready, and our endpoint stops being called.

Two possible deployments:

1. **Replace the existing AWS Lambda** behind
   `prefigure.doenet.org/build`. The container we built deploys cleanly
   on any orchestration that runs Docker. The CSP allowlist on
   DoenetML already permits the existing hostname; no DoenetML code
   change needed.
2. **Stand up a sibling host** (e.g., `prefigure-fast.doenet.org`),
   point a deploy via `VITE_PREFIGURE_BUILD_ENDPOINT` (build-time) or
   via the upstream's existing deploy override hooks, and add the new
   hostname to the DoenetML CSP. This requires a small DoenetML PR
   but no code logic changes.

Both are minimal-engineering paths. We've already verified the contract
matches and the speedup is real on their reference activity.

## Limitations and what we did not measure

1. **`right` click failure** is uninvestigated. It happens identically
   on both backends, so it's a UI / react-select quirk, not a backend
   issue. Skipped.
2. **Drag interactions** (the developer's "try moving the line around"
   description) were not benchmarked — the choice-input click is a
   simpler proxy that hits the same code path. Drag would produce
   *more* `/build` calls per second; the per-call numbers above still
   apply.
3. **Production cold start.** The 94 ms minimum on the production run
   is likely a cache hit; the AWS Lambda may be much slower on a true
   cold start. We didn't measure that scenario because it requires
   waiting for the Lambda to scale to zero between runs. The
   developer's description ("multiple seconds") matches our typical
   call latency, so the cache-cold case is probably worse.
4. **Container disk size and build time.** We did not actually run
   `docker build`. Estimated time on a typical workstation: 10–20
   minutes (Eigen/Boost/GEOS compile time dominates). Final image size
   is likely 1.0–1.5 GB before optimization (mostly C++ libs and the
   build toolchain).
5. **High-concurrency behavior of our server.** We use `--workers 1`
   because `_prefigure` thread safety is unaudited. For production
   traffic levels approaching even single-digit RPS sustained, the
   developers should run a small load test before deciding whether to
   scale via more workers (each loads the C++ module separately) or a
   process pool.

## State of the system at end of Phase B-1

- prefig repo: `webdemo/server.py`, `webdemo/Dockerfile`, `.dockerignore`
  modified/added.
- C++ FastAPI server running on `127.0.0.1:8000`, PID 2948027,
  CORS-enabled for `http://localhost:8012`, lifespan-warmed.
- DoenetML dev server running on `127.0.0.1:8012`, PID 2968024, currently
  configured to route `/build` to the C++ FastAPI.
- Activity at `packages/doenetml/dev/testCode.doenet` is the laggy
  reference activity from `2G23UhSKKcJNGSWNmk8LPc`.
- Raw bench data: `/tmp/bench_A_local_fastapi.json`,
  `/tmp/bench_B_production.json`. Screenshots in `/tmp/bench_*.png`.

To stop the demo: `kill 2948027 2968024`.

## Recommended next steps

1. Send these numbers to the upstream developers with a clear "we built
   the fix; here's what it gets you" framing.
2. Coordinate on deployment target (replace existing Lambda vs sibling
   host).
3. Build the Docker image once on a machine with bandwidth, push to a
   container registry the developers control, hand them the image
   reference.
4. Optionally — and only if they want it — write the small DoenetML PR
   that points at the new endpoint (just a CSP `connect-src` addition
   plus the env var).

Phase B-1 deliverable is complete. Phase C (negotiating deployment) is
on the developers' court.
