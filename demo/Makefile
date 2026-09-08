PYTHON ?= python3
ENV ?= cases/reliability/munk_cerveny_cc.env
VERSIONS ?= origin,f2cpp,broadband
DPI ?= 180
MULTI_ENV ?= cases/rayreuse_multifrequency/munk_rayreuse_multifrequency.env
MULTI_EXECUTION_MODE ?= reuse
MULTI_REUSE_MODE ?= serial
MULTI_INDEXES ?= 0,2,4
EXECUTION_ENV ?= $(MULTI_ENV)
EXECUTION_ROUTES ?= nonreuse,serial,frequency,range
EXECUTION_WORKERS ?= 2
EXECUTION_INDEXES ?= 2

CLI := $(PYTHON) codes/reliability.py
MULTI_CLI := $(PYTHON) codes/rayreuse_multifrequency.py
EXECUTION_CLI := $(PYTHON) codes/execution_modes.py
PLOT_ENV := MPLCONFIGDIR=/tmp/bellhop-showcase-matplotlib XDG_CACHE_HOME=/tmp/bellhop-showcase-cache
COMMON_ARGS := --environment "$(ENV)"

.PHONY: help check run plot show origin f2cpp broadband all \
	rayreuse-multifrequency multifrequency-run multifrequency-plot \
	execution-modes execution-modes-run execution-modes-plot test

help:
	@echo "Bellhop reliability and multi-frequency showcases"
	@echo "  make check      Check the ENV and three executables"
	@echo "  make origin     Directly run and plot Original Bellhop"
	@echo "  make f2cpp      Directly run and plot Bellhop F2CPP"
	@echo "  make broadband   Directly run and plot Bellhop Broadband"
	@echo "  make all        Run the same ENV with all three and compare"
	@echo "  make rayreuse-multifrequency  Run one multi-frequency ENV and plot slices"
	@echo "  make execution-modes          Run the four execution-mode routes and compare"
	@echo "  make run        Calculate selected VERSIONS only"
	@echo "  make plot       Plot existing selected VERSIONS results"
	@echo "  make test       Run showcase unit tests"

check:
	$(CLI) check $(COMMON_ARGS) --versions "$(VERSIONS)"

run:
	$(CLI) run $(COMMON_ARGS) --versions "$(VERSIONS)"

plot:
	$(PLOT_ENV) $(CLI) plot $(COMMON_ARGS) --versions "$(VERSIONS)" --dpi "$(DPI)"

show:
	$(PLOT_ENV) $(CLI) show $(COMMON_ARGS) --versions "$(VERSIONS)" --dpi "$(DPI)"

origin f2cpp broadband:
	$(PLOT_ENV) $(CLI) show $(COMMON_ARGS) --versions "$@" --dpi "$(DPI)"

all:
	$(PLOT_ENV) $(CLI) show $(COMMON_ARGS) \
		--versions "origin,f2cpp,broadband" --dpi "$(DPI)"

rayreuse-multifrequency:
	$(PLOT_ENV) $(MULTI_CLI) show \
		--environment "$(MULTI_ENV)" --execution-mode "$(MULTI_EXECUTION_MODE)" $(if $(filter reuse,$(MULTI_EXECUTION_MODE)),--reuse-mode "$(MULTI_REUSE_MODE)") \
		--plot-indexes "$(MULTI_INDEXES)" --dpi "$(DPI)"

multifrequency-run:
	$(MULTI_CLI) run \
		--environment "$(MULTI_ENV)" --execution-mode "$(MULTI_EXECUTION_MODE)" $(if $(filter reuse,$(MULTI_EXECUTION_MODE)),--reuse-mode "$(MULTI_REUSE_MODE)")

multifrequency-plot:
	$(PLOT_ENV) $(MULTI_CLI) plot \
		--environment "$(MULTI_ENV)" --plot-indexes "$(MULTI_INDEXES)" \
		--dpi "$(DPI)"

execution-modes:
	$(PLOT_ENV) $(EXECUTION_CLI) show \
		--environment "$(EXECUTION_ENV)" --routes "$(EXECUTION_ROUTES)" \
		--workers "$(EXECUTION_WORKERS)" --plot-indexes "$(EXECUTION_INDEXES)" \
		--dpi "$(DPI)"

execution-modes-run:
	$(EXECUTION_CLI) run \
		--environment "$(EXECUTION_ENV)" --routes "$(EXECUTION_ROUTES)" \
		--workers "$(EXECUTION_WORKERS)"

execution-modes-plot:
	$(PLOT_ENV) $(EXECUTION_CLI) plot \
		--environment "$(EXECUTION_ENV)" --routes "$(EXECUTION_ROUTES)" \
		--workers "$(EXECUTION_WORKERS)" --plot-indexes "$(EXECUTION_INDEXES)" \
		--dpi "$(DPI)"

test:
	PYTHONDONTWRITEBYTECODE=1 $(PYTHON) -m unittest discover \
		-s codes/tests -p 'test_*.py' -v
