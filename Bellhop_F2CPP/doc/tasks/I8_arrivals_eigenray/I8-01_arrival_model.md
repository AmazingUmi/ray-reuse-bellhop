# I8-01 — Arrival data model & accumulation

This stage defines the source-local, frequency-local arrival product and connects
the already accepted geometric contribution paths to it. It does not write ARR
files and does not expose `A/a` through the CLI.

## I8-01-T1

### Task ID

`I8-01-T1`

### Status

ACCEPTED

### Objective

Add the Origin-compatible arrival record types, checked capacity planner, and
empty source-local workspace without implementing candidate accumulation.

### Background

Origin declares `Arrival` in `Bellhop_origin/Bellhop/ArrMod.f90` and plans
`max(20,000,000 / receiverCellCount, 10)` records per receiver cell. Stored
fields are default REAL/default COMPLEX even though contribution calculations
are binary64. One workspace belongs to one source and one frequency.

### Depends On

- Accepted I7 baseline.
- Stable `ReceiverGrid` and checked-arithmetic conventions in
  `io/output_layout.cpp`.

### Allowed Changes

- New `include/bellhop/field/arrival.hpp`.
- New `include/bellhop/field/arrival_workspace.hpp`.
- New `src/field/arrival_workspace.cpp`.
- New `tests/component/arrival_workspace_test.cpp`.
- `Bellhop_F2CPP/CMakeLists.txt` only to register the new source/test target.

### Do Not Modify

- Solver, influence, parser, CLI, or writer code.
- `FrequencyWorkspace` / `IntensityWorkspace` storage.
- Any accepted I0–I7 behavior or test expectation.
- `Bellhop_RayReuse`.
- Other I8 stages or public interfaces not named above.
- I8 task documents, `PROGRESS.md`, or `FURTHER_REPLICATION_PLAN.md`.

### Requirements

1. Define binary64 `ArrivalCandidate` and stored `Arrival` exactly as specified
   in the I8 README; bounce counts are signed 32-bit integers.
2. Define `kOriginArrivalStorageSlots = 20'000'000` and
   `kOriginMinimumArrivalsPerCell = 10` in one authoritative header.
3. Add a checked `ArrivalCapacityPlan` containing receiver cell count,
   per-cell capacity, and logical slot count. Use
   `receiversPerRange() * rangeCount()`; do not use header depth count for an
   irregular grid.
4. Reject zero dimensions, `size_t` overflow, int32-incompatible ARR counts,
   and any impossible allocation metadata before allocation.
5. Add `ArrivalWorkspace(frequency, receivers, optionalCapacityOverride)` with
   one lazy cell per actual receiver. The override is for component testing and
   must not become ENV/CLI syntax.
6. Expose const metadata and checked cell lookup. Do not reserve arrival
   records in every cell and do not add a pressure member.
7. Keep all types independent of global state and mutable `RayPathCache` data.

### Acceptance Criteria

- `bellhop_f2cpp_arrival_workspace_tests` builds and passes.
- Tests prove capacities for 1, 6, 2,000,000, and an overflow-boundary cell
  count, including the minimum-ten rule.
- Rectilinear and irregular flat-index tests prove the correct actual cell
  count and reject out-of-range indices.
- Constructing an empty workspace allocates zero `Arrival` records in every
  cell.
- `sizeof`/type-trait assertions prove float/complex-float/int32 stored fields.
- AppleClang Debug and GCC 14 Werror compile the target without warnings.
- Existing CTest targets remain green.

### Suggested Tests

```bash
cmake --build Bellhop_F2CPP/build/debug -j 8 --target bellhop_f2cpp_arrival_workspace_tests
ctest --test-dir Bellhop_F2CPP/build/debug -R f2cpp.component.arrival_workspace --output-on-failure
cmake --build Bellhop_F2CPP/build/gcc14-release -j 8 --target bellhop_f2cpp_arrival_workspace_tests
ctest --test-dir Bellhop_F2CPP/build/gcc14-release -R f2cpp.component.arrival_workspace --output-on-failure
```

