# EXP-TRACE-PAR-1 — Minimal Static Trace Parallel Probe

> Status: ACCEPTED
> Date: 2026-09-04
> Scope: user-directed minimal locate → edit → run → compare → report probe.

### A01 [ADVANCED]
Status: DONE
Reviewer: PASS (2026-09-04; concurrency, ownership, order, parity)

Goal:
- Add an opt-in `--trace-workers` only to broadband `reuse`.
- Partition one source fan into continuous static launch-index ranges.
- Give each worker an independent `GeometryTracer` and complete local
  `RayPath` vector; join, move in original order, then freeze once.
- Do not modify `GeometryTracer::trace()`.

Acceptance:
- Default path remains serial.
- w1/w2/w4/w8 have identical ray count, ray-point count, cache fingerprint,
  and SHD bytes.
- Record Trace wall, worker durations, external wall, and peak RSS.
- Retain the experiment only if Trace wall benefit is clear; otherwise revert.

Evidence:
- Release build PASS.
- Targeted command-line and single-frequency-solver tests: 2/2 PASS.
- Munk 2F, 5000 rays / 1,683,973 points, one warmup + three samples.
- All worker counts: fingerprint `2271226459307825052`; SHD SHA-256
  `cf1f9711aefcab087bd766c395a03b935c1c9cf13980335a368035515fd126bc`.
- Trace medians w1/w2/w4/w8: 0.2963 / 0.1602 / 0.1282 / 0.1005 s.
- w8: 66.1% lower Trace wall, 2.2% lower external wall, 1.34% higher RSS.
- Raw artifact: `build/benchmarks/exp_trace_par_min.json` (ignored build output).
- Independent reviewer: PASS.
- Final-review finding: non-TL reuse products could accept and ignore the
  option. Remediated by product-level rejection; rebuild/tests/diff-check PASS.
- Final reviewer: `ACCEPTED` (2026-09-04).
