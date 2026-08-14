# I8-04 — Validation & documentation closure

This stage adds independent product readers, a direct Fortran accumulator
oracle, end-to-end parity matrices, and reproducible reports. Codex—not
OpenCode—updates task status, acceptance records, progress documents, and Git.

## I8-04-T1

### Task ID

`I8-04-T1`

### Status

ACCEPTED

### Objective

Extend the standard-case framework with independent ASCII/binary ARR readers
and EOF-based eigenray RAY parsing.

### Background

`case_model.py` currently permits only `shd` and fixed-count ordinary `ray`
products. I8 products need format-aware expected-product checks before cases can
serve as project truth.

### Depends On

- `I8-02-T3` and `I8-03-T5` user-visible products.
- Existing `case_model.py`, `standard_cases.py`, and
  `validate_i6_ray_trace.py` reader conventions.

### Allowed Changes

- `test/standard_cases/codes/case_model.py`.
- `test/standard_cases/codes/standard_cases.py`.
- New `test/standard_cases/codes/arrivals_io.py`.
- New `test/standard_cases/codes/eigenray_io.py`, or a narrowly factored shared
  RAY reader that preserves I6 behavior.
- Python tests under `test/standard_cases/codes/tests`.

### Do Not Modify

- F2CPP/Origin implementation code or output formats.
- Existing SHD and ordinary R case semantics.
- Case fixtures for parity (T3/T4).
- `Bellhop_RayReuse`, task/progress documents, or validation reports.

### Requirements

1. Add explicit output kinds `arrivals_ascii`, `arrivals_binary`, and
   `eigenray`; retain `shd` and ordinary `ray` behavior.
2. Standard-case runs require exactly the expected final product, reject stale
   incompatible SHD/RAY/ARR products, reject `.tmp`, and record product hashes.
3. ASCII reader parses the full header and exactly one source body per source;
   caller supplies expected receiver layout because ARR does not encode it.
4. Binary reader validates little-endian int32 record markers, equal closing
   markers, every expected payload length/type, complete source/cell bodies,
   and exact EOF.
5. Both ARR readers return one canonical typed structure with source-major
   cells and arrival fields. Reject non-finite values, negative counts/bounces,
   impossible dimensions, truncation, and trailing data.
6. Ordinary R parser continues to consume the header-derived fixed block count.
   Eigenray parser consumes valid blocks to EOF and allows zero blocks or a
   block count different from Nalpha.
7. Keep product parsing independent of F2CPP writer code.

### Acceptance Criteria

- Python tests cover valid zero/multiple/multi-source/irregular ASCII and binary
  products and prove canonical semantic equality.
- Corrupt marker, wrong endian, truncated payload, count mismatch, non-finite
  field, trailing byte/text, and unexpected product tests fail clearly.
- Eigenray tests prove zero blocks, repeated launch angles, Nalpha mismatch,
  valid EOF, truncation rejection, and ordinary R fixed-count behavior.
- Existing 123 Python tests and all existing standard cases remain green.

### Suggested Tests

```bash
/Users/luyiyang/miniconda3/envs/py/bin/python -m unittest discover -s test/standard_cases/codes/tests -p 'test_*.py'
/Users/luyiyang/miniconda3/envs/py/bin/python test/standard_cases/codes/standard_cases.py list
```

### Deliverables

- Do not execute `git commit` and do not continue to T2.
- Report modified files, new schemas/readers, exact tests/results, deviations,
  and unresolved findings.

### Acceptance Record

- Accepted commit: this I8-04-T1 reader checkpoint commit.
- Tests: full Python regression 131/131 passed; independent ASCII/binary ARR
  and EOF eigenray tests cover zero/multiple sources and arrivals, caller-
  supplied irregular cell count, repeated eigenray angles, Nalpha mismatch,
  marker/count/finite/truncation/trailing failures; standard-case listing and
  `git diff --check` passed.
- Oracle / parity result: readers are independent of F2CPP writer code and
  implement the frozen GNU Fortran record ABI / RAY text grammar. Product
  parity cases remain assigned to T3/T4.
- Notes: ordinary R keeps its existing fixed-count path; eigenray blocks are
  parsed to exact EOF. Standard-case records now carry the selected product
  path and SHA-256 and reject incompatible stale products and temporaries.

## I8-04-T2

### Task ID

`I8-04-T2`

### Status

