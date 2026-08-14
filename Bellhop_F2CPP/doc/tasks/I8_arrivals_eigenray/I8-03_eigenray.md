# I8-03 — Eigenray mode

Eigenray mode reuses geometric receiver-hit traversal but produces variable
prefixes of frozen rays. It is not an arrival view and is not ordinary `R`.

## I8-03-T1

### Task ID

`I8-03-T1`

### Status

ACCEPTED

### Objective

Define eigenray-hit records and collect every `G/g` receiver hit from the shared
geometric-hat contribution traversal without writing files.

### Background

Origin `ApplyContribution` writes an eigenray whenever a receiver contribution
passes the family beam-window rule. It writes the ray prefix through `iS`, does
not group duplicates, and may emit multiple records for one launch angle.

### Depends On

- `I8-01-T3` shared geometric-hat contribution sink.
- Accepted I6 RAY prefix/bounce semantics.

### Allowed Changes

- New `include/bellhop/field/eigenray_hit.hpp`.
- `include/bellhop/model/simulation_case.hpp` and
  `src/model/simulation_case.cpp` only to add the eigenray run mode and
  non-field product classification.
- `include/bellhop/field/geometric_hat_influence.hpp`.
- `src/field/geometric_hat_influence.cpp`.
- `tests/component/geometric_hat_influence_test.cpp`.
- A new focused eigenray-hit component test and CMake registration if clearer.

### Do Not Modify

- Arrival workspace/merge semantics, solver, parser, CLI, or writers.
- Pressure/intensity/arrival numerical output.
- Ordinary `RayTraceSolver` / `RayWriter`.
- `Bellhop_RayReuse`, B/simple/Cerveny families, other I8 stages, or task docs.

### Requirements

1. Add an `Eigenray` run-mode value. It is not TL, ordinary ray trace, pressure,
   intensity, or arrival accumulation.
2. Define an immutable `EigenrayHit` with receiver depth/range indices and an
   exclusive prefix point count. It must not own/copy a `RayPath`.
3. Add a hit sink requiring Eigenray mode to the same `G/g` traversal used by
   pressure/intensity/arrivals. Emit once for every accepted contribution and
   preserve traversal order.
4. Set prefix count to `rightPointIndex + 1`, require `2 <= count <= active
   frequency prefix`, and retain distinct hits even when launch angle, receiver,
   or endpoint repeat.
5. Do not impose arrival duplicate/capacity logic or exact-centerline root
   finding.
6. Hit collection must be frequency-aware through the projected active prefix
   but must not mutate the path or projected state.

### Acceptance Criteria

- Tests show one ray can emit zero, one, and multiple ordered hits.
- Repeated launch/endpoint hits remain repeated; no deduplication occurs.
- Every hit corresponds exactly to an existing G/g contribution and uses the
  expected receiver indices and prefix endpoint.
- Inactive suffixes cannot produce hits.
- Existing pressure, intensity, arrival, and I7 geometric-hat tests do not
  drift.
- AppleClang Debug sanitizer and GCC 14 Werror pass.

### Suggested Tests

```bash
cmake --build Bellhop_F2CPP/build/debug -j 8 --target bellhop_f2cpp_geometric_hat_influence_tests
ctest --test-dir Bellhop_F2CPP/build/debug -R f2cpp.component.geometric_hat_influence --output-on-failure
cmake --build Bellhop_F2CPP/build/gcc14-release -j 8 --target bellhop_f2cpp_geometric_hat_influence_tests
ctest --test-dir Bellhop_F2CPP/build/gcc14-release -R f2cpp.component.geometric_hat_influence --output-on-failure
```

### Deliverables

- Do not execute `git commit` and do not continue to T2.
- Report modified files, hit ordering/prefix rules, exact tests/results,
  deviations, and unresolved findings.

### Acceptance Record

- Accepted commit: this I8-03 core checkpoint commit.
- Tests: AppleClang Debug ASan/UBSan full CTest and focused GCC 14 Werror
  geometric-hat/eigenray tests passed; zero, one, multiple, repeated and
  inactive-suffix hits are covered; `git diff --check` passed.
- Oracle / parity result: hit selection and exclusive prefix endpoints were
  source-audited against `influence.f90::ApplyContribution`; product parity is
  completed by I8-04-T4.
- Notes: the immutable hit carries only receiver indices and prefix size; its
  frozen path/cache lifetime remains owned by the synchronous solver callback.

## I8-03-T2

### Task ID

`I8-03-T2`

### Status

ACCEPTED

### Objective