### Deliverables

- Do not execute `git commit` and do not continue to I8-01-T2.
- Report every modified file and the implemented contract.
- Report exact test commands and results.
- Report every deviation from Allowed Changes or Requirements.
- Report discovered but unhandled issues.

### Acceptance Record

- Accepted commit: this I8-01-T1 checkpoint commit.
- Tests: clean AppleClang Debug ASan/UBSan and GCC 14 Release/Werror builds;
  AppleClang Debug, AppleClang Release, and GCC 14 CTest 33/33 each; Python
  tests 123/123; F2CPP single-frequency cases 52/52; `git diff --check` passed.
- Oracle / parity result: executable `AddArr` parity is not part of T1 and
  remains assigned to I8-04-T2; stored field kinds and the capacity formula
  were source-audited against `ArrMod.f90` and `bellhop.f90`.
- Notes: requirements 1–7 and every acceptance criterion passed. Implementation
  changes stayed within the named model/workspace/test/CMake files, added no
  pressure/cache/global state, and did not modify any I0–I7 or RayReuse path.

## I8-01-T2

### Task ID

`I8-01-T2`

### Status

TODO

### Objective

Implement Origin `AddArr` duplicate grouping, weighted merge, and bounded
strongest-arrival retention inside `ArrivalWorkspace`.

### Background

`ArrMod.f90::AddArr` compares only the most recently stored record. Its
thresholds are strict, its merge intermediates are default REAL, and a full
cell replaces the first minimum-amplitude slot only for a strictly stronger
candidate.

### Depends On

- `I8-01-T1` accepted interfaces.
- `Bellhop_origin/Bellhop/ArrMod.f90::AddArr`.

### Allowed Changes

- `include/bellhop/field/arrival_workspace.hpp`.
- `src/field/arrival_workspace.cpp`.
- `tests/component/arrival_workspace_test.cpp`.
- CMake only if an existing target needs an explicit source adjustment.

### Do Not Modify

- Arrival field names/types or capacity formula from T1.
- Influence, solver, parser, CLI, writer, or standard-case code.
- Existing field workspaces or frozen ray/cache types.
- `Bellhop_RayReuse`, other I8 stages, or unapproved public interfaces.
- Task/progress documents.

### Requirements

1. Add candidates in encounter order and convert to stored float fields at the
   same points as Origin `SNGL`/`CMPLX`.
2. Group only with the last record when both
   `omega * abs(delayDifference) < 0.05F` and
   `abs(phaseDifference) < 0.05F` are true.
3. Reproduce float `AmpTot`, `w1`, and `w2`; update amplitude, complex delay,
   source angle, and receiver angle only. Preserve old phase and bounce counts.
4. If `abs(AmpTot) <= epsilon(float)`, preserve the old record unchanged and
   increment a cusp-guard statistic.
5. At capacity, find the first minimum amplitude. Replace it only for a
   strictly larger candidate; otherwise discard. Do not reorder the cell.
6. Track candidate, append, merge, cusp-guard, weakest-replacement,
   capacity-discard, and saturated-cell counts without affecting numerical
   output.
7. Reject non-finite candidate fields, negative amplitudes, negative bounce
   counts, frequency mismatch, and invalid receiver indices before mutation.

### Acceptance Criteria

- Component tests cover just-below/equal/just-above both strict thresholds.
- Tests prove that a non-last similar record is not merged.
- Weighted merged fields match explicitly computed float intermediates; phase
  and bounces remain unchanged.
- Axial-cusp cancellation leaves the old record byte-for-byte unchanged.
- Full-cell tests prove first-minimum tie selection, stronger replacement,
  equal-amplitude discard, weaker discard, unchanged count/order, and stats.
- Zero-arrival cells remain valid.
- AppleClang Debug sanitizer and GCC 14 Werror tests pass with no regression.