ACCEPTED

### Objective

Create a direct GNU Fortran `ArrMod::AddArr` component oracle and validate the
F2CPP accumulator against all grouping/capacity edge semantics.

### Background

End-to-end cases cannot cheaply force Origin's 20,000,000-slot grid to saturate.
A small driver may `USE ArrMod`, allocate a one-cell grid with a chosen
`MaxNArr`, call the real `AddArr`, and emit stored fields. Copying the algorithm
into Python is not an oracle.

### Depends On

- `I8-01-T2` accepted accumulator.
- `I8-04-T1` Python testing conventions.
- Existing GNU Fortran module/object build.

### Allowed Changes

- New standalone probe source under `Bellhop_origin/Bellhop` or
  `test/standard_cases/oracles` that imports the actual `ArrMod` module.
- `Bellhop_origin/Makefile` only for a non-default probe target that does not
  alter the normal `bellhop` executable.
- New `Bellhop_F2CPP/tests/tools/arrival_accumulator_probe.cpp` and the minimum
  CMake target registration needed to expose the accepted accumulator.
- New `test/standard_cases/codes/validate_i8_arrival_accumulator.py`.
- New Python unit tests for the validator/parser.
- A generated report under `Bellhop_F2CPP/doc/validation` only after Codex
  verifies it.

### Do Not Modify

- `ArrMod.f90` numerical semantics or normal Origin executable behavior.
- F2CPP accumulator implementation unless Codex creates a separate fix task.
- F2CPP solver/writer/parser, cases, RayReuse, or task/progress documents.
- Embed copied `AddArr` logic as the expected result.

### Requirements

1. Link/use the repository's actual `ArrMod`; expose deterministic scenarios
   through a fixed scenario ID, not arbitrary code execution.
2. The F2CPP probe must call the accepted production accumulator API; it must
   not contain a second implementation of grouping or capacity logic.
3. Cover append, last-only duplicate, non-last similar candidate, delay
   threshold below/equal/above, phase threshold below/equal/above, weighted
   float merge, preserved phase/bounces, axial-cusp guard, first-minimum tie,
   stronger replacement, equal/weaker discard, and zero arrivals.
4. Emit every stored field, count, and relevant action in a machine-readable,
   finite format; bind report provenance to probe source, `ArrMod.f90`, Origin
   executable/object, F2CPP test executable, and hashes.
5. Compare stored floats by bit pattern where both sides deliberately perform
   the same float operation; otherwise enforce a maximum of one float32 ULP.
6. Do not patch Origin behavior to make it easier to match.

### Acceptance Criteria

- Probe output demonstrates every required branch and strict boundary.
- F2CPP count/order/actions and all stored fields agree with the actual ArrMod
  oracle under the frozen bit/one-ULP gate.
- Validator rejects missing scenarios, duplicate IDs, non-finite values,
  provenance mismatch, and intentionally corrupted expected fields.
- Probe build leaves the normal Origin bellhop hash unchanged.
- Python tests pass and a reproducible validator command is recorded.

### Suggested Tests

```bash
make -C Bellhop_origin arrival-accumulator-probe
cmake --build Bellhop_F2CPP/build/release -j 8 --target bellhop_f2cpp_arrival_accumulator_probe
/Users/luyiyang/miniconda3/envs/py/bin/python -m unittest test.standard_cases.codes.tests.test_validate_i8_arrival_accumulator
/Users/luyiyang/miniconda3/envs/py/bin/python test/standard_cases/codes/validate_i8_arrival_accumulator.py --origin-probe Bellhop_origin/bin/arrival_accumulator_probe --f2cpp-probe Bellhop_F2CPP/build/release/bellhop_f2cpp_arrival_accumulator_probe --output Bellhop_F2CPP/doc/validation/i8_arrival_accumulator_report.json
```

### Deliverables

- Do not commit and do not continue to T3.
- Report modified files, probe linkage/provenance, exact scenarios/tests/results,
  deviations, and unresolved findings.

### Acceptance Record

- Accepted commit: this I8-04-T2 oracle checkpoint commit.
- Tests: Origin and F2CPP probes built independently; validator unit tests
  passed 4/4 on Python 3.12; production arrival-workspace tests passed on
  AppleClang Debug sanitizer and GCC 14 Werror; `git diff --check` passed.
