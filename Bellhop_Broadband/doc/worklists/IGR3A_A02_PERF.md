# IGR-3A A02 Performance Evidence — Unified Executor Migration (CC Coherent)

> Baseline binary: clean `git worktree` at `38137a4`
> (`5b802a8cc75819d5bc0434f6c14510df4fc4c30d41fe3e865eb29145d81a2ebb`),
> candidate: A01+A02 working tree
> (`900b09f026fd1a9baf9eb9a4015de464debd2ab7b16292d97147b0fd45c41034`).
> Machine: Apple M4, 10 cores, 24 GiB. Release preset, warnings-as-errors ON.
> Case: `munk_cerveny_cc` `broadband_regression` (50–500 Hz, 16 frequencies,
> 10000 rays, 201x501 receivers). Fused mode, static range workers 1/2/4/8,
> plus legacy `reuse` as cross-mode anchor. 1 warmup + 3 measured runs per
> configuration (benchmark harness `benchmark_rayreuse.py`, PRT-parsed
> Trace/Project/Influence/Scale + `ru_maxrss`), sequential sessions.

## Gate: SHD byte/SHA-256 identity

Both sessions enforce `cross_configuration_shd_identity_required`; all five
configurations per binary, and every interleaved A/B run below, produce the
identical product hash `f01ee48119549a82e79798322bf5227d8fc95054be82de955de5ccadef057c2c`
(reuse == fused == workers 1/2/4/8, baseline == candidate).

## Separated sessions (medians of 3 measured; delta = candidate vs baseline)

configs: ['fused-range-w1', 'fused-range-w2', 'fused-range-w4', 'fused-range-w8', 'reuse'] ['fused-range-w1', 'fused-range-w2', 'fused-range-w4', 'fused-range-w8', 'reuse']
base common_shd: f01ee48119549a82 | cand: f01ee48119549a82 | identical: True

