# PERF-TRACE-PAR-1 — Production Static Trace Parallelism

> Status: CLOSED (ACCEPTED 2026-09-05)
> Date opened: 2026-09-04
> Authority: user-directed productionization of accepted EXP-TRACE-PAR-1.

## Frozen decisions

- Reuse the accepted continuous launch-index partitions, worker-local
  `GeometryTracer` and complete `RayPath` vectors, ordered move/merge, and one
  final `freeze()`; do not modify `GeometryTracer::trace()`.
- One shared trace seam serves TL, ARR, Eigenray, and ordinary R products.
- Stable product-independent `--trace-workers N`; default remains 1. It is
  orthogonal to existing frequency/range `--workers`.
- `RayFanTraceSettings` contains only positive `workerCount`; one uses the
  serial fast path, larger counts clamp to launch count.
- Preserve product-specific abnormal-termination diagnostics and deterministic
  lowest-launch-index failure after joining every worker.
- Explicit trace-worker runs expose requested/effective counts and stable
  per-source worker durations; cache bytes do not substitute for external RSS.

### A01 [ADVANCED] Shared production seam and all product wiring
Status: DONE
Reviewer: PASS (2026-09-05; concurrency, ownership, ordered merge, product
diagnostics byte-identity against HEAD, 12+ call-site wiring, stats plumbing)

Goal:
- Migrate `traceSourceFan` to `RayFanTraceSettings{workerCount}` semantics
  (1 = serial fast path, clamp to launch count, reject zero) with the
  `RayFanTraceProduct` diagnostic selector.
- Route ARR/Eigenray/R through the shared seam (local trace loops removed);
  wire TL single/nonreuse/reuse/parallel/fused, ARR nonreuse/reuse/parallel/
  fused, Eigenray nonreuse/reuse/parallel, and ordinary R.

Evidence:
- `terminationDiagnostic` reproduces the four product texts byte-identically;
  parallel w4 abnormal termination equals the serial message (test).
- R product returns `RayFanTraceResult` vectors; writer/fingerprint paths
  consume `.cache`.
- Release build clean under warnings-as-errors.

### A02 [STANDARD/ADVANCED] CLI, statistics, and targeted tests
Status: DONE
Reviewer: PASS (2026-09-05)

Goal:
- `--trace-workers` accepted for every product/mode without execution-mode
  prerequisites; independent of `--workers`.
- Zero, clamp, exception, ordered fingerprint, and worker-stat tests pass.

Evidence:
- `--trace-workers 2` parses standalone; `--trace-workers 0` rejected;
  coexists with `--execution-mode parallel --workers 8` (test).
- Clamp test: workerCount 1000 → effective == launch count, fingerprint equal;
  worker stats sized to the effective count (test).
- All 47 test binaries in build/release pass (full suite, 2026-09-05).
- Default (no flag) behavior byte-identical to HEAD across 9 default runs
  (worker-verified with a HEAD worktree binary: SHD/ARR/RAY products plus
  timing-stripped PRT).

### A03 [ADVANCED] Product parity and performance acceptance
Status: DONE
Reviewer: PASS (2026-09-05)

Acceptance:
- w1/w2/w4/w8 preserve cache/ray/point semantics and byte-identical SHD,
  ASCII/binary ARR, Eigenray `.ray`, and ordinary R `.ray` products.
- External RSS for w8 remains within +25% of w1.
- Existing Munk TL Trace benefit remains clear; product-specific claims use
  only collected evidence.

Evidence (artifact `build/benchmarks/perf_trace_par1.json`, driver
`build/benchmarks/perf_trace_par1_driver.py`, both ignored build outputs):
- Byte identity w1/w2/w4/w8: Munk TL nonreuse/reuse/fused/parallel SHD;
  ARR ascii + binary `.arr`; Eigenray `.ray`; ordinary R `.ray`. All pass.
- Production clamp proven by the R case (5 launches): w8 effective = 5,
  products identical.
- Munk 2F reuse (5000 rays), 1 warmup + 3 samples: Trace medians
  w1/w2/w4/w8 = 0.2736 / 0.1571 / 0.1453 / 0.0927 s; w8 gain 66.1%;
  external wall 8.335 → 8.145 s; RSS 305.8 → 308.3 MiB (+0.8%, limit +25%);
  fingerprint 2271226459307825052 for every worker count (matches
  EXP-TRACE-PAR-1).

### A04 [ADVANCED] Batch acceptance and final review
Status: DONE
Reviewer: ACCEPTED (2026-09-05,
`doc/reviews/PERF-TRACE-PAR-1_FINAL_REVIEW_2026-09-05.md`)

Acceptance:
- Targeted/full acceptance proportional to risk passes; Origin/F2CPP untouched;
  `git diff --check` passes; independent final reviewer returns `ACCEPTED`.

Evidence:
- Full suite: 47/47 test binaries pass (release).
- `git diff --check` passes; Origin/F2CPP untouched; only listed production,
  test, and doc files changed.
- Reviewer minor findings closed: empty-fan guard added before the trace
  partition division (`single_frequency_solver.cpp`); worklist status sync
  (this file). Duplicate-flag CLI test skipped per repo convention.

## Blockers / findings

- None open.