- Oracle / parity result: the actual `ArrMod::AddArr` and production
  `ArrivalWorkspace` agree for all 15 fixed scenarios, 24 stored arrivals and
  all 144 float fields bit-for-bit, with exact count/order/bounce parity.
- Notes: the oracle exposed and closed one real gap: finite signed candidate
  amplitudes are required for Origin's axial-cusp cancellation guard. The
  normal Origin `bellhop` executable hash is unchanged by the non-default probe.

## I8-04-T3

### Task ID

`I8-04-T3`

### Status

ACCEPTED

### Objective

Add the complete Origin/F2CPP `A/a` standard-case matrix and a semantic arrival
validator with frozen numerical/effect gates.

### Background

Arrival parity requires exact source/cell/record sequencing plus float-field
comparison. ASCII and binary encodings from the same environment must describe
the same product, while direct/multipath/zero and existing input features remain
observable.

### Depends On

- `I8-04-T1` readers.
- `I8-04-T2` accumulator semantics.
- `I8-02-T3` complete A/a CLI.

### Allowed Changes

- New cases under `test/standard_cases/cases` dedicated to I8 arrivals.
- New `test/standard_cases/codes/validate_i8_arrivals.py`.
- Its Python unit tests.
- `case_model.py`/`standard_cases.py` only for defects exposed by the accepted
  T1 design.
- Generated `Bellhop_F2CPP/doc/validation/i8_arrivals_report.json` after
  successful validation.

### Do Not Modify

- F2CPP or Origin numerical implementation; use a Codex-approved fix task for
  discrepancies.
- Existing case inputs/tolerances or global field tolerances.
- Eigenray validation (T4), RayReuse, or task/progress documents.

### Requirements

1. Create exactly these case IDs unless Codex first approves a documented
   rename: `i8_arrivals_geometric_hat_ascii`,
   `i8_arrivals_geometric_hat_binary`,
   `i8_arrivals_geometric_hat_ray_centered`,
   `i8_arrivals_geometric_gaussian_irregular`,
   `i8_arrivals_line_directional_multisource`, and `i8_arrivals_zero`.
2. The case matrix must collectively cover ASCII and binary, `G/g/B`, regular
   and supported irregular receivers, point and line source scaling,
   directional source pattern, multiple sorted sources, direct arrivals,
   multiple/reflected paths, at least one caustic/unwrapped phase, duplicate
   grouping, and a valid zero-arrival source/cell.
3. Pair A/a cases with otherwise identical rendered inputs where encoding
   equivalence is asserted. Each case lists exact Origin source references.
4. Run Origin and F2CPP from distinct hashed executable paths and reject stale
   outputs older than either executable or rendered ENV.
5. Require exact header dimensions/order, source body count, receiver-cell
   count/order, per-cell arrival count/order, bounce counts, zero structure,
   and maximum-count records.
6. Compare decoded stored amplitude, delay real/imaginary, phase, and angles
   within at most eight float32 ULPs. No sorting or nearest-time matching is
   allowed. A wider gate requires a separate Codex architecture decision.
7. Add effect guards proving multipath/caustic/reflection, source separation,
   point-vs-line scaling, directional-pattern effect, and irregular pairing are
   nonempty on both implementations.
8. Report maxima and locations per field, counts by path/bounce class,
   executable/source hashes, case commands, and `status` in deterministic JSON.

### Acceptance Criteria

- All matrix cases pass independently for Origin and F2CPP.
- A/a semantic equivalence passes for paired cases.
- Every exact structural gate and <=8-ULP field gate passes.
- Required zero, duplicate, multipath, caustic, source, source-geometry,
  directional, family, and receiver-layout effects are observed.
- Validator unit tests reject reordered arrivals, count changes, missing zero
  cells, stale artifacts, wrong executable identity, and threshold violations.
- `i8_arrivals_report.json` is produced only by the validator and is
  byte-deterministic for unchanged inputs.

### Suggested Tests

