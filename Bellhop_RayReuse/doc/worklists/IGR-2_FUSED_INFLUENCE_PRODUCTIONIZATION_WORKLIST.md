# IGR-2 — Fused Influence Productionization & Optional Range Parallelism — WORKLIST

> **Status:** ACCEPTED / CLOSED
> **Branch:** `feat/igr-influence-geometry-reuse`
> **IGR-2 baseline:** `bd4816af105be1ac09aff44e5337187e7bec0c40` plus the retained, uncommitted static contiguous range-partition diff
> **Date frozen:** 2026-09-03
> **Authority:** This worklist and
> [`DESIGN_IGR2_FUSED_INFLUENCE_PRODUCTIONIZATION.md`](DESIGN_IGR2_FUSED_INFLUENCE_PRODUCTIONIZATION.md)
> become construction authority only after an independent design-review `PASS`.

## 1. Batch objective

Promote the retained L1/L1c Cartesian Cerveny fused Influence implementation
from an experiment to the production RayReuse broadband TL core for its
supported domain. Retain the `[range][depth][frequency]` pressure layout and
add the measured static contiguous receiver-range partition as an optional
execution optimization.

This batch does not broaden the fused scientific domain. `nonreuse` remains
the traditional/reference implementation. The old per-frequency `reuse` and
frequency-parallel `parallel` CLI modes remain behavior-compatible but are
deprecated; they are not deleted in IGR-2.

## 2. Frozen scope

In scope:

- Cartesian Cerveny coherent multi-frequency TL;
- one source and a rectilinear receiver grid with at least two receiver
  ranges, preserving the existing Cartesian Cerveny precondition;
- retained L1 point-major fused ray precompute and L1c
  `[range][depth][frequency]` pressure ownership/materialization;
- fused serial as the default execution within `--execution-mode fused`;
- optional static contiguous receiver-range parallelism;
- CLI validation, runtime deprecation notices, production statistics,
  targeted tests, regression, performance evidence, and IGR-2 documentation.

Out of scope:

- dynamic range tiles, ray or frequency parallel fused execution, atomics in
  pressure accumulation, per-worker complete pressure fields, affinity/core
  binding, SIMD, fast math, reassociation, frequency blocking, L1b, mask or
  projector redesign, multisource fused, Arrival/Eigenray fused, other beam
  families, irregular receivers, cache schema changes, and unrelated refactor.

## 3. Frozen compatibility decisions

1. The global unspecified CLI execution mode remains `nonreuse`. Changing it
   would silently reroute single-frequency and products outside the fused
   support domain. “Fused is the RayReuse main path” means the production
   trace-once broadband algorithm for the supported fused domain, not the
   universal CLI fallback.
2. `--execution-mode fused` is retained as the stable explicit token. With no
   range option it runs serial fused.
3. `--range-parallel` explicitly enables static range partitioning. Its
   default requested worker count is 4. `--workers N` may override that only
   when `--range-parallel` is present.
4. `--workers N` never implicitly enables range parallelism. Its legacy use
   with `--execution-mode parallel` remains accepted.
5. Legacy `reuse` and `parallel` tokens, enum values, solver paths, output, and
   benchmark compatibility remain. Within the domain where fused is a valid
   replacement, usage text marks them deprecated and an explicit legacy-mode
   invocation emits one runtime warning to stderr. Outside that domain they
   remain supported compatibility paths without a misleading warning. There
   is no C++ API removal or `[[deprecated]]` annotation in this batch.

## 4. Tasks

### I2-01 [ADVANCED] Workspace cleanup and baseline audit
Status: DONE
Reviewer: PASS

Acceptance:

- Preserve committed L1 `1ffc1d8e` and L1c `bd4816af`.
- Retain only the static contiguous range-parallel production/test diff.
- Remove dynamic-tile, frequency-blocking, and L1b production remnants.
- Preserve and reconcile only accurate IGR-1p report changes.
- Record `git status`, scoped diff, and `git diff --check` before IGR-2 code
  construction.

### I2-02 [ADVANCED] Production fused execution and static range partition
Status: DONE
Reviewer: PASS

Acceptance:

- Keep the authoritative ray/segment/range/depth/image/frequency hierarchy and
  `[R][D][F]` pressure layout.
- Add a fused execution setting whose default is one range worker; clamp the
  active count to `min(requestedRangeWorkers, receiverRangeCount)`.
- Partition ranges into deterministic contiguous, non-empty blocks with sizes
  differing by at most one; each worker traverses rays in frozen-cache order.
- Share only the immutable `RayPathCache`, environment/model data, and the
  fused workspace; pressure ranges are disjoint.
- Use no pressure atomics and no per-worker field copy. Projected frequency
  state, epsilon scratch, projector/influence objects, exceptions, and detailed
  statistics are worker-owned.
- Preserve byte-identical per-cell accumulation order and propagate worker
  exceptions after joining all workers.
- Report requested/effective range-worker counts and critical-path phase
  timings without presenting cumulative CPU time as wall time.

### I2-03 [STANDARD] CLI, compatibility deprecation, and routing
Status: DONE
Reviewer: PASS

Acceptance:

- Add `--range-parallel`; reject duplicates and invalid combinations using the
  matrix frozen in the design.
- Fused without the flag is serial. Fused plus the flag defaults to 4 requested
  workers; explicit `--workers N` overrides it. `--workers` alone does not
  enable fused range parallelism.
- Preserve legacy frequency-parallel tuning rules and the global `nonreuse`
  default.
- Mark `reuse` and `parallel` deprecated for the fused-supported domain in
  usage and emit a runtime stderr warning only when a selected legacy mode is
  actually replaceable by fused for the parsed product.
- Keep deterministic product/scope rejection for unsupported fused cases.

### I2-04 [ADVANCED] Numerical, cache, and concurrency acceptance
Status: DONE
Reviewer: PASS

Acceptance:

- Serial fused matches the existing per-frequency oracle at raw and scaled
  workspace levels; final SHD also matches `nonreuse`.
- Range workers 1/2/4/8 pass raw/scaled `memcmp`, divergent-prefix, frozen
  cache fingerprint, and SHD byte-identity gates.
- Cover non-divisible range counts and a minimum-supported two-range fixture
  with requested workers greater than the range count, proving
  `effectiveRangeWorkers == 2`; also cover invalid zero workers, exception
  propagation, and CLI validation. IGR-2 does not relax the existing
  Cartesian Cerveny requirement of at least two receiver ranges.
- Existing CTest and pytest suites pass; run the standard-case unit target if
  it is part of the repository's current acceptance workflow.
- `git diff --check` passes and reference implementations remain untouched.

### I2-05 [ADVANCED] Performance and memory acceptance
Status: DONE
Reviewer: PASS

Acceptance:

- Release `munk_cerveny_cc` 16F, static partition only, no Influence profiling.
- Measure range workers 1/2/4/8 with one warmup and three interleaved measured
  runs per configuration; report wall median/min/max/MAD, critical-path
  Project/Influence, peak RSS, and speedup versus worker 1.
- Treat the known serial 85–89 s and static-8 approximately 23.15 s only as
  sanity references, not acceptance thresholds.
- Distinguish peak RSS, total allocation, hot working set, and per-worker
  scratch. Confirm no per-worker full field exists.

### I2-06 [STANDARD] Documentation, Batch Acceptance, and commit
Status: DONE
Reviewer: ACCEPTED (final-reviewer, 2026-09-03)

Acceptance:

- Add an IGR-2 implementation/report and update
  `doc/plans/PLAN_CURRENT_WORK.md` plus the IGR-1p experiment report accurately.
- Document fused as the supported-domain RayReuse core, `[R][D][F]` as its
  production pressure layout, range parallel as optional/default-4 when
  enabled, legacy modes as deprecated, and `nonreuse` as reference.
- Coordinator performs Batch Acceptance; all checkpoint findings are closed by
  re-review; independent final reviewer returns `ACCEPTED`.
- Commit one scoped IGR-2 change only after all gates pass. Do not push and do
  not begin IGR-3.

## 5. Completion gate

IGR-2 may be marked `ACCEPTED / CLOSED` only when I2-01 through I2-06 are DONE,
all ADVANCED checkpoints have reviewer `PASS`, the full listed regression and
bitwise gates pass, performance evidence is recorded without overclaiming,
documentation matches the implementation, Git scope is clean, and the final
reviewer returns `ACCEPTED`.

## 6. Open acceptance finding and remediation

### I2-R1 [ADVANCED] Shared fused support-domain authority
Status: DONE
Reviewer: PASS

Finding:

- The app's legacy replacement-warning predicate omitted the solver's
  equally-spaced receiver-range requirement, so a non-equally-spaced
  rectilinear case could be advertised as replaceable by fused even though the
  fused solver rejects it.
- The performance artifact records baseline commit `bd4816af`, dirty state,
  commit tree, and binary SHA-256, but not an exact hash of the dirty diff.
  Documentation must not equate that evidence with an exactly reconstructible
  source or later commit identity.

Remediation evidence:

- A public read-only `supportsFusedRayReuse` predicate now derives from the
  same detailed scope classifier used by solver validation. It covers coherent
  TL, Cartesian Cerveny, one source, at least two frequencies, rectilinear
  non-irregular receivers, at least two ranges, and equal range spacing.
- Focused component coverage proves the valid production fixture is supported
  and a non-equally-spaced rectilinear fixture is not eligible for legacy
  replacement warnings; detailed solver rejection remains covered.
- Release focused CTest 4/4 PASS; benchmark focused pytest 25/25 plus 14
  subtests PASS; `git diff --check` PASS; Origin/F2CPP production untouched.
- The original Batch Acceptance reviewer returned `PASS` on re-review; all
  findings are closed. Final review remains pending.

## 7. Batch Acceptance evidence

- Release warnings-as-errors build: PASS (AppleClang 21.0.0).
- Full CTest: 43/43 PASS.
- Full pytest after benchmark-harness completion: 192/192 PASS with 399
  subtests.
- Standard-case unittest: 177/177 PASS.
- Raw/scaled workspace, divergent-prefix, cache fingerprint, 2F and 16F SHD
  byte-identity gates: PASS.
- 16F static range-worker benchmark: one warmup plus three rotated measured
  samples for workers 1/2/4/8; no Influence profiling; independent statistics
  recomputation PASS.
- Batch Acceptance reviewer verdict after remediation: PASS.
- Independent final-reviewer verdict: `ACCEPTED` on 2026-09-03; no open
  critical/actionable finding.
