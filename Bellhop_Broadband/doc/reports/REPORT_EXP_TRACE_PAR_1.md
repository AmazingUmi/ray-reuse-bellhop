# EXP-TRACE-PAR-1 — Minimal Static Trace Parallel Probe

## Change

The experimental `--trace-workers N` option is accepted only with explicit
`--execution-mode reuse`. It statically partitions the existing launch-angle
vector into continuous balanced index ranges. Every worker constructs its own
`GeometryTracer`, calls the unchanged `trace()` for each assigned angle, and
owns complete local `RayPath` values. After join, paths are moved into the
final cache in worker/range order and the cache is frozen once.

The option is deliberately limited to the reuse SHD path. Omitting it retains
the existing serial trace path. Product validation rejects the option for
ARR, Eigenray, and ordinary R-ray products rather than silently ignoring it.

## Quick experiment

- Case/profile: `munk_cerveny_cc`, `broadband_smoke` (50 and 250 Hz)
- Work: 5000 rays, 1,683,973 ray points
- Build: Release
- Samples: one warmup, then three isolated measured runs per worker count;
  order rotated
- RSS: child-process `ru_maxrss`

| workers | Trace median (s) | Trace gain vs w1 | external wall median (s) | RSS median (MiB) | RSS delta |
|---:|---:|---:|---:|---:|---:|
| 1 | 0.2963 | — | 8.420 | 306.45 | — |
| 2 | 0.1602 | 45.9% | 8.287 | 309.59 | +1.02% |
| 4 | 0.1282 | 56.7% | 8.241 | 310.86 | +1.44% |
| 8 | 0.1005 | 66.1% | 8.234 | 310.56 | +1.34% |

Worker-active durations are retained in
`build/benchmarks/exp_trace_par_min.json`; the PRT emits them in stable
source/worker order.

## Correctness

All 12 measured w1/w2/w4/w8 runs matched exactly:

- ray count: `5000`
- ray-point count: `1683973`
- cache fingerprint before/after: `2271226459307825052`
- SHD SHA-256:
  `cf1f9711aefcab087bd766c395a03b935c1c9cf13980335a368035515fd126bc`

The fingerprint covers launch order and complete ray state including p/q,
steps, reflection events, and termination. Targeted component coverage also
checks w1/w2/w4/w8 against the legacy serial fingerprint.

## Verdict

`CLEAR_TRACE_WALL_GAIN` for this narrow Munk reuse probe. The experiment is not
rolled back: w8 reduced Trace wall by 66.1%, and the small RSS increase stayed
far below the 25% limit. End-to-end improvement is only 2.2% because Influence
dominates this case. This is experimental evidence, not authorization to claim
general production speedup or to extend the option to ARR/RAY/fused paths.

Independent concurrency/ownership review returned `PASS`. Final review first
identified and closed the non-TL option-validation gap, then returned
`ACCEPTED`.