```bash
/Users/luyiyang/miniconda3/envs/py/bin/python -m unittest test.standard_cases.codes.tests.test_validate_i8_arrivals
/Users/luyiyang/miniconda3/envs/py/bin/python test/standard_cases/codes/standard_cases.py test --version origin --case i8_arrivals_geometric_hat_ascii --case i8_arrivals_geometric_hat_binary --case i8_arrivals_geometric_hat_ray_centered --case i8_arrivals_geometric_gaussian_irregular --case i8_arrivals_line_directional_multisource --case i8_arrivals_zero --profile single --executable Bellhop_origin/bin/bellhop --results-root /tmp/i8_arrivals
/Users/luyiyang/miniconda3/envs/py/bin/python test/standard_cases/codes/standard_cases.py test --version f2cpp --case i8_arrivals_geometric_hat_ascii --case i8_arrivals_geometric_hat_binary --case i8_arrivals_geometric_hat_ray_centered --case i8_arrivals_geometric_gaussian_irregular --case i8_arrivals_line_directional_multisource --case i8_arrivals_zero --profile single --executable Bellhop_F2CPP/build/release/bellhop_f2cpp --results-root /tmp/i8_arrivals
/Users/luyiyang/miniconda3/envs/py/bin/python test/standard_cases/codes/validate_i8_arrivals.py --results-root /tmp/i8_arrivals --origin-executable Bellhop_origin/bin/bellhop --f2cpp-executable Bellhop_F2CPP/build/release/bellhop_f2cpp --output Bellhop_F2CPP/doc/validation/i8_arrivals_report.json
```

### Deliverables

- Do not commit and do not continue to T4.
- Report cases/files, exact commands/results, worst discrepancies/effect values,
  deviations, and unresolved findings.

### Acceptance Record

- Accepted commit: this I8-04-T3 arrival-parity checkpoint commit.
- Tests: all six cases passed independently for Origin and F2CPP; arrival
  validator unit tests passed 3/3, the parity-checkpoint Python suite passed
  140/140 (excluding the still-pending T5 output-safety tests), repeated report
  generation was byte-identical, and `git diff --check` passed.
- Oracle / parity result: exact source/cell/record order, counts, bounce fields,
  zero structure and every stored float field agree. All compared float32
  fields have a maximum error of 0 ULP; the paired ASCII/binary products are
  semantically identical. The matrix contains 984 reflected arrivals, a
  123-arrival maximum cell, a three-cell irregular product, two ordered
  sources, and a valid zero-arrival cell.
- Notes: the frozen six-case matrix observes multiple arrivals, line-source
  directional amplitudes, and the corresponding Origin/F2CPP parity, but does
  not contain otherwise-identical controls that independently isolate all
  three effects. Duplicate grouping is instead isolated by the accepted real
  `ArrMod` oracle in T2; point/line and directional behavior retain the
  independent I7 source-geometry and I6 source-pattern gates. The generated
  report states this limitation explicitly rather than treating structural
  observations as isolated proof.

## I8-04-T4

### Task ID

`I8-04-T4`

### Status

ACCEPTED

### Objective

Add the complete Origin/F2CPP E-mode standard-case matrix and EOF-based
eigenray parity validator.

### Background

Eigenray output has no block-count field. Header Nalpha describes the launch
fan, while output blocks describe receiver hits and can repeat launch angles.
Parity therefore compares the ordered EOF stream and every written prefix.

### Depends On

- `I8-04-T1` EOF reader.
- `I8-03-T5` complete E CLI.
- `I8-04-T3` provenance/report conventions.

### Allowed Changes

- New I8 eigenray cases under `test/standard_cases/cases`.
- New `test/standard_cases/codes/validate_i8_eigenrays.py`.
- Its Python unit tests.
- Generated `Bellhop_F2CPP/doc/validation/i8_eigenrays_report.json` after
  successful validation.

### Do Not Modify

- F2CPP/Origin implementation or ordinary I6 R validation.
- Existing case tolerances, arrival validator, RayReuse, or task/progress docs.
- Sort/deduplicate eigenray blocks or infer block count from Nalpha.

### Requirements

1. Create exactly these case IDs unless Codex first approves a documented
   rename: `i8_eigenray_geometric_hat`,
   `i8_eigenray_geometric_hat_ray_centered`,
   `i8_eigenray_geometric_gaussian`, and `i8_eigenray_zero`; use the existing
   `ray_trace_vacuum_rigid` case as the ordinary-R control.
2. Matrix covers `G/g/B`, zero hits, multiple receivers, repeated launch-angle
   blocks, multiple sources, reflections, and a prefix ending on each side of a
   reflection where feasible.
3. Bind rendered input, executable, Origin `ApplyContribution`/`WriteRay2D`
   sources, and product hashes; reject stale/mixed provenance.