Extend shared eigenray-hit delivery to Cartesian geometric Gaussian `B` without
changing B pressure, intensity, or arrival results.

### Background

Origin routes B contributions through `ApplyContribution` for E exactly as it
does for G, but B retains its own beam-width/window calculation.

### Depends On

- `I8-03-T1`.
- `I8-01-T5` B arrival sink.

### Allowed Changes

- `include/bellhop/field/geometric_gaussian_influence.hpp`.
- `src/field/geometric_gaussian_influence.cpp`.
- `tests/component/geometric_gaussian_influence_test.cpp`.

### Do Not Modify

- Geometric-hat, simple-Gaussian, or Cerveny code.
- Solvers, parser, CLI, writers, arrival model, or standard cases.
- Accepted B formula/window/diagnostic behavior.
- `Bellhop_RayReuse`, other I8 stages, or task/progress documents.

### Requirements

1. Add the T1 hit sink to B's one shared contribution traversal.
2. Emit the same receiver indices/prefix convention for every accepted B
   contribution, with no grouping or capacity.
3. Respect active prefix, irregular receiver coordinates, and strict B window
   inequalities.
4. Preserve all I7 and I8-01 B numerical outputs.

### Acceptance Criteria

- B tests cover zero/one/multiple hits across geometric, near-field, and
  wavelength-cap branches.
- Hit order and endpoints correspond to B contribution diagnostics.
- Existing B pressure/intensity/arrival tests and I7 Gaussian-family validator
  show no drift.
- AppleClang Debug and GCC 14 Werror pass.

### Suggested Tests

```bash
cmake --build Bellhop_F2CPP/build/debug -j 8 --target bellhop_f2cpp_geometric_gaussian_influence_tests
ctest --test-dir Bellhop_F2CPP/build/debug -R f2cpp.component.geometric_gaussian_influence --output-on-failure
cmake --build Bellhop_F2CPP/build/gcc14-release -j 8 --target bellhop_f2cpp_geometric_gaussian_influence_tests
ctest --test-dir Bellhop_F2CPP/build/gcc14-release -R f2cpp.component.geometric_gaussian_influence --output-on-failure
```

### Deliverables

- Do not commit and do not continue to T3.
- Report modified files, B hit behavior, exact tests/results, deviations, and
  unresolved findings.

### Acceptance Record

- Accepted commit: same I8-03 core checkpoint as T1/T3/T4.
- Tests: B component tests passed on Debug sanitizer and GCC 14 Werror for
  geometric, near-field and wavelength-cap widths, zero/multiple hits,
  irregular receivers, ordering and active-prefix cutoff; arrival/field
  regressions passed.
- Oracle / parity result: B uses its accepted contribution window and the same
  Origin `ApplyContribution` endpoint; fresh I7 Gaussian validation is part of
  final I8 regression closure.
- Notes: no pressure, intensity or arrival formula was duplicated or changed.

## I8-03-T3

### Task ID

`I8-03-T3`

### Status

ACCEPTED

### Objective

Add a source-streamed, frequency-aware `EigenraySolver` that emits ordered path
references and prefix hits for safe `G/g/B` families.

### Background

E shares trace and projection with arrivals but not arrival storage. The
consumer must see each hit while its source cache is alive, in source/launch/
contribution order.

### Depends On

- `I8-03-T1` and `I8-03-T2`.
- `I8-01-T4` source-streamed projection lifecycle.

### Allowed Changes

- New `include/bellhop/solver/eigenray_solver.hpp`.
- New `src/solver/eigenray_solver.cpp`.
- New `tests/component/eigenray_solver_test.cpp`.
- CMake registration.

### Do Not Modify

- Parser, CLI, RAY writer, ordinary ray tracer, arrival writer/accumulator.
- Beam contribution formulas or accepted run modes.
- `Bellhop_RayReuse`, other I8 stages, or task/progress documents.

### Requirements

1. `EigenraySolver` accepts only `Eigenray` mode and safe `G/g/B` family/layout
   combinations; require a nonempty synchronous consumer.
2. Reuse one source-local frozen `RayPathCache`, frequency projection, source
   amplitude/pattern, and active-prefix behavior from `ArrivalSolver`.
3. For each path, collect hits and call the consumer immediately in encounter
   order with source index, launch index/path reference, and `EigenrayHit`.
4. The consumer contract is valid only during the call and cannot retain
   mutable access. The cache remains frozen.
5. Return checked trace/project/influence/consumer timings, total launch rays,
   total hits, total candidate prefix points, and peak cache bytes.
6. Zero-hit runs are successful and still process every configured launch ray.

