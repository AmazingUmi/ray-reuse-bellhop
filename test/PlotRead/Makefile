PYTHON ?= conda run -n py python

.PHONY: help setup-python test test-plotread

help:
	@echo "PlotRead commands"
	@echo "  Python default: conda environment 'py' (override with PYTHON=...)"
	@echo "  make setup-python  Install bellhop-shd into the configured Python environment"
	@echo "  make test          Run SHD reader and plotting tests"
	@echo "  make test-plotread Alias for make test"

setup-python:
	$(PYTHON) -m pip install -e .

test: test-plotread

test-plotread:
	PYTHONDONTWRITEBYTECODE=1 \
	MPLCONFIGDIR=/tmp/bellhop-plotread-matplotlib \
	XDG_CACHE_HOME=/tmp/bellhop-plotread-cache \
		$(PYTHON) -m unittest discover -s tests -p 'test_*.py' -v
