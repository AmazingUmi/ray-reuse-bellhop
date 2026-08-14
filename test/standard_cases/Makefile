PYTHON ?= conda run -n py python
VERSION ?= origin
CASE ?= all
PROFILE ?= single
VERSIONS ?= available
PROFILES ?= single,broadband_smoke
EXECUTABLE ?=
SET ?= core
F2CPP_BUILD_DIR ?= ../../Bellhop_F2CPP/build/release
REFERENCE ?=
CANDIDATE ?=

CLI := $(PYTHON) codes/standard_cases.py
EXECUTABLE_ARG := $(if $(EXECUTABLE),--executable "$(EXECUTABLE)",)

.PHONY: help list generate run validate test test-unit batch compare \
	feature f2cpp-regression f2cpp-full

help:
	@echo "Standard-case commands"
	@echo "  make list"
	@echo "  make generate VERSION=origin CASE=munk_cerveny_cc PROFILE=single"
	@echo "  make run      VERSION=origin CASE=munk_cerveny_cc PROFILE=single"
	@echo "  make validate VERSION=origin CASE=munk_cerveny_cc PROFILE=single"
	@echo "  make test     VERSION=origin CASE=munk_cerveny_cc PROFILE=single"
	@echo "  make batch    VERSIONS=available PROFILES=single,broadband_smoke"
	@echo "  make feature  SET=arrival"
	@echo "  make f2cpp-regression"
	@echo "  make f2cpp-full"
	@echo "  make compare  REFERENCE=reference.shd CANDIDATE=candidate.shd"

list:
	$(CLI) list

generate run validate test:
	$(CLI) $@ \
		--version "$(VERSION)" \
		--case "$(CASE)" \
		--profile "$(PROFILE)" $(EXECUTABLE_ARG)

test-unit:
	PYTHONDONTWRITEBYTECODE=1 \
		$(PYTHON) -m unittest discover -s codes/tests -p 'test_*.py' -v

batch: test-unit
	$(CLI) batch \
		--versions "$(VERSIONS)" \
		--profiles "$(PROFILES)" $(EXECUTABLE_ARG)

feature:
	$(CLI) suite --test-set "$(SET)" --profile single \
		--build-dir "$(F2CPP_BUILD_DIR)" $(EXECUTABLE_ARG)

f2cpp-regression:
	$(CLI) suite --test-set f2cpp_regular --profile single \
		--build-dir "$(F2CPP_BUILD_DIR)" $(EXECUTABLE_ARG)

f2cpp-full: test-unit
	ctest --test-dir "$(F2CPP_BUILD_DIR)" --output-on-failure
	$(CLI) test --version f2cpp --case all --profile single $(EXECUTABLE_ARG)

compare:
	@test -n "$(REFERENCE)" || \
		{ echo "REFERENCE is required"; exit 2; }
	@test -n "$(CANDIDATE)" || \
		{ echo "CANDIDATE is required"; exit 2; }
	$(CLI) compare "$(REFERENCE)" "$(CANDIDATE)"
