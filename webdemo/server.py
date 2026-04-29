"""FastAPI server exposing the prefig C++ rendering pipeline to a browser.

Architecture:

    Browser  --HTTP-->  FastAPI (this module)  --PyBind11-->  _prefigure (C++)

The server holds the C++ module loaded in-process and serves diagram renders
via three HTTP endpoints:

    GET  /api/examples/implicit   - renders the bundled implicit.xml example.
                                    Convenience entry used by the demo button.
    POST /api/render              - renders arbitrary PreFigure XML supplied
                                    in a JSON body.  The general case for our
                                    own webdemo.
    POST /build                   - drop-in replacement for the upstream
                                    `prefigure.doenet.org/build` endpoint that
                                    DoenetML calls during Pyodide warmup.
                                    Accepts raw application/xml, returns the
                                    five-key JSON shape the upstream renderer
                                    expects: {svg, annotationsXml, hash,
                                    cached, annotationsGenerated}.

Same-origin static frontend is mounted at "/" so a single uvicorn process
serves both the HTML and the API.

Run from the repo root with the project venv active:

    uvicorn webdemo.server:app --host 127.0.0.1 --port 8000 --workers 1

CORS:
    By default, no cross-origin allowance is configured (same-origin only,
    which is what the bundled demo uses).  To enable cross-origin requests
    later, set PREFIGURE_CORS_ORIGINS to a comma-separated list of origins,
    e.g.  PREFIGURE_CORS_ORIGINS=https://my-frontend.example,https://other.example

    For DoenetML local-dev integration (the dev playground at :8012 calling
    our :8000), set PREFIGURE_CORS_ORIGINS=http://localhost:8012
"""

from __future__ import annotations

import hashlib
import logging
import os
import threading
from collections import OrderedDict
from contextlib import asynccontextmanager
from pathlib import Path
from typing import Any

from fastapi import FastAPI, HTTPException, Request
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import JSONResponse, Response
from fastapi.staticfiles import StaticFiles
from pydantic import BaseModel, Field

from prefig import engine

log = logging.getLogger("webdemo")
logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(name)s: %(message)s")

# Repo paths --------------------------------------------------------------
REPO_ROOT = Path(__file__).resolve().parent.parent
EXAMPLES_DIR = REPO_ROOT / "examples"
IMPLICIT_XML_PATH = EXAMPLES_DIR / "implicit.xml"
STATIC_DIR = Path(__file__).resolve().parent / "static"

# Module-level cache loaded once at startup -------------------------------
_implicit_xml_text: str = ""

# Per-XML LRU cache for the /build endpoint.  Keyed on SHA-256 of the request
# body.  Process-local; sized for ~256 distinct activities, which covers any
# realistic dev-playground workflow without unbounded growth.  Thread-safe
# because uvicorn's threadpool may dispatch concurrent /build calls while the
# C++ render is in flight.
_BUILD_CACHE_MAX = 256
_build_cache: "OrderedDict[str, dict[str, Any]]" = OrderedDict()
_build_cache_lock = threading.Lock()


def _cache_get(h: str) -> dict[str, Any] | None:
    with _build_cache_lock:
        if h in _build_cache:
            _build_cache.move_to_end(h)
            return _build_cache[h]
    return None


def _cache_put(h: str, val: dict[str, Any]) -> None:
    with _build_cache_lock:
        _build_cache[h] = val
        _build_cache.move_to_end(h)
        while len(_build_cache) > _BUILD_CACHE_MAX:
            _build_cache.popitem(last=False)


def _parse_cors_origins() -> list[str]:
    """Read PREFIGURE_CORS_ORIGINS env var into a list of origins.

    Empty / unset returns []. Values are comma-separated; whitespace stripped.
    """
    raw = os.environ.get("PREFIGURE_CORS_ORIGINS", "").strip()
    if not raw:
        return []
    return [o.strip() for o in raw.split(",") if o.strip()]


@asynccontextmanager
async def lifespan(app: FastAPI):
    """Warm the C++ module and prime any caches before the first request."""
    global _implicit_xml_text

    if not IMPLICIT_XML_PATH.is_file():
        raise RuntimeError(
            f"bundled example missing: {IMPLICIT_XML_PATH}. "
            f"webdemo expects it at examples/implicit.xml relative to the repo root."
        )
    _implicit_xml_text = IMPLICIT_XML_PATH.read_text(encoding="utf-8")

    # Trigger one render so the C++ .so loads, exprtk parser cache primes,
    # and the MathJax daemon spawns.  The user's first click then bypasses
    # all cold-start cost.
    log.info("warming C++ backend with implicit.xml render...")
    svg, _ = engine.build_from_string("svg", _implicit_xml_text, environment="pyodide")
    if not svg:
        log.warning("warmup render returned empty SVG; check backend health")
    else:
        log.info("warmup ok, %d bytes svg", len(svg))

    yield
    # No teardown needed; daemon exits via its own atexit hook.


