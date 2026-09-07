# IGR-3B — Arrival Fused Influence + Broadband Arrival Layout — WORKLIST

> **Status:** ACCEPTED / CLOSED (2026-09-04)
> **Branch:** `feat/igr-influence-geometry-reuse`
> **Baseline:** HEAD `dda1c2c`, clean tree
> **Authority:** `IGR-3_SCOPE_AND_ARCHITECTURE_DECISION.md`,
> `DESIGN_IGR3_UNIFIED_INFLUENCE_ARCHITECTURE.md`, and the accepted IGR-3A
> unified executor architecture.

## Frozen decisions

- Scope is exactly `G/g/B × A/a`; `R/E` are regression boundaries only.
- Extend the IGR-3A compile-time unified executor with an Arrival sink; no
  separate fused solver loop.
- Broadband lanes use logical `[range][depth][frequency]` order with one
  ordered `vector<Arrival>` per lane. No pool/slab without memory evidence.
- One source workspace is resident at a time. Static workers exclusively own
  `[rangeBegin, rangeEnd) × all depths × all frequencies`; no atomics/locks in
  Arrival accumulation.
- AddArr is physically single-sourced and preserves the exact legacy
  last-only comparison, float32 arithmetic, cusp, first-weakest,
  strict-replacement, and encounter-order rules.
- `frequencyView` is zero/minimal-copy and is consumed directly by one
  stateful writer per frequency. All temporary files complete before the
  output set is transactionally published.
- Fused Arrival has a separate eligibility predicate so multisource source
  streaming is legal without widening TL fused scope.

### B01 [ADVANCED] Broadband/FusedArrivalWorkspace
Status: DONE
Reviewer: PASS (independent B01 review, 2026-09-04)

Acceptance:
- Checked `[R][D][F]` lane indexing and shared Origin capacity plan.
- Ordered variable-length Arrival lane per cell/frequency.
- Zero-copy frequency view with legacy-compatible cell traversal.
- Source-local memory/statistics accounting; no `Nf` legacy workspace copies.

Evidence:
- Added source-local `BroadbandArrivalWorkspace` with checked
  `((rangeIndex * depthCount) + depthIndex) * frequencyCount + frequencyIndex`
  indexing, a single shared
  Origin capacity plan per receiver grid, ordered `vector<Arrival>` lanes,
  and allocation/logical-capacity accounting.
- `FrequencyView` aliases the fused lanes while preserving the legacy
  depth-major `cellAt`/`arrivalsAt` traversal and frequency/dimension metadata;
  no per-frequency `ArrivalWorkspace` materialization is present.
- `uv run ctest --test-dir Bellhop_RayReuse/build/igr3b-b01 -R
  '^rayreuse\\.component\\.(broadband_)?arrival_workspace$'
  --output-on-failure` — PASS (2/2); `git diff --check` — PASS.

### B02 [ADVANCED] Shared AddArr primitive
Status: DONE
Reviewer: PASS after one validation-precedence remediation (2026-09-04)

Acceptance:
- Legacy and fused lanes call one implementation.
- Exact 0.05F delay/phase, float32 merge arithmetic, cusp, first weakest,
  strict stronger replacement, encounter order, and no reorder.
- Focused merge/no-merge/capacity/encounter-order tests.

Evidence:
- Added `arrival_accumulator.{hpp,cpp}` as the single lane-level AddArr
  implementation with caller-owned `ArrivalAccumulationStatistics` and an
  explicit merge helper for later range-worker aggregation.
- `ArrivalWorkspace::addCandidate` and
  `BroadbandArrivalWorkspace::addCandidate` both forward to the primitive;
  no second merge/capacity implementation remains.
- Focused tests cover merge, cusp return-before-merge, strict no-merge,
  first-weakest in-place replacement, strict stronger comparison, and
  preserved encounter order.
- Coordinator build succeeded; focused workspace CTest 2/2 passed;
  `git diff --check` passed.
