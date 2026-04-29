# Container image for the prefig FastAPI server.
#
# Build:
#     docker build -t prefig-webdemo -f webdemo/Dockerfile .
# (build context must be the repo root; the Dockerfile is in webdemo/.)
#
# Run:
#     docker run --rm -p 8000:8000 \
#         -e PREFIGURE_CORS_ORIGINS=https://your-frontend.example \
#         prefig-webdemo
#
# Endpoints exposed (see webdemo/server.py for the full contract):
#     POST /build                  drop-in for prefigure.doenet.org/build
#     POST /api/render             JSON body, our own contract
#     GET  /api/examples/implicit  bundled demo
#     GET  /api/health             liveness probe
#     GET  /                       static demo HTML (served from webdemo/static/)
#
# Environment variables:
#     PREFIGURE_CORS_ORIGINS  Comma-separated list of origins to allow.
#                              Empty/unset = same-origin only (CORS is a no-op).

FROM python:3.12-slim-bookworm AS base

# System libraries the C++ port links against, plus the build toolchain
# (it stays in the image because rebuilding from source on container start
# is not the contract here -- we ship a pre-built C++ extension).
# Node.js is needed at runtime by the MathJax daemon (mj-sre-daemon.js).
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        cmake \
        pkg-config \
        libeigen3-dev \
        libspdlog-dev \
        libboost-all-dev \
        libgeos-dev \
        libcairo2-dev \
        libpango1.0-dev \
        liblouis-dev \
        pybind11-dev \
        nodejs \
        npm \
        ca-certificates \
        curl \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Copy the parts of the repo that the build needs.  Order matters for layer
# caching: deps and pyproject.toml first, source last.
COPY pyproject.toml CMakeLists.txt README.md ./
COPY prefigure-cpp/ ./prefigure-cpp/
COPY prefig/ ./prefig/
COPY examples/ ./examples/
COPY webdemo/ ./webdemo/

# Build the C++ extension and install the prefig package, plus the demo's
# runtime dependencies.  We install in editable mode so that `prefig.engine`
# resolves to the source tree and the MathJax daemon JS files travel with
# it; the `_prefigure.so` lands inside the install path.
RUN pip install --no-cache-dir --upgrade pip \
    && pip install --no-cache-dir -e . \
    && pip install --no-cache-dir -r webdemo/requirements.txt

# Sanity: ensure the C++ extension actually loaded.
RUN python -c "from prefig import _prefigure; \
    svg, ann = _prefigure.build_from_string('svg', \
        open('/app/examples/implicit.xml').read(), 'pyodide'); \
    assert len(svg) > 1000, 'C++ render failed'; \
    print('C++ build_from_string OK,', len(svg), 'bytes')"

EXPOSE 8000

# uvicorn with a single worker so the C++ module is loaded once and
# concurrent requests serialize at the Python threadpool boundary.
# The lifespan hook in webdemo/server.py warms the C++ pipeline at startup.
CMD ["uvicorn", "webdemo.server:app", \
     "--host", "0.0.0.0", \
     "--port", "8000", \
     "--workers", "1"]
