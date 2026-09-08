PYTHON ?= python3
ENV ?= cases/reliability/munk_cerveny_cc.env
VERSIONS ?= origin,f2cpp,broadband
DPI ?= 180
SPEED_ENV ?= cases/reliability/munk_rayreuse_multifrequency.env
SPEED_ROUTES ?= nonreuse,serial,frequency,range
SPEED_WORKERS ?= 4
SPEED_FREQUENCY_INDEX ?= 2

CLI := $(PYTHON) codes/reliability.py
SPEED_CLI := $(PYTHON) codes/speed.py
PLOT_ENV := MPLCONFIGDIR=/tmp/bellhop-showcase-matplotlib XDG_CACHE_HOME=/tmp/bellhop-showcase-cache
COMMON_ARGS := --environment "$(ENV)"

.PHONY: help check run plot show origin f2cpp broadband all \
	speed speed-run speed-plot test

help:
	@echo "Bellhop reliability and multi-frequency showcases"
	@echo "  make check      Check the ENV and three executables"
	@echo "  make origin     Directly run and plot Original Bellhop"
	@echo "  make f2cpp      Directly run and plot Bellhop F2CPP"
	@echo "  make broadband   Directly run and plot Bellhop Broadband"
	@echo "  make all        Run the same ENV with all three and compare"
	@echo "  make speed      Run the four execution-mode routes; plot TL consistency and speed"
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

speed:
	$(SPEED_CLI) run \
		--environment "$(SPEED_ENV)" --routes "$(SPEED_ROUTES)" \
		--workers "$(SPEED_WORKERS)"
	$(PLOT_ENV) $(SPEED_CLI) plot \
		--environment "$(SPEED_ENV)" --routes "$(SPEED_ROUTES)" \
		--workers "$(SPEED_WORKERS)" --dpi "$(DPI)"
	$(PLOT_ENV) $(CLI) routes \
		--environment "$(SPEED_ENV)" --workers "$(SPEED_WORKERS)" \
		--frequency-index "$(SPEED_FREQUENCY_INDEX)" --dpi "$(DPI)"

speed-run:
	$(SPEED_CLI) run \
		--environment "$(SPEED_ENV)" --routes "$(SPEED_ROUTES)" \
		--workers "$(SPEED_WORKERS)"

speed-plot:
	$(PLOT_ENV) $(SPEED_CLI) plot \
		--environment "$(SPEED_ENV)" --routes "$(SPEED_ROUTES)" \
		--workers "$(SPEED_WORKERS)" --dpi "$(DPI)"
	$(PLOT_ENV) $(CLI) routes \
		--environment "$(SPEED_ENV)" --workers "$(SPEED_WORKERS)" \
		--frequency-index "$(SPEED_FREQUENCY_INDEX)" --dpi "$(DPI)"

test:
	PYTHONDONTWRITEBYTECODE=1 $(PYTHON) -m unittest discover \
		-s codes/tests -p 'test_*.py' -v