### Suggested Tests

```bash
cmake --build Bellhop_F2CPP/build/debug -j 8 --target bellhop_f2cpp_arrival_workspace_tests
ctest --test-dir Bellhop_F2CPP/build/debug -R f2cpp.component.arrival_workspace --output-on-failure
cmake --build Bellhop_F2CPP/build/gcc14-release -j 8 --target bellhop_f2cpp_arrival_workspace_tests
ctest --test-dir Bellhop_F2CPP/build/gcc14-release -R f2cpp.component.arrival_workspace --output-on-failure
```

### Deliverables

- Do not commit and do not start T3.
- Report modified files, implemented cases, exact tests/results, deviations,
  and unresolved findings.

### Acceptance Record

- Accepted commit:
- Tests:
- Oracle / parity result:
- Notes:

## I8-01-T3

### Task ID

`I8-01-T3`

### Status

TODO

### Objective

Refactor geometric-hat receiver contribution delivery so the same accepted
calculation can target pressure, intensity, or arrivals without numerical drift.

### Background

The current `GeometricHatInfluence` already calculates all arrival fields except
prefix bounce counts. Origin `ApplyContribution` consumes that one calculation
for TL, arrivals, or eigenrays. I8 must not maintain a second receiver walker.

### Depends On

- `I8-01-T2`.
- Accepted I7 geometric-hat component/oracle baseline.
- `RayPath::events` as the authoritative reflection history.

### Allowed Changes

- `include/bellhop/field/geometric_hat_influence.hpp`.
- `src/field/geometric_hat_influence.cpp`.
- `include/bellhop/field/arrival_workspace.hpp` only for the narrow sink API.
- `include/bellhop/model/simulation_case.hpp` and
  `src/model/simulation_case.cpp` only to add distinct ASCII/binary arrival
  run modes and a non-field product classification.
- `tests/component/geometric_hat_influence_test.cpp`.
- `tests/component/arrival_workspace_test.cpp` if shared validation belongs
  there.

### Do Not Modify

- `SingleFrequencySolver`, parser, CLI, writers, or other beam families.
- Geometric-hat formulas, receiver traversal, strict window comparisons,
  REAL4 compatibility constants, or I7 diagnostic meanings.
- `RayPath`, `RayPathCache`, `FrequencyWorkspace`, or `FrequencyProjector`.
- `Bellhop_RayReuse`, other I8 stages, or task/progress documents.

### Requirements

1. Add distinct `AsciiArrivals` and `BinaryArrivals` run-mode values. They are
   not transmission-loss modes, do not use Lloyd mirror, and map to no pressure
   or intensity accumulation kind.
2. Keep one internal contribution traversal for Cartesian `G` and ray-centered
   `g`; select pressure, intensity, or arrival as the output sink.
3. Add an arrivals entry point that requires an arrival run mode and a
   frequency-matching
   `ArrivalWorkspace` and preserves every existing traversal/window condition.
4. Populate candidate amplitude, unwrapped phase radians, complex interpolated
   delay, launch angle in degrees, local receiver declination in degrees, and
   prefix top/bottom bounces corresponding to Origin `ray2D(iS)`.
5. Precompute per-point prefix bounce counts once per ray; do not scan all
   reflection events for every receiver hit.
6. Arrival collection must not touch pressure/intensity storage and must not
   apply point/line writer scaling.
7. Preserve existing I7 diagnostics and bit/ULP results for pressure and
   intensity.

### Acceptance Criteria

- New component tests exercise direct, reflected, multipath, caustic, regular,
  and existing-supported irregular/coordinate combinations.
- Candidate receiver indices, endpoint bounces, source/receiver angles, delay,
  phase, and amplitude match hand-selected Origin-derived expectations.
- At least one adjacent bracketing pair reaches the T2 merge path.
- Existing geometric-hat component tests pass unchanged.
- Frozen I7 geometric-hat/Gaussian-family validators show no pressure drift.
- AppleClang Debug and GCC 14 Werror pass.

