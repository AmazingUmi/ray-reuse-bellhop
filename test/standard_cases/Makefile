PYTHON ?= conda run -n py python
VERSION ?= origin
CASE ?= all
PROFILE ?= single
VERSIONS ?= available
PROFILES ?= single,broadband_smoke
EXECUTABLE ?=
REFERENCE ?=
CANDIDATE ?=

CLI := $(PYTHON) codes/standard_cases.py
EXECUTABLE_ARG := $(if $(EXECUTABLE),--executable "$(EXECUTABLE)",)

.PHONY: help list generate run validate test test-unit batch compare

help:
	@echo "Standard-case commands"
	@echo "  make list"
	@echo "  make generate VERSION=origin CASE=munk_cerveny_cc PROFILE=single"
	@echo "  make run      VERSION=origin CASE=munk_cerveny_cc PROFILE=single"
	@echo "  make validate VERSION=origin CASE=munk_cerveny_cc PROFILE=single"
	@echo "  make test     VERSION=origin CASE=munk_cerveny_cc PROFILE=single"
	@echo "  make batch    VERSIONS=available PROFILES=single,broadband_smoke"
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

compare:
	@test -n "$(REFERENCE)" || \
		{ echo "REFERENCE is required"; exit 2; }
	@test -n "$(CANDIDATE)" || \
		{ echo "CANDIDATE is required"; exit 2; }
	$(CLI) compare "$(REFERENCE)" "$(CANDIDATE)"
