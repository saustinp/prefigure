# DOEnet integration: research findings and architectural map

Captured 2026-04-27. Branch `cpp-packaging`. Companion file to
`realtime_pipeline_notes.md`. **Read this before resuming the integration
work** — it changes the framing of everything we discussed before.

## Headline finding

**Interactive PreFigure on DOEnet already exists and ships in production.**
The integration was designed and built upstream. We are not pioneering it.

- `@doenet/prefigure` v0.5.15 is published on npm (AGPL-3.0). It runs
  **Python prefigure in the browser via Pyodide + a Web Worker.**
- `packages/doenetml/src/Viewer/renderers/prefigure.tsx` is the React
  renderer that consumes a `prefigureXML` state variable and emits SVG
  (with diagcess for accessibility).
- `packages/doenetml-worker-javascript/src/utils/prefigure/` converts a
  DoenetML `<graph>` and its descendants (`<point>`, `<line>`,
  `<vector>`, `<curve>`, etc.) into prefigure XML, and re-emits that XML
  on every reactive DAG update.
- A slider that controls a tangent point already works in this
  architecture without any C++ in the loop. DoenetML's reactive DAG
  re-emits XML on slider drag → `prefigureRuntime.ts` debounces 40 ms
  warm → Pyodide recompiles → `<svg>` updates.

**Implication:** "modify a static prefigure example figure to be
interactive" does not require our FastAPI server, our C++ port, or any
new infrastructure. It requires authoring a DoenetML activity that wires
a `<slider>` to a graph variable. Our C++ work has a place but it is a
*separate, larger-scope* contribution: making the existing render path
faster.

## The two-backend system

DoenetML already has two prefigure render backends; they race on every
render until WASM warms up.

| Backend | Where it runs | When it's used | Source |
|---|---|---|---|
| Pyodide WASM (`@doenet/prefigure`) | Browser | Once warm, primary path (40 ms debounce) | Public, in DoenetML monorepo |
| Build service: `https://prefigure.doenet.org/build` | AWS API Gateway → ??? | Used during Pyodide warmup; loses the race once WASM is warm | **Source NOT in any public Doenet repo** |

### Build-service contract (verified by reading `prefigureRuntime.ts` and probing the live endpoint)

```
POST https://prefigure.doenet.org/build
Content-Type: application/xml
Body: <diagram>...</diagram>          (raw XML, not JSON-wrapped)

Response 200 application/json:
{ "svg": "<svg.../>",
  "annotationsXml": null,             (or string)
  "hash": "ee813f3d...",              (cache key)
  "cached": false,
  "annotationsGenerated": false }
```

The CSP allowlist in DoenetML hard-codes `connect-src
https://prefigure.doenet.org`, so a third-party endpoint requires a CSP
change.

### Race orchestration

From `packages/doenetml/src/Viewer/renderers/utils/prefigureRuntime.ts`:
the renderer fires *both* the service POST and a Pyodide warmup
simultaneously and uses `firstSuccessful([…])`. Once WASM wins a race
the in-flight POST is aborted and from then on the local path is used.
Debounce shifts 1000 ms cold → 40 ms warm
(`PREFIGURE_BUILD_DEBOUNCE_COLD_MS` / `_WARM_MS`).

### Configuration knobs

- `VITE_PREFIGURE_BUILD_ENDPOINT` — env var read by `prefigureConfig.ts`
- `VITE_PREFIGURE_MODULE_URL`, `VITE_PREFIGURE_INDEX_URL` — env vars
- `globalThis.__DOENET_PREFIGURE_MODULE_URL__` /
  `__DOENET_PREFIGURE_INDEX_URL__` — runtime overrides for the WASM bundle
  (no global override hook for the build endpoint — env var only)
- `packages/doenetml/dev/main.tsx`: a `USE_LOCAL_PREFIGURE = true` flag
  points the playground at the locally-built `@doenet/prefigure`

## Three integration paths for our C++ FastAPI

### Path 1 — Replace the AWS build service (server-side hot swap)
- Adapt our FastAPI to accept `POST application/xml` and return the
  full 5-key response. Add content-hash cache.
- Doenet team would point their API Gateway at our container.
- **Pros:** No DoenetML code change.
- **Cons:** Build service is only used during Pyodide warmup. Does not
  affect warm slider drags.

### Path 2 — Replace the Pyodide WASM bundle
- Compile our C++ via Emscripten and publish a new `@doenet/prefigure`
  (or sibling package). DoenetML bumps the dep.
- **Pros:** Replaces the slow Pyodide-Python path on the warm path —
  exactly where slider work happens.