### Suggested Tests

```bash
cmake --build Bellhop_F2CPP/build/debug -j 8 --target bellhop_f2cpp_geometric_hat_influence_tests
ctest --test-dir Bellhop_F2CPP/build/debug -R 'f2cpp.component.(geometric_hat_influence|arrival_workspace)' --output-on-failure
cmake --build Bellhop_F2CPP/build/gcc14-release -j 8 --target bellhop_f2cpp_geometric_hat_influence_tests
ctest --test-dir Bellhop_F2CPP/build/gcc14-release -R f2cpp.component.geometric_hat_influence --output-on-failure
/Users/luyiyang/miniconda3/envs/py/bin/python test/standard_cases/codes/standard_cases.py test --version f2cpp --case geometric_hat_cartesian_safe_control --case geometric_hat_ray_centered --profile single --executable Bellhop_F2CPP/build/release/bellhop_f2cpp --results-root /tmp/i8_01_t3_regression
```

### Deliverables

- Do not commit and do not start T4.
- Report modified files, sink refactor, exact tests/results, deviations, and
  unresolved findings.

### Acceptance Record

- Accepted commit:
- Tests:
- Oracle / parity result:
- Notes:

## I8-01-T4

### Task ID

`I8-01-T4`

### Status

TODO

### Objective

Add source-streamed `ArrivalSolver` orchestration for geometric-hat arrival
modes without exposing output files or CLI support.

### Background

Origin clears one arrival grid per source. F2CPP must trace/freeze one source
fan, project each path at the single run frequency, accumulate into one
workspace, pass it to a const consumer, then release it. This mirrors
`RayTraceSolver` and prepares future RayReuse consumption.

### Depends On

- `I8-01-T3`.
- Stable `GeometryTracer`, `RayPathCache`, `FrequencyProjector`, and
  `FrozenSourceRayConsumer` lifecycle patterns.

### Allowed Changes

- New `include/bellhop/solver/arrival_solver.hpp`.
- New `src/solver/arrival_solver.cpp`.
- New `tests/component/arrival_solver_test.cpp`.
- `CMakeLists.txt` for source/test registration.

### Do Not Modify

- ENV parser, CLI, ARR writers, eigenray mode, or ordinary R solver/writer.
- Existing TL solver behavior or `FrequencyWorkspace`.
- Beam formulas or support families beyond `G/g`.
- `Bellhop_RayReuse`, other I8 stages, or task/progress documents.

### Requirements

1. `ArrivalSolver::solve` accepts only the T3 arrival modes and `G/g`, validates the
   existing family/layout restrictions, and requires a nonempty const source
   consumer.
2. Use the accepted launch fan and one `RayPathCache` per source. Require normal
   termination, freeze before consumption, and never mutate the cache later.
3. Project source amplitude, source beam pattern, complex attenuation, and
   boundary reflection state at the run frequency exactly as the TL solver does;
   do not apply semi-coherent Lloyd processing.
4. Allocate/clear one `ArrivalWorkspace` per source, call the geometric-hat
   arrivals sink for every projected path, invoke the consumer in sorted source
   order, then release the source cache/workspace.
5. Return checked trace/project/influence statistics, peak cache/workspace
   bytes, candidate counts, and saturation totals.

### Acceptance Criteria

- Two-source tests observe consumer order `{0,1}`, distinct unmerged source
  grids, complete frozen fans, and released source-local lifetime.
- Point/line and directional beam-pattern amplitudes reach candidates without
  writer scaling.
- Complex-delay imaginary parts and reflection bounces survive projection.
- Wrong run modes, unsupported families/layouts, empty consumer, overflow, and
  abnormal termination fail before a partial consumer call.
- Existing single-frequency and ray-trace solver tests remain green.
- AppleClang Debug sanitizer and GCC 14 Werror pass.

