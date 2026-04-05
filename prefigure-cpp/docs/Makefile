# PreFigure C++ Documentation
# Build with: make html
# Prerequisites: pip install -r requirements.txt && apt install doxygen

SPHINXBUILD = sphinx-build
SOURCEDIR   = .
BUILDDIR    = _build

.PHONY: help html clean doxygen

help:
	@echo "  make html    - Build HTML documentation (runs Doxygen + Sphinx)"
	@echo "  make doxygen - Run Doxygen only (generates XML for Breathe)"
	@echo "  make clean   - Remove all build artifacts"

doxygen:
	doxygen Doxyfile

html: doxygen
	$(SPHINXBUILD) -b html $(SOURCEDIR) $(BUILDDIR)/html
	@echo ""
	@echo "Documentation built: $(BUILDDIR)/html/index.html"

clean:
	rm -rf $(BUILDDIR)