app = FastAPI(
    title="prefig webdemo",
    description="Browser-callable HTTP entry points for the prefig C++ renderer.",
    lifespan=lifespan,
)

# CORS hook ---------------------------------------------------------------
# Always installed but defaults to an empty allowed_origins list, which makes
# it a no-op for same-origin traffic.  Enabled by setting the env var.
_cors_origins = _parse_cors_origins()
app.add_middleware(
    CORSMiddleware,
    allow_origins=_cors_origins,
    allow_credentials=False,
    # OPTIONS is needed for CORS preflight; browser sends it before any
    # cross-origin POST with a non-simple Content-Type.
    allow_methods=["GET", "POST", "OPTIONS"],
    allow_headers=["Content-Type"],
)
if _cors_origins:
    log.info("CORS enabled for origins: %s", _cors_origins)


# Schemas -----------------------------------------------------------------
class RenderRequest(BaseModel):
    xml: str = Field(..., description="PreFigure XML source containing a <diagram>.")
    environment: str = Field(
        default="pyodide",
        description="Backend environment hint: pyodide | pretext | pf_cli.",
    )


# Routes ------------------------------------------------------------------
@app.get("/api/health")
def health():
    """Liveness probe. Returns ok when the C++ module is loaded and warm."""
    return {"status": "ok", "warm": bool(_implicit_xml_text)}


@app.get("/api/examples/implicit")
def render_implicit():
    """Render the bundled implicit.xml example. Powers the demo button."""
    svg, _ = engine.build_from_string(
        "svg", _implicit_xml_text, environment="pyodide"
    )
    if not svg:
        raise HTTPException(status_code=500, detail="render returned empty SVG")
    return Response(content=svg, media_type="image/svg+xml")


@app.post("/api/render")
def render(req: RenderRequest):
    """Render arbitrary PreFigure XML. Returns SVG bytes."""
    if req.environment not in ("pyodide", "pretext", "pf_cli"):
        raise HTTPException(status_code=400, detail="invalid environment")
    if not req.xml.strip():
        raise HTTPException(status_code=400, detail="empty xml")

    svg, _ = engine.build_from_string(
        "svg", req.xml, environment=req.environment
    )
    if not svg:
        raise HTTPException(
            status_code=400,
            detail="render produced empty SVG (check XML for a valid <diagram>)",
        )
    return Response(content=svg, media_type="image/svg+xml")


# Drop-in replacement for the upstream `prefigure.doenet.org/build` endpoint.
# Contract verified by reading
# DoenetML/packages/doenetml/src/Viewer/renderers/utils/prefigureRuntime.ts
# (function `buildWithPrefigureService`) and probing the live AWS endpoint.
@app.post("/build")
async def build(request: Request) -> JSONResponse:
    """Render PreFigure XML matching the upstream service contract.

    Request:
        Content-Type: application/xml (or */*)
        Body: raw XML containing a <diagram> element.  Not JSON-wrapped.

    Response 200 application/json:
        {
            "svg": "<svg.../>",
            "annotationsXml": null | string,
            "hash": "<sha-256 hex>",
            "cached": false | true,
            "annotationsGenerated": false | true
        }

    On parse failure or empty body: 400.  On render failure: 502 with detail.
    """
    body: bytes = await request.body()
    if not body:
        raise HTTPException(status_code=400, detail="empty body")

    # Hash the raw bytes (stable regardless of decode behavior).
    h = hashlib.sha256(body).hexdigest()

    cached = _cache_get(h)
    if cached is not None:
        # Return a copy with cached=true (the cached entry was stored with the
        # original cached=false, so we override on return).
        return JSONResponse({**cached, "cached": True})

    try:
        xml = body.decode("utf-8")
    except UnicodeDecodeError:
        raise HTTPException(status_code=400, detail="body is not valid utf-8")

    if not xml.strip():
        raise HTTPException(status_code=400, detail="body has no content")

    svg, annotations = engine.build_from_string(
        "svg", xml, environment="pyodide"
    )
    if not svg:
        # Empty SVG means the C++ render rejected the input.  502 because from
        # the client's perspective this server is the upstream, and the failure
        # is downstream of HTTP request validation.
        raise HTTPException(
            status_code=502,
            detail="render produced empty SVG (check XML for a valid <diagram>)",
        )

    result: dict[str, Any] = {
        "svg": svg,
        "annotationsXml": annotations,  # already string-or-None from engine
        "hash": h,
        "cached": False,
        "annotationsGenerated": annotations is not None,
    }
    # Cache the canonical (uncached) form; we mutate `cached` on lookup.
    _cache_put(h, dict(result))
    return JSONResponse(result)


# Static frontend at "/" — mounted last so it doesn't shadow /api or /build routes.
app.mount("/", StaticFiles(directory=STATIC_DIR, html=True), name="static")