4. Require exact header source/launch dimensions, actual block count, block
   order, launch-angle order, point count, and top/bottom bounce counts.
5. Require coordinate-by-coordinate sequence equality within `1e-7 m`
   absolute and `1e-10` relative tolerance, without resampling.
6. Effect guards prove actual block count differs from launch count in at least
   one case, at least one launch angle repeats, zero-hit output is valid, and
   at least one prefix is shorter than its corresponding ordinary full ray.
7. Report maximum coordinate error/location, block/point/bounce totals, repeat
   counts, prefix statistics, hashes, commands, and deterministic status.

### Acceptance Criteria

- All Origin/F2CPP E cases pass exact structural and coordinate gates.
- EOF parser consumes every block and rejects truncation/trailing corruption.
- Required variable-count, repeated-angle, zero-hit, reflected-prefix, family,
  and multi-source guards are observed in both implementations.
- An ordinary R control remains unchanged and has exactly its header-derived
  block count.
- Validator unit tests detect reordered/missing/extra blocks, changed prefix
  counts/bounces, wrong coordinates, stale output, and provenance mismatch.
- Deterministic `i8_eigenrays_report.json` is generated by the validator.

### Suggested Tests

```bash
/Users/luyiyang/miniconda3/envs/py/bin/python -m unittest test.standard_cases.codes.tests.test_validate_i8_eigenrays
/Users/luyiyang/miniconda3/envs/py/bin/python test/standard_cases/codes/standard_cases.py test --version origin --case i8_eigenray_geometric_hat --case i8_eigenray_geometric_hat_ray_centered --case i8_eigenray_geometric_gaussian --case i8_eigenray_zero --case ray_trace_vacuum_rigid --profile single --executable Bellhop_origin/bin/bellhop --results-root /tmp/i8_eigenrays
/Users/luyiyang/miniconda3/envs/py/bin/python test/standard_cases/codes/standard_cases.py test --version f2cpp --case i8_eigenray_geometric_hat --case i8_eigenray_geometric_hat_ray_centered --case i8_eigenray_geometric_gaussian --case i8_eigenray_zero --case ray_trace_vacuum_rigid --profile single --executable Bellhop_F2CPP/build/release/bellhop_f2cpp --results-root /tmp/i8_eigenrays
/Users/luyiyang/miniconda3/envs/py/bin/python test/standard_cases/codes/validate_i8_eigenrays.py --results-root /tmp/i8_eigenrays --origin-executable Bellhop_origin/bin/bellhop --f2cpp-executable Bellhop_F2CPP/build/release/bellhop_f2cpp --output Bellhop_F2CPP/doc/validation/i8_eigenrays_report.json
```

### Deliverables

- Do not commit and do not continue to T5.
- Report cases/files, exact commands/results, worst discrepancies/effects,
  deviations, and unresolved findings.

### Acceptance Record

- Accepted commit: this I8-04-T4 eigenray-parity checkpoint commit.
- Tests: all four E cases and the ordinary-R control passed independently for
  Origin and F2CPP; eigenray validator unit tests passed 2/2, repeated report
  generation was byte-identical, and `git diff --check` passed.
- Oracle / parity result: all 2200 EOF-terminated blocks and 876191 written
  points agree exactly in block order, launch angles, prefix point counts,
  bounce counts, and coordinates (maximum coordinate error `0 m`). Both
  implementations have 1684 repeated-launch blocks, equal top/bottom bounce
  totals of 2830, two ordered sources, and a valid header-only zero-hit file.
- Notes: the validator never sorts, deduplicates, resamples, or derives the E
  block count from `Nalpha`. The existing ordinary-R case remains on its fixed
  header-count parser and supplies the frozen full-ray control.

## I8-04-T5

### Task ID

`I8-04-T5`

### Status

ACCEPTED

### Objective

Close the machine-verifiable I8 regression and output-safety matrix and produce
the final deterministic validation reports for Codex acceptance.

### Background

This task does not redesign I8. It runs and, only where missing, adds narrow
automation for the complete accepted matrix. Codex performs final document
status updates and commits after independently rerunning the evidence.

### Depends On

- `I8-04-T2`, `I8-04-T3`, and `I8-04-T4`.
- All I8-01 through I8-03 tasks accepted.

### Allowed Changes

- Narrow validator/orchestration fixes under `test/standard_cases/codes` and
  their unit tests.