- **Cons:** Heavy. C++ deps include Eigen, Boost, GEOS, exprtk, pugixml.
  GEOS is the hard one. `prefigure-cpp/src/label.cpp:129` says "Pyodide
  is not applicable for the C++ port" — that comment was written
  knowing this Wasm path was open. Reversing it is real engineering.

### Path 3 — Local FastAPI for prototyping / measurement
- Set `VITE_PREFIGURE_BUILD_ENDPOINT=http://localhost:8000/build` in
  the DoenetML dev playground; disable WASM (`USE_LOCAL_PREFIGURE =
  false` plus a 404 module URL) to force the service path on every render.
- **Pros:** Lets us measure C++ vs Pyodide on identical inputs.
- **Cons:** Doesn't ship anything to users by itself.

## Recommended plan (corrected from earlier framing)

The plan visits **all three paths in sequence**, not just one:

| Phase | Purpose | Path |
|---|---|---|
| **A** | Author an interactive activity using the existing Pyodide path. Prove the slider→tangent goal with zero new infra. | **None** (uses stock infrastructure) |
| **B** | Wire our FastAPI into the local DoenetML playground. Measure C++ via HTTP vs Pyodide warm on the same activity. | **Path 3** |
| **C** | If B's data justifies it: propose deploying our FastAPI as the replacement for `prefigure.doenet.org/build`. | **Path 1** |
| **D** | Stretch only if HTTP RTT dominates the C++ speedup in B: compile C++ to Wasm. | **Path 2** |

## Open question, answered

**"Surface-level only, or DoenetTools backend changes?"**

Neither in the strict sense.

- For interactive-figure goal alone: **author a DoenetML activity. No
  prefigure changes, no Doenet backend changes.**
- For C++ perf goal: changes target the **deployed service at
  `prefigure.doenet.org/build`** (AWS-hosted, source not in any public
  Doenet repo). We deliver a container; they deploy. Optionally (Path 2)
  we'd touch `packages/prefigure/` inside the DoenetML monorepo (a
  DoenetML PR, not a DoenetTools PR).
- DoenetTools (the website app) is **not in the picture for any path**.
  It just embeds DoenetML.

## Local development setup

DoenetML repo (lighter, sufficient for our work):
- Node 24 + Rust required; no MySQL, no Docker
- `npm install && npm run build && npm run dev` from repo root
- Playground at `http://localhost:8012`
- Edit `packages/doenetml/dev/testCode.doenet` to write activities

DoenetTools repo (heavier, NOT needed for our work):
- Node 24, Docker for MySQL via `docker-compose.yml`, Prisma migrations
- `npm install && npm run setup`, `docker compose ... up -d`,
  `npm run db:setup`, `npm run dev` → API:3000 + SPA:8000 + Astro:4321

## Key file paths in DoenetML monorepo

- `packages/doenetml/src/Viewer/renderers/prefigure.tsx`
- `packages/doenetml/src/Viewer/renderers/utils/prefigureRuntime.ts`
- `packages/doenetml/src/Viewer/renderers/utils/prefigureConfig.ts`
- `packages/doenetml/dev/main.tsx`
- `packages/doenetml/dev/testCode.doenet`
- `packages/doenetml-worker-javascript/src/utils/prefigure/`
- `packages/doenetml-worker-javascript/src/utils/prefigure/README.md`
- `packages/prefigure/` (the npm `@doenet/prefigure` 0.5.15 package — the Pyodide bundle)
- `packages/test-cypress/cypress/support/prefigure.js` (intercept pattern: `**/build`)

## What I could not verify (record kept honest)

- Source of `prefigure.doenet.org/build` server. AWS API Gateway response
  headers confirmed but no public source. Maintenance README says
  explicitly: "Update `prefigure.doenet.org` outside this repository."
- Whether DoenetML has a generic `<iframe>` / `<externalContent>` element
  (didn't surface in the README excerpts, not exhaustively searched).
- Real-world performance of Pyodide vs C++ on heavy diagrams (implicit
  curves, etc.) — that's what Phase B will measure.

## References

- Doenet GitHub org: https://github.com/Doenet
- DoenetML repo: https://github.com/Doenet/DoenetML
- DoenetTools repo: https://github.com/Doenet/DoenetTools
- npm @doenet/prefigure: https://www.npmjs.com/package/@doenet/prefigure
- Beta site: https://beta.doenet.org
- Get-involved page (TLS-blocked from WebFetch in this session):
  https://beta.doenet.org/get-involved
- Build service host: `https://prefigure.doenet.org/build`
