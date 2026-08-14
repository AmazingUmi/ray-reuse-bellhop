# I8-02 — Arrival writers

This stage serializes accepted source-local arrival workspaces and exposes
`A/a` only after both encodings and output lifecycle are complete.

## I8-02-T1

### Task ID

`I8-02-T1`

### Status

TODO

### Objective

Implement checked 2-D ARR layout planning and atomic ASCII `A` writing for a
complete source stream.

### Background

Origin writes one common header followed by one complete body per sorted source.
Body cells are depth-major/range-minor for rectilinear grids and one cell per
range for irregular grids. Source-geometry amplitude scaling happens only in
the writer.

### Depends On

- `I8-01-T4` (and T5 before B is used end to end).
- Existing `Shd2DLayout`, `ShdWriter`, and `RayWriter` checked/atomic patterns.
- `ArrMod.f90::WriteArrivalsASCII` and
  `ReadEnvironmentBell.f90::OpenOutputFiles`.

### Allowed Changes

- `include/bellhop/io/output_layout.hpp` and `src/io/output_layout.cpp`.
- New `include/bellhop/io/arrival_writer.hpp`.
- New `src/io/arrival_writer.cpp`.
- New `tests/component/arrival_writer_test.cpp`.
- `CMakeLists.txt` for source/test registration.

### Do Not Modify

- Binary ARR support, parser, CLI, solver, influence, or standard-case tools.
- Existing SHD/RAY bytes or lifecycle.
- Stored arrival values inside the source workspace.
- `Bellhop_RayReuse`, I8-03/I8-04, or task/progress documents.

### Requirements

1. Add `Arrival2DLayout` with checked source/header counts, actual cells per
   source, per-cell capacity, maximum record/line sizes, and conservative final
   file-size bounds. Validate int32 and stream limits before opening output.
2. `ArrivalWriter` construction requires `AsciiArrivals`, opens only
   `<output>.tmp`, and writes quoted `2D`, frequency, all sorted source depths,
   all receiver depths, and all receiver ranges in Origin order.
3. `appendSource(index, workspace)` accepts each source exactly once in
   ascending order; metadata/capacity must match the simulation.
4. Write per-source maximum cell count, then every actual cell count and record
   in depth-major/range-minor order. Write explicit zeros for empty cells.
5. Scale a copied amplitude at output: line source `4*sqrt(pi)`; point source
   `1/sqrt(range_m)` and exactly `1e5` at zero range. Convert phase radians to
   degrees using the Origin float conversion order.
6. Emit the eight ASCII fields defined in README with enough precision to
   round-trip every float stored field. Do not sort records.
7. `finalize()` requires all sources, closes successfully, then atomically
   publishes. Destructor/failure removes only the temporary file and preserves
   an existing valid ARR product.

### Acceptance Criteria

- Component reader round-trips one/multiple/zero arrivals, complex delay,
  unwrapped phase, angles, and bounces.
- Exact tests prove source-major and depth-major/range-minor order, including
  irregular one-cell-per-range bodies with full header vectors.
- Point, zero-range point, and line scaling match explicit Origin float
  calculations; the workspace is byte-for-byte unchanged after writing.
- Missing, duplicate, out-of-order, mismatched, and incomplete source streams
  fail without publishing and leak no `.tmp`.
- Existing valid ARR is preserved on injected write/finalize failure.
- AppleClang Debug sanitizer and GCC 14 Werror pass; SHD/RAY writer tests do not
  regress.

### Suggested Tests

```bash
cmake --build Bellhop_F2CPP/build/debug -j 8 --target bellhop_f2cpp_arrival_writer_tests
ctest --test-dir Bellhop_F2CPP/build/debug -R 'f2cpp.component.(arrival_writer|shd_writer|ray_trace_output)' --output-on-failure
cmake --build Bellhop_F2CPP/build/gcc14-release -j 8 --target bellhop_f2cpp_arrival_writer_tests
ctest --test-dir Bellhop_F2CPP/build/gcc14-release -R f2cpp.component.arrival_writer --output-on-failure
```

