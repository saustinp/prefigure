# prefig webdemo

Minimal end-to-end demo wiring the prefig C++ rendering backend to a browser.

```
Browser  ──HTTP──>  FastAPI (uvicorn)  ──PyBind11──>  _prefigure (C++ .so)
```

The server holds the C++ module loaded in-process. The browser sends an
HTTP request, the server renders the diagram natively, and returns the SVG;
the browser injects the SVG into the DOM.

This directory has zero dependencies on anything inside `prefig/` beyond
the public Python entry point `prefig.engine.build_from_string`. Deleting
`webdemo/` removes the demo without touching the library.

## Running it

```bash
# 1. Activate the project venv (where the _prefigure .so lives).
source /home/sam/Documents/prefigure/.venv/bin/activate

# 2. Install the demo's HTTP-only dependencies.
pip install -r webdemo/requirements.txt

# 3. Start the server. Single worker so the C++ module is loaded once.
uvicorn webdemo.server:app --host 127.0.0.1 --port 8000 --workers 1
```

Then open <http://127.0.0.1:8000/> in a browser and click the button.

The server warms the C++ backend at startup (one synchronous render of the
bundled example), so the first user click is fast.

## Endpoints

| Method | Path | Body | Returns |
|---|---|---|---|
| `GET` | `/api/health` | — | `{"status":"ok","warm":true}` |
| `GET` | `/api/examples/implicit` | — | `image/svg+xml` (rendered `examples/implicit.xml`) |
| `POST` | `/api/render` | `{"xml":"...", "environment":"pyodide"}` | `image/svg+xml` |
| `GET`  | `/` and other paths | — | static frontend (`webdemo/static/`) |

`environment` accepts `pyodide`, `pretext`, or `pf_cli`. The demo uses
`pyodide` to skip publication-file lookup and on-disk annotation writes.

## Smoke tests

With the server running:

```bash
# 1. Health probe
curl -s http://127.0.0.1:8000/api/health
#   {"status":"ok","warm":true}

# 2. Bundled example -> SVG (~120 KB)
curl -sI http://127.0.0.1:8000/api/examples/implicit | head -3
curl -s   http://127.0.0.1:8000/api/examples/implicit | wc -c

# 3. Arbitrary XML via POST
curl -s -X POST http://127.0.0.1:8000/api/render \
     -H 'content-type: application/json' \
     --data-binary "$(jq -Rs '{xml: .}' examples/implicit.xml)" \
     | wc -c
```

## Enabling CORS

The frontend is served from the same origin as the API by default, so CORS
is not needed. If you later host the frontend elsewhere, set the
`PREFIGURE_CORS_ORIGINS` environment variable to a comma-separated list of
origins before starting uvicorn:

```bash
PREFIGURE_CORS_ORIGINS=https://my-frontend.example,https://other.example \
  uvicorn webdemo.server:app --host 127.0.0.1 --port 8000 --workers 1
```

The `CORSMiddleware` is always installed in the app; with the env var
unset, its `allow_origins` list is empty and it is a no-op.

## Notes & known limitations

- **Single worker.** `_prefigure` has not been audited for thread safety.
  FastAPI's threadpool serializes blocking handlers within one process, so
  a single uvicorn worker is the safe configuration. Running with
  `--workers N > 1` will load N independent C++ modules and is fine for
  scaling, but each worker pays its own warmup cost.
- **No annotations endpoint yet.** The C++ binding now returns
  `(svg, annotations_or_None)`, but the demo discards the annotations.
  Hooking that into a future `/api/render-with-annotations` endpoint is
  trivial — `engine.build_from_string` already returns the tuple.
- **No tactile output.** The POST schema accepts `xml` and `environment`
  only; format is hard-coded to SVG. Tactile output can be added later by
  extending `RenderRequest` and `engine.build_from_string`'s first arg.
- **No request size limits.** Localhost demo only. Add a body-size cap
  before exposing this beyond a trusted network.
- **No auth.** Any client that can reach the port can invoke `POST
  /api/render` with arbitrary XML. The C++ code parses XML through pugixml
  and processes math through MathJax; treat the input surface as untrusted
  before deploying.

## File layout

```
webdemo/
├── server.py            FastAPI app
├── requirements.txt     fastapi + uvicorn[standard]
├── static/
│   └── index.html       single-page demo (HTML + inline JS)
└── README.md            this file
```