- Reviewer remediation restored legacy exception precedence: the shared
  candidate validator runs after frequency matching but before receiver lane
  lookup. A combined invalid-candidate/out-of-range regression locks this
  behavior without duplicating AddArr or validation logic; focused workspace
  CTest remains PASS (2/2), and `git diff --check` remains PASS.

### B03 [ADVANCED] Arrival sink and G/g/B fused kernels
Status: DONE
Reviewer: PASS (independent B03 review, 2026-09-04)

Acceptance:
- Arrival sink uses the unified executor for Hat Cartesian, Hat ray-centered,
  and Geometric Gaussian.
- Fixed `(source, frequency, receiver cell)` encounter order matches legacy.
- Gaussian sigma/window eligibility remains frequency-local.
- Per-cell counts and stored ordered Arrival bytes match legacy reuse.

Evidence:
- Extended the IGR-3A compile-time unified executor with an `ArrivalFusedSink`
  and a source-index-aware raw accumulation seam; existing TL entries retain
  source index 0 and their public API/behavior.
- Hat Cartesian, Hat ray-centered, and Geometric Gaussian reuse their existing
  fused geometry traversals and emit `ArrivalCandidate` directly at the same
  receiver evaluation points into `BroadbandArrivalWorkspace`; Gaussian
  sigma/window gates remain frequency-local and AddArr receives worker-local
  statistics.
- `rayreuse.component.fused_arrival_parity` compares every receiver cell with
  legacy reuse for `G/A`, `g/a`, `B/A`, and `B/a` across source indices 0/1:
  counts and ordered `Arrival` record bytes are identical; the frozen cache
  fingerprint is unchanged.
- Targeted build succeeded. Focused CTest passed 4/4:
  `fused_solver`, `fused_hat_parity`, `fused_geometric_gaussian_parity`, and
  `fused_arrival_parity`; `git diff --check` passed.

### B04 [ADVANCED] Static range parallelism
Status: DONE
Reviewer: PASS (independent B04 review, 2026-09-04)

Acceptance:
- Existing contiguous quotient/remainder range partition and executor worker
  lifecycle are reused.
- Worker-local Arrival statistics merge after join; hot loop has no
  atomics/mutexes.
- Workers 1 and 4 are bitwise identical; cache fingerprint is unchanged.

Evidence:
- Arrival reuses the IGR-3A contiguous quotient/remainder range partition.
  Each worker owns its range block across every depth/frequency lane, keeps a
  local `ArrivalAccumulationStatistics`, and merges statistics only after
  join; the Arrival workspace/kernel/executor hot path contains no
  atomic/mutex/lock update.
- Focused `fused_arrival_parity` runs requested workers 1 and 4 for `G/A`,
  `g/a`, `B/A`, and `B/a` across both source indices of a multi-source case.
  Both worker configurations match legacy reuse per-cell count and ordered
  `Arrival` bytes, match each other bitwise, report the expected effective
  worker count, merge identical statistics, and preserve cache fingerprints.
- Result checks cover raw-seam timing shape, frozen-cache metrics, and
  source-local broadband workspace memory accounting.
- Targeted build succeeded; focused `fused_solver` +
  `fused_arrival_parity` CTest passed 2/2; `git diff --check` passed.

### B05 [ADVANCED] Source-streamed Arrival writers
Status: DONE
Reviewer: PASS after transactional-cleanup remediation (2026-09-04)

Acceptance:
- One writer per frequency: begin/header, appendSource in source order,
  finalize temp, coordinated publication.
- One all-frequency source workspace is released before the next source.
- Frequency views are consumed directly; no conversion to `Nf`
  `ArrivalWorkspace` objects.
- ASCII and binary ARR bytes/naming remain identical; failure leaves no
  partial final products or `.tmp` files.

Evidence:
- `ArrivalWriter` now has a split header/append/complete/publish lifecycle and
  consumes `BroadbandArrivalWorkspace::FrequencyView` directly. The
  `BroadbandArrivalWriterSet` owns one writer per frequency, appends each
  source workspace across frequencies in ascending order, and completes every
  temporary before coordinated publication; it never materializes per-source
  `Nf` legacy workspaces.