### Deliverables

- Do not execute `git commit` and do not continue to T2.
- Report modified files, layout/writer behavior, exact test commands/results,
  deviations, and unresolved findings.

### Acceptance Record

- Accepted commit:
- Tests:
- Oracle / parity result:
- Notes:
## I8-02-T2

### Task ID

`I8-02-T2`

### Status

TODO

### Objective

Add GNU Fortran-compatible sequential-unformatted binary `a` output to the
accepted arrival writer.

### Background

The project oracle uses Homebrew GNU Fortran. Its binary `.arr` is a sequence of
little-endian records with signed 32-bit byte-count markers. Header arrays use
mixed kinds and each arrival is one 32-byte record of eight float32 values.

### Depends On

- `I8-02-T1`.
- README binary layout decision.
- `ArrMod.f90::WriteArrivalsBinary` and the current gfortran executable ABI.

### Allowed Changes

- `include/bellhop/io/arrival_writer.hpp`.
- `src/io/arrival_writer.cpp`.
- `include/bellhop/io/output_layout.hpp` and `src/io/output_layout.cpp` only for
  binary record/file bounds.
- `tests/component/arrival_writer_test.cpp`.
- A small internal binary-record helper under `src/io` if it is private to ARR.

### Do Not Modify

- ASCII semantics accepted in T1.
- Parser, CLI, solver, influence, standard cases, SHD/RAY formats.
- Add a portable/new binary format, flags, checksums, padding, or version field.
- `Bellhop_RayReuse`, other I8 stages, or task/progress documents.

### Requirements

1. Select binary encoding only for `BinaryArrivals`; reject encoding/mode
   mismatch.
2. Write every record with equal int32 prefix/suffix lengths, explicit little-
   endian scalar encoding, checked payload arithmetic, and no native-struct
   dumps.
3. Match header payload kinds exactly: four-character `'2D'`, float32
   frequency, int32+float32 source depths, int32+float32 receiver depths, and
   int32+float64 receiver ranges.
4. Match body records exactly: int32 source maximum, int32 cell count, then one
   32-byte eight-float record per arrival. Encode bounce counts as float32 only
   after proving exact representation for accepted int32 values.
5. Use the same record order and amplitude/phase scaling functions as ASCII.
6. Preserve T1 atomic publication and failure behavior.

### Acceptance Criteria

- An independent test decoder validates every marker, payload size, endian
  order, scalar kind, field value, source/cell order, and EOF.
- Known small fixtures match a checked byte vector, including zero-arrival and
  multi-source records.
- Parsed ASCII and binary products from the same synthetic workspace are
  semantically identical in every header and arrival field.
- Overflow/non-exact bounce conversion and oversized record/file plans fail
  before publication.
- ASCII tests and existing writers remain unchanged and green.
- AppleClang Debug sanitizer and GCC 14 Werror pass.

### Suggested Tests

```bash
cmake --build Bellhop_F2CPP/build/debug -j 8 --target bellhop_f2cpp_arrival_writer_tests
ctest --test-dir Bellhop_F2CPP/build/debug -R f2cpp.component.arrival_writer --output-on-failure
cmake --build Bellhop_F2CPP/build/gcc14-release -j 8 --target bellhop_f2cpp_arrival_writer_tests
ctest --test-dir Bellhop_F2CPP/build/gcc14-release -R f2cpp.component.arrival_writer --output-on-failure
```

### Deliverables

- Do not commit and do not continue to T3.
- Report modified files, binary ABI details, exact tests/results, deviations,
  and unresolved findings.

### Acceptance Record

- Accepted commit:
- Tests:
- Oracle / parity result:
- Notes:

## I8-02-T3

### Task ID

`I8-02-T3`

