PYTHON ?= python3

.PHONY: help setup-python test test-plotread

help:
	@echo "PlotRead commands"
	@echo "  Python default: python3 from PATH (override with PYTHON=...)"
	@echo "  make setup-python  Sync the project-level uv environment"
	@echo "  make test          Run SHD reader and plotting tests"
	@echo "  make test-plotread Alias for make test"

setup-python:
	uv sync --project ../..

test: test-plotread

test-plotread:
	PYTHONDONTWRITEBYTECODE=1 \
	MPLCONFIGDIR=/tmp/bellhop-plotread-matplotlib \
	XDG_CACHE_HOME=/tmp/bellhop-plotread-cache \
		$(PYTHON) -m unittest discover -s tests -p 'test_*.py' -v