| config | metric | base med [min..max] MAD | cand med [min..max] MAD | delta(med) | overlap |
|---|---|---|---|---|---|
| fused-range-w1 | wall_s | 84.232 [84.121..84.352] 0.111 | 84.353 [84.281..84.453] 0.072 | +0.14% | yes |
| fused-range-w1 | Trace_s | 0.563 [0.553..0.583] 0.010 | 0.560 [0.550..0.580] 0.011 | -0.43% | yes |
| fused-range-w1 | Project_s | 0.480 [0.478..0.493] 0.002 | 0.477 [0.476..0.480] 0.001 | -0.53% | yes |
| fused-range-w1 | Influence_s | 83.028 [82.938..83.187] 0.090 | 83.183 [83.107..83.255] 0.072 | +0.19% | yes |
| fused-range-w1 | Scale_s | 0.093 [0.087..0.105] 0.006 | 0.101 [0.088..0.108] 0.006 | +8.47% | yes |
| fused-range-w1 | RSS_MiB | 634.609 [634.531..634.703] 0.078 | 634.594 [634.109..634.656] 0.062 | -0.00% | yes |
| fused-range-w2 | wall_s | 45.060 [45.039..45.146] 0.021 | 45.156 [45.148..45.179] 0.008 | +0.21% | NO |
| fused-range-w2 | Trace_s | 0.592 [0.585..0.598] 0.006 | 0.555 [0.550..0.557] 0.003 | -6.33% | NO |
| fused-range-w2 | Project_s | 0.480 [0.472..0.483] 0.003 | 0.484 [0.483..0.484] 0.000 | +0.72% | NO |
| fused-range-w2 | Influence_s | 43.855 [43.830..43.950] 0.025 | 43.988 [43.967..44.003] 0.015 | +0.30% | NO |
| fused-range-w2 | Scale_s | 0.094 [0.092..0.098] 0.001 | 0.097 [0.095..0.102] 0.002 | +3.64% | yes |
| fused-range-w2 | RSS_MiB | 637.125 [636.156..637.406] 0.281 | 637.359 [637.297..637.625] 0.062 | +0.04% | yes |
| fused-range-w4 | wall_s | 27.626 [27.551..27.854] 0.075 | 27.590 [27.531..27.780] 0.059 | -0.13% | yes |
| fused-range-w4 | Trace_s | 0.559 [0.556..0.580] 0.003 | 0.562 [0.551..0.582] 0.011 | +0.49% | yes |
| fused-range-w4 | Project_s | 0.551 [0.548..0.556] 0.004 | 0.551 [0.547..0.555] 0.003 | -0.00% | yes |
| fused-range-w4 | Influence_s | 26.365 [26.334..26.615] 0.031 | 26.345 [26.278..26.533] 0.067 | -0.08% | yes |
| fused-range-w4 | Scale_s | 0.097 [0.097..0.101] 0.001 | 0.099 [0.093..0.113] 0.007 | +2.04% | yes |
| fused-range-w4 | RSS_MiB | 641.531 [641.438..641.641] 0.094 | 641.828 [641.656..641.906] 0.078 | +0.05% | NO |
| fused-range-w8 | wall_s | 22.364 [22.338..23.149] 0.025 | 22.679 [22.660..22.750] 0.019 | +1.41% | yes |
| fused-range-w8 | Trace_s | 0.589 [0.553..0.598] 0.010 | 0.580 [0.555..0.591] 0.011 | -1.47% | yes |
| fused-range-w8 | Project_s | 0.795 [0.778..0.797] 0.002 | 0.810 [0.801..0.811] 0.002 | +1.90% | NO |
| fused-range-w8 | Influence_s | 20.856 [20.850..21.686] 0.006 | 21.187 [21.166..21.228] 0.020 | +1.59% | yes |
| fused-range-w8 | Scale_s | 0.102 [0.084..0.110] 0.008 | 0.099 [0.099..0.112] 0.000 | -2.92% | yes |
| fused-range-w8 | RSS_MiB | 651.844 [651.547..653.922] 0.297 | 652.719 [650.531..652.859] 0.141 | +0.13% | yes |
| reuse | wall_s | 97.228 [97.108..97.230] 0.002 | 97.286 [97.284..97.460] 0.002 | +0.06% | NO |
| reuse | Trace_s | 0.583 [0.558..0.594] 0.012 | 0.581 [0.556..0.585] 0.005 | -0.40% | yes |
| reuse | Project_s | 0.536 [0.525..0.539] 0.003 | 0.538 [0.530..0.538] 0.000 | +0.31% | yes |
| reuse | Influence_s | 95.992 [95.885..95.993] 0.001 | 96.069 [96.053..96.211] 0.016 | +0.08% | NO |
| reuse | Scale_s | 0.065 [0.060..0.065] 0.000 | 0.065 [0.065..0.069] 0.000 | -0.13% | yes |
| reuse | RSS_MiB | 607.375 [607.328..607.422] 0.047 | 607.375 [607.344..607.391] 0.016 | +0.00% | yes |

worst |wall median delta| (separated sessions): 1.41%

Sub-second metrics (Trace/Project/Scale) swing several percent between
sessions with overlapping ranges (Scale at w1: +8.47% median on a 0.09 s
quantity) — noise, not overhead. Wall/Influence/RSS are the decision metrics.

## Interleaved A/B (session-bias control for the two largest-delta configs)

Protocol: fixed generated 16F env, direct binary invocation, alternating
candidate/baseline order per measured pair (order swapped between pairs),
1 warmup per binary per config, 3 measured pairs, w8 then w1. Every run's
SHD sha256 identical (`f01ee481...`).

| config | metric | base med [min..max] | cand med [min..max] | delta(med) | overlap |
|---|---|---|---|---|---|
| w8 | wall_s | 22.363 [22.323..22.803] | 22.641 [22.601..22.726] | +1.24% | yes |
| w1 | wall_s | 83.384 [83.161..84.537] | 83.466 [83.448..84.186] | +0.10% | yes |

(The separated-session w1 delta of +0.14% collapses to +0.10% interleaved;
w8 holds a ~+1.2..1.4% median delta with fully overlapping ranges — the
baseline's own w8 spread across sessions is 22.34..23.15 s, i.e. ~3.6%.)

## Conclusion

No measurable overhead from the unified executor migration: worst median
wall delta +1.41% (w8) against the documented <=5% threshold, all other
configs within +/-0.21%; RSS identical within 0.13%; SHD products bitwise
identical across both binaries and all worker counts. Gate PASS.

Raw benchmark JSONs: `/tmp/igr3a_a02_perf_baseline.json`,
`/tmp/igr3a_a02_perf_candidate.json` (regenerable via the commands above).