### Acceptance Criteria

- Multi-source tests prove source-major then launch-major then hit order.
- Total hits may be zero or differ from configured launch count; repeated launch
  angles are retained.
- Every path is frozen and every prefix is within the active point count.
- Wrong mode/family, empty consumer, abnormal termination, and arithmetic
  overflow fail without a later consumer call.
- Arrival and ordinary R solver tests remain green.
- AppleClang Debug sanitizer and GCC 14 Werror pass.

### Suggested Tests

```bash
cmake --build Bellhop_F2CPP/build/debug -j 8 --target bellhop_f2cpp_eigenray_solver_tests
ctest --test-dir Bellhop_F2CPP/build/debug -R 'f2cpp.component.(eigenray_solver|arrival_solver|ray_trace_output)' --output-on-failure
cmake --build Bellhop_F2CPP/build/gcc14-release -j 8 --target bellhop_f2cpp_eigenray_solver_tests
ctest --test-dir Bellhop_F2CPP/build/gcc14-release -R f2cpp.component.eigenray_solver --output-on-failure
```

### Deliverables

- Do not commit and do not continue to T4.
- Report modified files, consumer order/statistics, exact tests/results,
  deviations, and unresolved findings.

### Acceptance Record

- Accepted commit: same I8-03 core checkpoint as T1/T2/T4.
- Tests: multi-source G and B solver tests passed on Debug sanitizer and GCC 14
  Werror; callbacks prove frozen-cache identity, source/launch/hit order,
  checked statistics, wrong-mode/family and empty-consumer rejection.
- Oracle / parity result: tracing, projection and G/g/B dispatch reuse the
  accepted ArrivalSolver lifecycle; end-to-end E parity remains I8-04-T4.
- Notes: each synchronous callback receives the frozen source cache so the
  writer can validate path identity without retaining mutable state.

## I8-03-T4

### Task ID

`I8-03-T4`

### Status

ACCEPTED

### Objective

Implement atomic eigenray `.ray` writing for variable ray prefixes while
preserving ordinary `R` bytes and behavior.

### Background

Origin uses `WriteRay2D(alpha, iS)` for E. The header is the ordinary RAY header
and still declares the configured launch fan. Each subsequent block is a
selected prefix, and block count is implicit at EOF.

### Depends On

- `I8-03-T3`.
- Accepted `RayWriter` point compression and output safety.
- `Bellhop_origin/Bellhop/WriteRay.f90`.

### Allowed Changes

- `include/bellhop/io/ray_writer.hpp` and `src/io/ray_writer.cpp` for a shared
  private prefix writer that preserves R output.
- New `include/bellhop/io/eigenray_writer.hpp` and
  `src/io/eigenray_writer.cpp`, or an equivalently narrow public split.
- `tests/component/ray_trace_output_test.cpp`.
- New `tests/component/eigenray_writer_test.cpp`.
- CMake registration.

### Do Not Modify

- RAY header fields, ordinary R block order/bytes, tracer, hit selection,
  arrivals, parser, or CLI.
- Add an eigenray block count or receiver metadata to `.ray`.
- `Bellhop_RayReuse`, other I8 stages, or task/progress documents.

### Requirements

1. Constructor requires Eigenray mode, writes the exact existing 2-D RAY header
   to `.tmp`, including configured source and launch counts.
2. `appendHit` validates source/launch order, frozen path identity, and prefix
   count, then writes launch angle, compressed prefix point count, prefix
   top/bottom bounce counts, and prefix coordinates.
3. Reuse exactly one point-index/compression implementation for R full paths and
   E prefixes. Preserve incident/reflected duplicate points and terminal point.
4. Count only reflection events visible at the prefix endpoint.
5. Allow zero blocks and repeated launch-angle blocks. Finalize at EOF after the
   source stream completes; do not compare block count to header Nalpha.
6. Preserve atomic publication/failure cleanup and all ordinary R output.

### Acceptance Criteria

- Independent reader parses E blocks to EOF and validates zero, one, multiple,
  and repeated-angle blocks.
- Prefix point/bounce counts and terminal coordinates match synthetic paths,
  including a cutoff before and after a reflection pair.
- Header launch count deliberately differs from actual block count in a test.
- Existing ordinary R fixture remains semantically and byte stable.
- Incomplete/out-of-order/invalid-prefix writes fail without publication and
  preserve an old file.
- AppleClang Debug sanitizer and GCC 14 Werror pass.

### Suggested Tests

```bash
cmake --build Bellhop_F2CPP/build/debug -j 8 --target bellhop_f2cpp_eigenray_writer_tests bellhop_f2cpp_ray_trace_output_tests
ctest --test-dir Bellhop_F2CPP/build/debug -R 'f2cpp.component.(eigenray_writer|ray_trace_output)' --output-on-failure
cmake --build Bellhop_F2CPP/build/gcc14-release -j 8
ctest --test-dir Bellhop_F2CPP/build/gcc14-release -R 'f2cpp.component.(eigenray_writer|ray_trace_output)' --output-on-failure
```

### Deliverables

- Do not commit and do not continue to T5.
- Report modified files, shared compression/prefix behavior, exact tests/results,
  deviations, and unresolved findings.

### Acceptance Record

- Accepted commit: same I8-03 core checkpoint as T1/T2/T3.
- Tests: eigenray writer, ordinary R writer and reader regressions passed on
  Debug sanitizer and GCC 14 Werror; tests cover zero/repeated/variable blocks,
  prefix bounce visibility, invalid order/prefix and atomic cleanup.
- Oracle / parity result: shared compression and prefix bounce semantics were
  source-audited against `WriteRay.f90::WriteRay2D`; EOF product parity remains
  I8-04-T4.
- Notes: ordinary R and E share one prefix encoder; E deliberately writes no
  block-count extension and may finalize a header-only zero-hit stream.

## I8-03-T5

### Task ID

`I8-03-T5`

### Status

TODO

### Objective

Expose safe `E` run types through parser, PRT, CLI, and full SHD/RAY/ARR product
lifecycle.

### Background

This is the only task that makes E user-visible. E outputs `.ray` like R but
runs projected geometric influence and reports variable eigenray hit counts.

### Depends On

- `I8-03-T4`.
- `I8-02-T3` complete three-product lifecycle.

### Allowed Changes

- `src/io/environment_parser.cpp` and parser tests.
- `app/main.cpp`.
- Eigenray solver/writer only for integration defects.
- CLI fixtures and `tests/cli_output_lifecycle.cmake`.
- CMake integration.

### Do Not Modify

- Ordinary R semantics or accepted A/a/TL behavior.
- Arrival accumulator/writers, SHD format, beam formulas, or standard-case
  framework.
- Accept incomplete Cerveny/simple-Gaussian, 3D, N×2D, or shifted E modes.
- `Bellhop_RayReuse`, I8-04, or task/progress documents.

### Requirements

1. Parse `E` only with safe `G/g/B` combinations and existing family/layout
   restrictions. Emit precise explicit errors for incomplete combinations.
2. Do not consume Cerveny beam-tail records for geometric E.
3. PRT identifies eigenray mode/family and reports configured launch rays,
   actual hit blocks, prefix points, cache bytes, timings, and zero-hit success.
4. Main streams `EigenraySolver` hits into `EigenrayWriter` and publishes only
   after complete success.
5. On successful E, retain `.ray` and remove incompatible `.shd`/`.arr`; on
   failure preserve all prior valid products and remove temporary files.
6. Distinguish E success text from ordinary R success text.

### Acceptance Criteria

- Parser accepts representative `EG`, `Eg`, and `EB`; rejects `EC`, `ER`, `ES`,
  3D/N×2D/shift, and family-invalid irregular cases.
- CLI nonzero-hit and zero-hit E fixtures produce valid EOF-parsed `.ray` files,
  no ARR/SHD, no temp, and correct PRT totals.
- `CC -> R -> A -> E -> a -> CC` lifecycle leaves only the current expected
  product and preserves prior products on injected failures.
- Ordinary R reader still uses configured fixed block count; E reader uses EOF.
- All parser/output CTests pass on AppleClang Debug/Release and GCC 14 Werror.

### Suggested Tests

```bash
cmake --build Bellhop_F2CPP/build/debug -j 8
ctest --test-dir Bellhop_F2CPP/build/debug -R 'f2cpp.(component.environment_parser|cli.output_lifecycle|component.eigenray)' --output-on-failure
cmake --build Bellhop_F2CPP/build/release -j 8
ctest --test-dir Bellhop_F2CPP/build/release --output-on-failure
cmake --build Bellhop_F2CPP/build/gcc14-release -j 8
ctest --test-dir Bellhop_F2CPP/build/gcc14-release --output-on-failure
```

### Deliverables

- Do not commit and do not continue to I8-04.
- Report modified files, grammar/lifecycle behavior, exact tests/results,
  deviations, and unresolved findings.

### Acceptance Record

- Accepted commit:
- Tests:
- Oracle / parity result:
- Notes:
