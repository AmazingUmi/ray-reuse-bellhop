# EXP-TRACE-PAR-1 Final Review — 2026-09-04

## Verdict

`ACCEPTED`

The minimum experiment preserves the accepted serial default, leaves
`GeometryTracer::trace()` unchanged, uses continuous static launch-index
partitions with worker-local tracers and complete paths, joins before ordered
move/merge, and freezes the final cache once.

Targeted tests and the Munk w1/w2/w4/w8 probe preserve ray count, ray-point
count, cache fingerprint, and SHD bytes. The measured Trace wall gain is clear.

The initial review finding that non-TL reuse products could accept and ignore
`--trace-workers` was remediated with product-level rejection. Rebuild,
targeted tests, and `git diff --check` passed before re-review.

