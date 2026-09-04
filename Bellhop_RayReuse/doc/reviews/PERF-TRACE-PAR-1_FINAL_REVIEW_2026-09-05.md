# PERF-TRACE-PAR-1 Final Review — 2026-09-05

## Verdict

`ACCEPTED`

Static trace parallelism is productionized behind one shared seam. The
`SingleFrequencySolver::traceSourceFan(simulation, sourceIndex, settings,
product)` entry now serves TL, ARR, Eigenray, and ordinary R products; the
product-local trace loops in the arrival, eigenray, and ray-trace solvers were
removed. `--trace-workers N` is product- and mode-independent, defaults to the
serial fast path, clamps to the launch count, and stays orthogonal to the
frequency/range `--workers` option.

Verified in review:

- Frozen-geometry contract: workers hold local `GeometryTracer` instances and
  complete local `RayPath` vectors, read only shared immutable inputs, and the
  ordered merge (worker-ascending, launch-ascending) is followed by a single
  `freeze()`, so the merged order equals the serial launch order.
  `GeometryTracer::trace()` is untouched.
- Failure determinism: all workers join before the lowest-worker-index
  `exception_ptr` is rethrown; with continuous partitions that equals the
  lowest launch index, and the four product-specific diagnostics are
  byte-identical to HEAD in both serial and parallel paths.
- Evidence: 8 product configurations (Munk TL nonreuse/reuse/fused/parallel,
  ARR ASCII/binary, Eigenray, R) produce byte-identical products for
  w1/w2/w4/w8; the R case proves the production clamp (w8 effective = 5).
  Munk 2F reuse: Trace median 0.2736 → 0.0927 s (66.1% trace-stage gain),
  external wall 8.335 → 8.145 s, RSS +0.8% (limit +25%), cache fingerprint
  2271226459307825052 stable across worker counts and equal to the
  EXP-TRACE-PAR-1 value. Full suite: 47/47 test binaries pass.
- Reviewer PASS (2026-09-05) with minor findings closed (empty-fan guard
  before the partition division; worklist status sync); the duplicate-flag CLI
  test was skipped per repo convention. Origin/F2CPP untouched;
  `git diff --check` passes.

Claims stay scoped: 66.1% is the Trace-stage gain on the Munk reuse case,
not an end-to-end speedup claim.