- `FusedRayReuseSolver::solveArrivalStreaming` traces and accumulates one
  source at a time, invokes the consumer synchronously in source order, and
  releases the source-local cache/workspace before advancing.
- `multi_source_writer_test` constructs equivalent two-source/two-frequency
  legacy and broadband fixtures and proves full-file ASCII and binary ARR byte
  identity for every frequency. It also verifies callback source order and
  that each frequency view aliases the corresponding `[R][D][F]` lane.
- A forced publication failure using an existing non-regular second target
  proves that a staged pre-existing first final is restored and no `.tmp` or
  `.rayreuse-backup` artifacts remain.
- `uv run cmake --build Bellhop_RayReuse/build/igr3b-b01 --target
  bellhop_rayreuse_multi_source_writer_tests --parallel` — PASS;
  `uv run ctest --test-dir Bellhop_RayReuse/build/igr3b-b01 -R
  '^rayreuse\\.component\\.multi_source_writer$' --output-on-failure` — PASS
  (1/1).
- Reviewer remediation: constructor failure after opening the temporary now
  closes/removes it before rethrow; coordinated rollback checks and reports
  final removal, backup restoration, and temporary cleanup failures instead
  of silently ignoring them. Narrow opt-in test hooks deterministically inject
  post-open and post-first-publish failures without changing normal output.
- The post-first-publish regression restores both pre-existing frequency
  finals byte-for-byte and leaves no `.tmp`/`.rayreuse-backup`; the constructor
  regression leaves neither a final nor a temporary. Targeted rebuild and
  `rayreuse.component.multi_source_writer` CTest remain PASS (1/1, 1.62 s).

### B06 [STANDARD] CLI/routing and focused validation
Status: DONE
Reviewer: PASS (independent B06 review, 2026-09-04)

Acceptance:
- Fused routes exactly legal `G/g/B × A/a`, including multisource; legacy
  nonreuse/reuse/parallel routing and R/E behavior stay unchanged.
- Focused coverage includes G, g, B, multisource, representative A/a,
  writer byte identity, workers 1/4, and cache identity.

Evidence:
- `app/main.cpp` now admits fused execution only for multi-frequency
  rectilinear `G/g/B × A/a` arrivals (multisource remains legal), while
  Eigenray fused rejection and all legacy nonreuse/reuse/parallel branches
  remain separate and unchanged.
- The fused Arrival executable route creates one output path/writer per
  frequency in ascending order, synchronously appends each source-local
  `BroadbandArrivalWorkspace`, finalizes the writer set transactionally, and
  forwards the existing range-parallel worker selection as
  `requestedRangeWorkers`.
- Targeted `bellhop_rayreuse` build passed. Executable smoke runs passed for
  multisource Cartesian GeoHat ASCII (`w1` and `w4`), ray-centered GeoHat
  binary (`w4`), and multisource GeoGaussian ASCII (`w4`), all with
  `--verify-cache`; each PRT reported identical before/after per-source
  fingerprints.
- Full-file `cmp` proved legacy reuse vs fused identity for both frequencies
  of the ASCII GeoHat, binary ray-centered GeoHat, and ASCII GeoGaussian
  runs. GeoHat fused `w1` vs `w4` outputs were also byte-identical.

### B07 [ADVANCED] Batch acceptance and characterization
Status: DONE
Reviewer: PASS (independent B07 review, 2026-09-04)

Acceptance:
- Full end-of-batch CTest once; existing Arrival regression and
  `validate_i8_arrivals.py` once; `git diff --check` clean.
- Concise GeoHat and GeoGaussian 8F/16F legacy reuse vs fused w1/w4 record:
  wall, Influence, writer time, peak RSS; no obvious regression/explosion.
- Reference production trees untouched and Git scope clean.

