"""FastAPI server exposing the prefig C++ rendering pipeline to a browser.

Architecture:

    Browser  --HTTP-->  FastAPI (this module)  --PyBind11-->  _prefigure (C++)

The server holds the C++ module loaded in-process and serves diagram renders
via two HTTP endpoints:

    GET  /api/examples/implicit   - renders the bundled implicit.xml example.
                                    Convenience entry used by the demo button.
    POST /api/render              - renders arbitrary PreFigure XML supplied
                                    in the request body. The general case.

Same-origin static frontend is mounted at "/" so a single uvicorn process
serves both the HTML and the API.

Run from the repo root with the project venv active:

    uvicorn webdemo.server:app --host 127.0.0.1 --port 8000 --workers 1

CORS:
    By default, no cross-origin allowance is configured (same-origin only,
    which is what the bundled demo uses).  To enable cross-origin requests
    later, set PREFIGURE_CORS_ORIGINS to a comma-separated list of origins,
    e.g.  PREFIGURE_CORS_ORIGINS=https://my-frontend.example,https://other.example
"""

from __future__ import annotations

import logging
import os
from contextlib import asynccontextmanager
from pathlib import Path

from fastapi import FastAPI, HTTPException
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import Response
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
    allow_methods=["GET", "POST"],
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


# Static frontend at "/" — mounted last so it doesn't shadow /api routes.
app.mount("/", StaticFiles(directory=STATIC_DIR, html=True), name="static")