### Suggested Tests

```bash
cmake --build Bellhop_F2CPP/build/debug -j 8 --target bellhop_f2cpp_arrival_solver_tests
ctest --test-dir Bellhop_F2CPP/build/debug -R 'f2cpp.component.(arrival_solver|single_frequency_solver|ray_trace_output)' --output-on-failure
cmake --build Bellhop_F2CPP/build/gcc14-release -j 8 --target bellhop_f2cpp_arrival_solver_tests
ctest --test-dir Bellhop_F2CPP/build/gcc14-release -R f2cpp.component.arrival_solver --output-on-failure
```

### Deliverables

- Do not commit and do not start T5.
- Report modified files, lifecycle/statistics behavior, exact tests/results,
  deviations, and unresolved findings.

### Acceptance Record

- Accepted commit:
- Tests:
- Oracle / parity result:
- Notes:

## I8-01-T5

### Task ID

`I8-01-T5`

### Status

TODO

### Objective

Extend the accepted arrival contribution and solver path to Cartesian geometric
Gaussian `B` without changing Gaussian TL output.

### Background

Origin routes `InfluenceGeoGaussianCart` through the same `ApplyContribution`
arrival call as geometric hat. Its beam window and amplitude formula remain
family-specific, while storage/merge semantics are shared.

### Depends On

- `I8-01-T4`.
- Accepted I7 geometric-Gaussian component and family validator.

### Allowed Changes

- `include/bellhop/field/geometric_gaussian_influence.hpp`.
- `src/field/geometric_gaussian_influence.cpp`.
- `src/solver/arrival_solver.cpp` and its header only for family dispatch.
- `tests/component/geometric_gaussian_influence_test.cpp`.
- `tests/component/arrival_solver_test.cpp`.

### Do Not Modify

- Simple Gaussian or Cerveny influence code.
- Writer, parser, CLI, standard cases, ordinary R/eigenray code.
- Existing B-family receiver/window/numerical formulas or I7 diagnostics.
- `Bellhop_RayReuse`, other I8 stages, or task/progress documents.

### Requirements

1. Reuse one internal B-family traversal for pressure, intensity, and arrivals.
2. Emit the same `ArrivalCandidate` fields and prefix bounce semantics as T3,
   using B-family amplitude/weight and caustic phase exactly.
3. Add `B` dispatch to `ArrivalSolver`; retain explicit rejection of Cerveny
   and simple Gaussian combinations.
4. Preserve source geometry, irregular receiver support, complex delay, and
   directional source pattern behavior already accepted for B TL.

### Acceptance Criteria

- B arrival component tests cover at least one geometric, near-field, and
  wavelength-cap width branch and a reflected/caustic path.
- B candidate fields match Origin-derived expected values within the frozen
  float ULP targets.
- Existing B pressure/intensity diagnostics and I7 Gaussian-family report do
  not drift.
- Arrival solver tests accept B and continue rejecting incomplete families.
- AppleClang Debug sanitizer, Release, and GCC 14 Werror pass relevant tests.

### Suggested Tests

```bash
cmake --build Bellhop_F2CPP/build/debug -j 8 --target bellhop_f2cpp_geometric_gaussian_influence_tests bellhop_f2cpp_arrival_solver_tests
ctest --test-dir Bellhop_F2CPP/build/debug -R 'f2cpp.component.(geometric_gaussian_influence|arrival_solver)' --output-on-failure
cmake --build Bellhop_F2CPP/build/gcc14-release -j 8
ctest --test-dir Bellhop_F2CPP/build/gcc14-release -R 'f2cpp.component.(geometric_gaussian_influence|arrival_solver)' --output-on-failure
```

### Deliverables

- Do not commit and do not continue to I8-02.
- Report modified files, B dispatch/contribution changes, exact tests/results,
  deviations, and unresolved findings.

### Acceptance Record

- Accepted commit:
- Tests:
- Oracle / parity result:
- Notes:
