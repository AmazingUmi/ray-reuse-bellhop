# BB-N4 — Format Gate Version Pin and Whole-Tree Reformat

Goal:
- Fix the engineering-gate clang-format failure on GitHub macos-26 (first
  full CI run of the split gate exposed it).
- Pin the project/CI clang-format so the gate no longer depends on whatever
  formatter the runner happens to expose.

Diagnosis:
- The committed style was produced by a formatter binary no longer present
  (IGR-3A A01 finding "format-gate environment drift"): Apple clang-format
  21.0.0 and pip clang-format 14-23 all disagreed with HEAD (violations in
  essentially every C++ file; signature-wrapping class plus include blocks
  left unsorted by the BB-N1 renames).
- macos-26 runner image: system Clang/LLVM 21.0.0 (CLT 26.6), Homebrew
  llvm@20 20.1.8 off-PATH. check_format.sh resolved the CLT formatter via
  xcrun, same class as the local machine.
- Version equivalence probe: Apple clang-format 21.0.0 (clang-2100.1.1.101)
  and pip clang-format 21.1.8 produce byte-identical output on all 167
  C++ files → pinning 21.1.8 makes the uv env, the CI runner system
  formatter and the local xcrun fallback agree.

### A01 [STANDARD] Pin formatter + reformat tree + version echo
Status: DONE
Reviewer: N/A (mechanical reformat; no production semantics change)

Goal:
- `clang-format==21.1.8` exact pin in the root uv dev group (pyproject +
  uv.lock); CI's existing `uv sync --locked` / `uv run` picks it up ahead
  of the xcrun fallback.
- Whole-tree `clang-format -i` with the pinned binary (55 files,
  includes reordered within blocks only: 20 added / 20 removed include
  lines, balanced).
- check_format.sh prints the resolved formatter path and
  `clang-format --version` before the dry-run.

Scope guard:
- BB-N3 sanitizer exclusion untouched; no production semantics, no test
  oracle, no test matrix, no .clang-format config change.

Acceptance / Evidence:
- `bash scripts/check_format.sh` PASS via xcrun fallback (Apple 21.0.0).
- `uv run bash scripts/check_format.sh` PASS with the pin (21.1.8), both
  printing the formatter version line.
- `git diff --check` clean.
- Post-reformat smoke: Release build clean; `ctest --preset release`
  61/61 PASS (89.8s local).
- pyproject diff: single dev-group line; uv.lock +27 lines.

Blockers:
- none