Evidence:
- Clean Release/Werror build configured and built successfully with the
  project uv toolchain:
  `uv run cmake -S Bellhop_RayReuse -B
  Bellhop_RayReuse/build/igr3b-clean -DCMAKE_BUILD_TYPE=Release
  -DBUILD_TESTING=ON -DRAYREUSE_WARNINGS_AS_ERRORS=ON`, then
  `uv run cmake --build Bellhop_RayReuse/build/igr3b-clean --parallel 8`.
  The single end-of-batch full run,
  `uv run ctest --test-dir Bellhop_RayReuse/build/igr3b-clean
  --output-on-failure`, passed 50/50 in 157.68 s. This includes fused Arrival
  parity/writer coverage, legacy nonreuse/reuse/parallel solver coverage, and
  the representative `arrival_eigenray_solver` R/E regression boundary.
- One fresh single-profile Arrival regression in
  `/private/tmp/igr3b-b07-arrivals.cI8l0w` passed all 9 cases for each of
  Origin, F2CPP, and RayReuse. The one required
  `validate_i8_arrivals.py` invocation exited 0 with `status=passed`, 9 cases
  per implementation and 36 comparisons (RayReuse observed maximum 123
  arrivals/cell and 1,600 reflected arrivals).
- Concise characterization used one temporary multisource regular-grid
  fixture per family (`G/A` and `B/A`), 2 sources, 1,000 rays, 101 ranges x 3
  depths, and 8 frequencies (300--1000 Hz). Each configuration was one
  unprofiled sample with `--verify-cache`; peak RSS is Darwin
  `resource.RUSAGE_CHILDREN.ru_maxrss`, normalized to MiB. Legacy `writer`
  is its timed writer callback (`arrival consume seconds`). Legacy Arrival
  PRT does not expose the populated `ArrivalSolverStatistics::influenceSeconds`,
  so that non-gating observability value is recorded as unavailable rather
  than changing PRT instrumentation in this Batch.

  | family/config | wall s | Influence s | writer s | peak RSS MiB |
  |---|---:|---:|---:|---:|
  | GeoHat reuse | 0.749 | unavailable | 0.585 | 48.1 |
  | GeoHat fused w1 | 0.166 | 0.00837 | 0.0253 | 27.6 |
  | GeoHat fused w4 | 0.171 | 0.00927 | 0.0256 | 31.9 |
  | GeoGaussian reuse | 1.150 | unavailable | 0.970 | 54.5 |
  | GeoGaussian fused w1 | 0.572 | 0.0234 | 0.414 | 44.8 |
  | GeoGaussian fused w4 | 0.563 | 0.0178 | 0.410 | 49.0 |

  The fixtures are writer-dominated micro workloads: GeoGaussian Influence
  improves about 24% from w1 to w4; GeoHat's roughly 9 ms Influence payload
  is below useful parallel granularity. Fused wall/RSS show no obvious
  regression or memory explosion. All 8 ARR frequency files are byte-identical
  across reuse/fused-w1/fused-w4 for both families; every run's before/after
  cache fingerprint is identical. Fused peak source-local Arrival workspace
  was 0.64 MiB (Hat) and 11.0 MiB (Gaussian).
- `git diff --check` passed. Git scope contains only intended
  `Bellhop_RayReuse/` production/tests/worklist paths; both reference
  production trees have no diff, and no ARR/SHD/RAY/temp/backup products were
  found outside ignored build or `/private/tmp` characterization trees.

### B08 [ADVANCED] Final review and close
Status: DONE
Reviewer: ACCEPTED (independent final review, 2026-09-04)

Acceptance:
- Independent final-reviewer returns `ACCEPTED`; every finding is remediated
  and re-reviewed.
- Commit exactly `feat(rayreuse): complete IGR-3B fused arrival influence`.
- Working tree clean after commit; no next IGR batch starts.

Evidence:
- Independent final-reviewer inspected the frozen scope/worklist, complete
  production diff, AddArr semantics, broadband layout, G/g/B kernels, range
  ownership, source-streaming writer transaction, CLI boundaries, tests,
  oracle/characterization evidence, reference-tree scope, and Git hygiene;
  verdict `ACCEPTED` with no actionable findings.
- All task checkpoints are PASS, Batch Acceptance is PASS, and no HIGH/BLOCKER
  remains. IGR-3B is CLOSED; no subsequent IGR Batch is started.