### Status

TODO

### Objective

Expose safe `A/a` run types through ENV parsing, PRT, and CLI with complete
cross-product lifecycle and saturation reporting.

### Background

The library model/solver/writers already exist after I8-01 and T1/T2. This task
is the first point at which `A/a` becomes user-visible. A failed invocation must
not destroy a previously valid SHD/RAY/ARR product.

### Depends On

- `I8-01-T5`.
- `I8-02-T1` and `I8-02-T2`.
- I6-05 CLI atomic-output contract.

### Allowed Changes

- `src/io/environment_parser.cpp` and parser tests.
- `app/main.cpp`.
- `model/simulation_case.hpp/.cpp` only for validation corrections required by
  the already accepted arrival enum values.
- Arrival solver/writer files only for integration defects, not redesign.
- `tests/fixtures` and `tests/cli_output_lifecycle.cmake`.
- `CMakeLists.txt` as needed for integration tests.

### Do Not Modify

- Eigenray support or ordinary `R` semantics.
- TL numerical code, beam formulas, SHD/RAY formats, or accepted parser options.
- Accept Cerveny `C/R`, simple Gaussian `S`, 3D, N×2D, or beam shift for A/a.
- `Bellhop_RayReuse`, standard-case framework (I8-04), or task/progress docs.

### Requirements

1. Parse first run-type character `A` and `a` into the distinct modes. Accept
   only complete Origin-safe `G/g/B` combinations and existing family-specific
   receiver rules; reject incomplete families with a precise error.
2. Do not consume Cerveny `MS`/image records for geometric arrival modes.
3. PRT identifies ASCII/binary arrivals, family, source geometry, receiver
   layout, logical/per-cell capacity, candidate/merge/replacement/discard totals,
   ray/cache/workspace sizes, timings, and successful completion.
4. Main dispatches `ArrivalSolver` into `ArrivalWriter`, appends one workspace
   per source, and finalizes only after complete success.
5. Manage `.shd`, `.ray`, `.arr`, and all corresponding `.tmp` files. Remove
   stale incompatible final products only after the new product is published.
6. On parse/solve/write failure, preserve all prior valid final products,
   remove new temporary files, and record `FATAL ERROR` in PRT.
7. Update CLI help text to name SHD, RAY, and ARR products.

### Acceptance Criteria

- Parser tests accept representative `AG`, `aG`, `Ag`, and `aB` point/line and
  supported receiver forms and reject `AC`, `AR`, `AS`, shifted/3D/N×2D forms.
- CLI A and a fixtures produce nonempty `.arr`, PRT, no SHD/RAY, and no temp.
- A mode and a mode are distinguishable by independent file readers.
- A sequence `CC -> R -> A -> a -> CC` on one root leaves only the expected
  current product at each successful step.
- Injected parse and write failures preserve the previous valid product hashes.
- Existing output-lifecycle, parser, and all 32 baseline CTests pass.
- AppleClang Debug ASan/UBSan, AppleClang Release, and GCC 14 Werror pass.

### Suggested Tests

```bash
cmake --build Bellhop_F2CPP/build/debug -j 8
ctest --test-dir Bellhop_F2CPP/build/debug -R 'f2cpp.(component.environment_parser|cli.output_lifecycle|component.arrival)' --output-on-failure
cmake --build Bellhop_F2CPP/build/release -j 8
ctest --test-dir Bellhop_F2CPP/build/release --output-on-failure
cmake --build Bellhop_F2CPP/build/gcc14-release -j 8
ctest --test-dir Bellhop_F2CPP/build/gcc14-release --output-on-failure
```

### Deliverables

- Do not commit and do not continue to I8-03.
- Report modified files, accepted/rejected grammar, lifecycle behavior, exact
  tests/results, deviations, and unresolved findings.

### Acceptance Record

- Accepted commit:
- Tests:
- Oracle / parity result:
- Notes:
