# IGR-1p — Fused Layout & Memory Optimization Feasibility — WORKLIST

> **Status:** CONSTRUCT / P04 L1 PROBE COMPLETE (candidate undecided)
> **Branch:** `feat/igr-influence-geometry-reuse`
> **IGR-1p start HEAD:** `749741d9eba8559ecdd286fb7080ceabd7e0b605`
> **Date opened:** 2026-09-02
> **Authority:** Execution authority after architect freeze and independent design-review `PASS` on 2026-09-02. P01 was interrupted before report/review by the user's explicit reprioritization; current authorization is the isolated P04 L1 data-layout probe only.

## 1. Batch objective

Determine, without presupposing the result, whether the IGR-1 serial Cartesian
Cerveny fused wall-time failure is primarily caused by `Bf=Nf` and fragmented
per-frequency memory layout, and whether layout/locality improvements plus
frequency blocking can make fused execution `VIABLE`, `PARTIALLY_VIABLE`, or
`NOT_VIABLE` while preserving the existing physics and per-frequency
accumulation semantics.

This is a performance/layout follow-up to closed IGR-1. It is not IGR-2 and
does not expand the feature boundary.

## 2. Frozen scope from the batch request

In scope only:

- Cartesian Cerveny, coherent TL, rectilinear receiver grid, single source,
  serial execution, and the existing fused reference path.
- Fused-specific data-layout optimization, scratch reuse, frequency blocking,
  instrumentation, allocation/layout audit, and benchmark additions.
- Controlled experiments for masks, fused precompute layout, cached workspace
  pressure bases, and—only if justified by earlier data—block-local workspace
  staging.

Hard out of scope:

- Parallel fused, multisource fused, Arrival/Eigenray, other beam families,
  frequency interpolation, persistent influence-geometry cache, GPU, SIMD math
  approximation, fast-math, reassociation, approximate transcendental math,
  acoustic-physics changes, projector/reflection/attenuation semantic changes,
  frozen `RayPathCache` changes, and unrelated refactors.

## 3. Numerical and cache hard gates

- Existing `reuse` is the oracle.
- Level A: `RayPathCache` fingerprint before == after.
- Level B: raw workspace is bitwise identical per frequency (`std::memcmp`).
- Level C: scaled workspace is bitwise identical per frequency.
- Level D: SHD SHA-256 equals `reuse`.
- For every fixed frequency preserve ray, segment/range/depth, and
  True -> Surface -> Bottom image order; preserve contribution expressions and
  the workspace addition stream; no reduction or reassociation.
- A candidate that cannot retain bitwise parity stops immediately and is not
  allowed to weaken the oracle gate.

## 4. Baseline interpretation

The authoritative IGR-1 raw wall medians are:

| Nf | reuse (s) | fused `Bf=Nf` (s) |
|---:|---:|---:|
| 2 | 7.89 | 11.24 |
| 8 | 49.16 | 51.04 |
| 16 | 95.66 | 103.04 |

Historical values labelled `fused/reuse = 0.70/0.96/0.93` are actually
`reuse/fused`; they must not be used with the incorrect label.

## 5. Frozen benchmark protocol

- Release build; same machine, case, executable configuration, and frequency
  list within each comparison.
- Primary case: `munk_cerveny_cc`; mandatory 2F, 8F, and 16F rows.
- Per row and candidate, run clean timing with influence profiling disabled:
  at least one warmup plus five measured repeats; report median, min/max, MAD
  (or equivalent dispersion), end-to-end wall, clean Influence/Project/Scale,
  peak RSS, and parity evidence. These clean samples alone drive retention and
  viability.
- Run matching `--profile-influence` invocations separately for geometry and
  frequency-kernel counters. Label profiled wall/Influence as instrumented and
  never use them for retention or viability.
- Compare `reuse`, unchanged IGR-1 fused `Bf=Nf` baseline, IGR-1p candidates,
  and parallel as reference only.
- Mandatory blocking sweep with clamp for `Nf < Bf`:
  - 2F: `Bf=1,2` (larger requested sizes clamp and need not duplicate rows).
  - 8F: `Bf=1,2,4,8`.
  - 16F: `Bf=1,2,4,8,16`.
  - Optional 32F: `Bf=4,8,16,32` only if runtime is acceptable.
- Geometry traversal expectation is approximately
  `ceil(Nf/Bf)`, or ratio `ceil(Nf/Bf)/Nf` relative to reuse; this deliberate
  increase is a performance trade-off, not a correctness failure.
- Frequency-local kernel counters must remain reuse-equivalent.

## 6. Memory interpretation

Record and keep distinct:

- total allocated bytes;
- hot working-set locality;
- peak resident set size (RSS).

Target model: `M ~= M_frozen + Bf*M_field + scratch` only if field ownership is
actually block-bounded. If all `Nf` output fields remain resident, state
explicitly that blocking changes the hot working set but not total resident
field memory.

## 7. Tasks

### P01 [STANDARD] Baseline / profiling
Status: PAUSED_AFTER_DATA_COLLECTION (report/review incomplete; user reprioritized)
Reviewer: PENDING

Acceptance:
- Reproduce `reuse` and unchanged fused `Bf=Nf` at 2F/8F/16F.
- Record wall/Influence/Project/Scale/RSS/counters with the frozen protocol.
- Explain any drift from IGR-1 before candidate benchmarking.

Evidence:
- Correctness health gate and primary 2F/8F/16F clean archives were collected
  under `build/igr1p-clean`; separate counter runs were also collected. A full
  P01 report and independent review were not completed before the user directed
  work to the data-structure probe. These artifacts are not a closed P01 gate.

### P02 [ADVANCED] Allocation / layout audit and frozen design
Status: DONE
Reviewer: PASS (2026-09-02, after one findings/remediation/re-review cycle)

Acceptance:
- Audit fused kernel, workspace ownership, precompute layout, masks, projector,
  allocations, pointer chasing, lifetimes, and working-set formulas before
  production changes.
- Publish an as-built memory-layout diagram and numerical-order audit.
- Freeze isolated candidate experiments, ordering, rollback rules, API surface,
  counter semantics, parity gates, benchmark matrix, and construction stop/go
  gate in `DESIGN_IGR1p_LAYOUT_MEMORY_OPTIMIZATION.md`.
- Independent reviewer returns `PASS`; findings, if any, are remediated and
  re-reviewed before CONSTRUCT.

Evidence:
- Architect-frozen design: `DESIGN_IGR1p_LAYOUT_MEMORY_OPTIMIZATION.md`
  (2026-09-02), including as-built allocation/lifetime model, isolated
  candidates, block ownership/API, bitwise order audit, measurement protocol,
  P06 activation gate, risks, and stop rule.
- Design-review remediation (2026-09-02): separated clean timing from profiled
  counter runs; corrected full-path projected-state vs active-prefix models
  and epsilon bytes; removed shared SSP-evaluation dedup from authority.
- Independent design review:
  `../reviews/IGR1p_DESIGN_REVIEW_2026-09-02.md`; initial findings were one
  HIGH and two MEDIUM, all remediated by architect and independently re-reviewed.
  Final verdict `PASS`.

### P03 [STANDARD/ADVANCED by candidate] Low-risk scratch experiments
Status: TODO (blocked by P02 reviewer PASS)
Reviewer: PENDING by frozen risk classification

Acceptance:
- Benchmark each candidate independently: mask representation, fused-only
  precompute scratch, removing long-lived unused `p`, cross-ray capacity reuse,
  cached workspace pressure bases/spans.
- Retain only candidates with measured benefit or a documented non-wall benefit;
  revert no-benefit candidates.
- Preserve Levels A-D.

Evidence:
- Pending.

### P04 [ADVANCED] Fused-specific frequency-local SoA/AoSoA
Status: PROBE_COMPLETE (not retained/rejected; full gate not run)
Reviewer: PASS_FOR_SHORT_PROBE_ONLY (not P04 closure)

Acceptance:
- Implement only the frozen fused-specific layout; do not force changes to the
  general per-frequency `PrecomputedRayValues` path.
- Measure allocation count, per-ray scratch bytes, working set, wall/Influence,
  and Levels A-D before retaining the candidate.

Evidence:
- Fused-only flat point-major `p/q/gamma/kmah` implementation in
  `src/field/cartesian_cerveny_influence.cpp`; no general-kernel or other
  candidate changes.
- Release targeted tests: Cartesian Cerveny influence + fused CC parity 2/2
  PASS; reviewer independently checked index/prefix/order semantics and reran
  parity, returning PASS only for entry to the short probe.
- Clean 2F one-warmup/one-sample probe: reuse 8.008 s; fused L1 11.307 s;
  fused/reuse 1.412; SHD identical. The L1 fused result is about 0.78% below
  the unchanged-layout 2F median but inside its observed range, so no
  performance improvement is demonstrated and no retention decision is made.
- Report: `../reports/REPORT_IGR1p_LAYOUT_EXPERIMENTS.md`.

### P05 [ADVANCED] Frequency blocking
Status: TODO (blocked by P02 reviewer PASS)
Reviewer: PENDING

Acceptance:
- Implement the frozen internal `frequencyBlockSize` policy and the mandatory
  sweep from section 5.
- Preserve each frequency's addition stream and Levels A-D.
- Counters match the frozen block semantics and frequency-local counts remain
  reuse-equivalent.

Evidence:
- Pending.

### P06 [ADVANCED, OPTIONAL] Block-local workspace-layout experiment
Status: DEFERRED pending P04/P05 data
Reviewer: PENDING if activated

Acceptance:
- Activate only if P04/P05 evidence predicts sufficient remaining locality or
  resident-memory benefit.
- Compare existing `pressure[f][cell]` against frozen block-local staging;
  include transpose/copy cost in end-to-end wall.
- Preserve each frequency's addition order and Levels A-D; otherwise stop the
  candidate immediately.

Evidence:
- Pending.

### P07 [ADVANCED] Batch acceptance and final attribution
Status: TODO
Reviewer: PENDING (final-reviewer)

Acceptance:
- Complete targeted parity/oracle checks and one consolidated full Batch
  Acceptance run.
- Produce all required reports and final-review document.
- State the best retained layout, best `Bf`, wall/Influence/RSS/counters,
  allocation/working-set changes, attribution, limitations, and exactly one of
  `VIABLE`, `PARTIALLY_VIABLE`, or `NOT_VIABLE`.
- If low-risk cleanup, SoA/AoSoA, and the blocking sweep cannot stably beat
  reuse, terminate serial Cartesian Cerveny fusion wall-time optimization; do
  not continue open-ended tuning.

Evidence:
- Pending.

## 8. Required deliverables

- `doc/worklists/IGR-1p_LAYOUT_MEMORY_OPTIMIZATION_WORKLIST.md`
- `doc/worklists/DESIGN_IGR1p_LAYOUT_MEMORY_OPTIMIZATION.md`
- `doc/reports/REPORT_IGR1p_BASELINE_AND_LAYOUT_AUDIT.md`
- `doc/reports/REPORT_IGR1p_LAYOUT_EXPERIMENTS.md`
- `doc/reports/REPORT_IGR1p_BLOCKING_PERFORMANCE.md`
- `doc/reports/REPORT_IGR1p_FINAL.md`
- `doc/reviews/IGR1p_FINAL_REVIEW_2026-09-XX.md`

## 9. Current construction gate

```text
CONSTRUCT / P04 L1 PROBE COMPLETE

Result: targeted parity PASS; performance gain NOT DEMONSTRATED
Other P03/P04/P05/P06 candidates: FORBIDDEN
Next action: none in this turn; full retention gate remains unrun
```