- CLI output-safety fixtures/tests if a specified I8 lifecycle case is not yet
  covered.
- Generated reports:
  `doc/validation/i8_arrival_accumulator_report.json`,
  `doc/validation/i8_arrivals_report.json`,
  `doc/validation/i8_eigenrays_report.json`, and
  `doc/validation/i8_output_safety_report.json`.

### Do Not Modify

- Numerical implementation, public architecture, case tolerances, Origin
  semantics, or accepted I0–I7 code.
- `Bellhop_RayReuse`.
- I8 task documents, `PROGRESS.md`, `FURTHER_REPLICATION_PLAN.md`, or `USAGE.md`;
  Codex owns final documentation updates after acceptance.
- Add unrelated formatting/refactoring.

### Requirements

1. Run the accumulator oracle, arrival parity, eigenray parity, and product
   lifecycle validators from fresh result roots with distinct executable hashes.
2. Output-safety report covers `CC -> R -> A -> a -> E -> CC`, zero products,
   stale incompatible products, stale temporaries, and injected parse/solve/
   write failure preservation.
3. Run full AppleClang Debug ASan/UBSan, AppleClang Release, GCC 14
   Release/Werror, Python tests, every F2CPP single-frequency case, and all I8
   validators.
4. Reports contain commands, executable/source hashes and mtimes, case/product
   hashes, exact structural totals, worst numerical errors, and `status`.
5. Do not weaken an existing gate or suppress a failure. Report any mismatch
   for a Codex-created fix task.

### Acceptance Criteria

- All four I8 reports have deterministic `passed` status and verified
  provenance.
- Every I8 exact/ULP/coordinate/effect/output-safety gate passes.
- AppleClang Debug sanitizer, AppleClang Release, and GCC 14 Werror pass all
  CTests.
- The complete Python suite and all old/new F2CPP single-frequency cases pass.
- I7 Gaussian-family and I6 ordinary ray validators still pass unchanged.
- `git diff --check` passes and no generated result outside the approved reports
  appears in the working tree.

### Suggested Tests

```bash
cmake --build Bellhop_F2CPP/build/debug -j 8 && ctest --test-dir Bellhop_F2CPP/build/debug --output-on-failure
cmake --build Bellhop_F2CPP/build/release -j 8 && ctest --test-dir Bellhop_F2CPP/build/release --output-on-failure
cmake --build Bellhop_F2CPP/build/gcc14-release -j 8 && ctest --test-dir Bellhop_F2CPP/build/gcc14-release --output-on-failure
/Users/luyiyang/miniconda3/envs/py/bin/python -m unittest discover -s test/standard_cases/codes/tests -p 'test_*.py'
/Users/luyiyang/miniconda3/envs/py/bin/python test/standard_cases/codes/standard_cases.py test --version f2cpp --case all --profile single --executable Bellhop_F2CPP/build/release/bellhop_f2cpp --results-root /tmp/i8_full
git diff --check
```

### Deliverables

- Do not commit and do not start I9 or RayReuse synchronization.
- Report all modified/generated files, exact commands/results, report paths,
  worst metrics, deviations, and unresolved findings.

### Acceptance Record

- Accepted commit: this I8-04-T5 / I8 closure checkpoint commit.
- Tests: clean-first AppleClang Debug ASan/UBSan, AppleClang Release, and GCC
  14 Release/Werror each passed 37/37 CTests; Python passed 142/142; all 62
  F2CPP single-frequency cases passed. The I7 Gaussian-family and I6 ordinary
  ray validators passed unchanged, and `git diff --check` passed.
- Oracle / parity result: all four final reports have deterministic `passed`
  status and were regenerated against the final Release executable. The
  accumulator retains 15 scenarios / 24 arrivals / 144 bit-exact float fields;
  ARR remains 0 ULP across every compared record; E remains exact across 2200
  blocks / 876191 points. Output safety passes the formal
  `CC -> R -> A -> a -> E -> CC` sequence, zero-hit E, stale final/temporary
  cleanup, and parse/solve/publish-failure preservation.
- Notes: the solver-failure gate uses a syntactically valid Q environment whose
  range-dependent SSP ends before the configured tracing box, producing the
  solver's deterministic abnormal-termination path without a test hook. No
  numerical gate, public interface, I0–I7 code, Origin semantics, or RayReuse
  source was changed during closure.
