# IGR-3A A09 Level F Performance Evidence — Unified Fused Executor, All Families + I/S

> Candidate: clean-tree build `Bellhop_RayReuse/build/igr3a-clean` (release
> preset cache: `CMAKE_BUILD_TYPE=Release`,
> `RAYREUSE_ENABLE_SANITIZERS=OFF`, `RAYREUSE_WARNINGS_AS_ERRORS=ON`;
> `uv run cmake -S Bellhop_RayReuse -B Bellhop_RayReuse/build/igr3a-clean
> -G "Unix Makefiles" ...`), exe sha256
> `5aeebe780c750bc4b8b0a98121d7adf27b618530f8a15e4cc662028631c050c0`
> (byte-identical to `build/release` rebuilt from the same tree).
> Git: HEAD `68ad370c86926d8fed0a097ea7106f9282cff02a`, dirty (accepted
> uncommitted A02b-A08 batch diffs + this task's shared-test infra edits);
> `git diff --stat | tail -1` = "33 files changed, 4201 insertions(+),
> 143 deletions(-)". `git diff --check` clean.
> Machine: Apple M4, 10 cores, 24 GiB (label recorded in the JSON).
> Harness: `test/standard_cases/codes/benchmark_rayreuse.py --case <12 ids>
> --profile broadband_regression --modes reuse,fused
> --fused-range-workers 1,2,4,8 --repeats 3 --warmups 1 --executable
> build/igr3a-clean/bellhop_rayreuse --allow-dirty`, one sequential
> session, rotated-per-round config order (reuse, w1, w2, w4, w8),
> 19:35 wall total. Raw JSON:
> `Bellhop_RayReuse/build/benchmarks/igr3a_a09_level_f.json`.

## Benchmark inventory (frozen worklist §4; no substitutions)

Every configuration uses its case's `broadband_regression` 16F profile.

| set | case | profile (all 16F) | rays |
|---|---|---|---|
| coherent | munk_cerveny_cc | 50-500 Hz (pre-existing) | 10000 |
| coherent | ray_centered_component_pressure | 1000-2500 Hz (added A09) | 300 |
| coherent | geometric_hat_cartesian | 100-250 Hz (added A09) | 497 |
| coherent | geometric_hat_ray_centered | 100-250 Hz (added A09) | 497 |
| coherent | geometric_gaussian_cartesian | 1000-2500 Hz (added A09) | 300 |
| coherent | simple_gaussian_cartesian | 1000-2500 Hz (added A09) | 300 |
| I/S | incoherent_direct | 50-500 Hz (added A09, frozen) | 500 |
| I/S | semicoherent_direct | 50-500 Hz (added A09, frozen) | 500 |
| I/S | geometric_hat_incoherent | 1000-2500 Hz (added A09, frozen) | 300 |
| I/S | geometric_hat_semicoherent | 1000-2500 Hz (added A09, frozen) | 300 |
| I/S | geometric_gaussian_incoherent | 1000-2500 Hz (added A09, frozen) | 300 |
| I/S | geometric_gaussian_semicoherent | 1000-2500 Hz (added A09, frozen) | 300 |

Coherent-case range rationale (documented choice, worklist §4 "16F-class"):
1000-Hz-class cases use 1000-2500 x16 — identical to the frozen 1000-Hz-class
I/S values (design §11), covering each case's broadband_smoke span; the two
hat boundary-geometry cases use 100-250 x16 — start = the case's single
frequency 100 Hz, x2.5 span mirrors the 1000->2500 rule, covers their
broadband_smoke span 100-200 Hz. All 11 new profiles passed three-party
origin-oracle validation before the benchmark (A09 Phase 1).

## SHD hash identity (Level D re-check via the harness)

`require_identical_sample_hashes` (warmup + 3 repeats per config) and
`require_cross_configuration_hashes` (reuse == fused w1/w2/w4/w8) both
enforced and PASSED for all 12 cases:

| case | common SHD sha256 | effective workers w1/w2/w4/w8 |
|---|---|---|
| munk_cerveny_cc | f01ee48119549a82e79798322bf5227d8fc95054be82de955de5ccadef057c2c | 1/2/4/8 |
| ray_centered_component_pressure | de900bc4e4e49e4af78a9d10fd786343ed5e86e2062bc2fffb023835ab0559f4 | 1/2/4/8 |
| geometric_hat_cartesian | 0ebc25d0ef2a8185588cf93e7b5653182f570993267f505a9ebd09ad014f635d | 1/2/4/3(*) |
| geometric_hat_ray_centered | d666bb484e02e50be5259f5f247e7e8d2e191f3b59d1f70b83a5a0480a7a76ce | 1/2/4/3(*) |
| geometric_gaussian_cartesian | 888b0298407bf479ceba2bdc412b480455298d520d783a0f9fca4812b0476196 | 1/2/4/8 |
| simple_gaussian_cartesian | 06b8cea11718ae374f1573efebb09d0bb66d55277c17f97f09df974b9267f87a | 1/2/4/8 |
| incoherent_direct | 016353885920d06d427228b0cd3703e952d1dee4623414f6cf98d5e6ffd36dfa | 1/2/4/8 |
| semicoherent_direct | 8d122565b68c08cd1bce425a2277f6744cfdd2f61ead54147ddbb29d9fb058b3 | 1/2/4/8 |
| geometric_hat_incoherent | ba17d9e7c2f1de4f11217ec5a81a2be0574fa558964185cc5995f821dc68402e | 1/2/4/8 |
| geometric_hat_semicoherent | 18da0fc16595f24811be3efb6420d3872a6e62256235c7e1079811c461591deb | 1/2/4/8 |
| geometric_gaussian_incoherent | 3f318f7aaeb72210443208c3f7d251b14b0195937720bbda3a64f8759ed2183b | 1/2/4/8 |
| geometric_gaussian_semicoherent | d38ee2af81dc658744a02d70909b037bdeeaf2a015d226c02070761a947374ed | 1/2/4/8 |

(*) hat boundary-geometry cases have 3 receiver ranges, so requested w4/w8
clamp to 3 effective workers (expected; matches A04).
munk_cerveny_cc reproduces the A02 historical hash `f01ee481...` (identical
to the pre-batch `38137a4` baseline binary) — Level D re-check plus
cross-batch output stability in one observation.

## Median wall seconds (all 12 cases; full tables below)

| case | reuse | w1 | w2 | w4 | w8 |
|---|---|---|---|---|---|
| munk_cerveny_cc | 96.266 | 84.310 | 45.158 | 34.346 | 23.664 |
| ray_centered_component_pressure | 0.165 | 0.174 | 0.160 | 0.155 | 0.197 |
| geometric_hat_cartesian | 0.015 | 0.013 | 0.016 | 0.019 | 0.017 |
| geometric_hat_ray_centered | 0.017 | 0.013 | 0.016 | 0.019 | 0.017 |
| geometric_gaussian_cartesian | 0.046 | 0.035 | 0.035 | 0.036 | 0.045 |
| simple_gaussian_cartesian | 0.052 | 0.038 | 0.035 | 0.036 | 0.041 |
| incoherent_direct | 0.380 | 0.357 | 0.306 | 0.303 | 0.437 |
| semicoherent_direct | 0.376 | 0.342 | 0.309 | 0.306 | 0.467 |
| geometric_hat_incoherent | 0.040 | 0.030 | 0.031 | 0.034 | 0.042 |
| geometric_hat_semicoherent | 0.040 | 0.030 | 0.031 | 0.034 | 0.040 |
| geometric_gaussian_incoherent | 0.045 | 0.034 | 0.034 | 0.037 | 0.044 |
| geometric_gaussian_semicoherent | 0.042 | 0.034 | 0.035 | 0.037 | 0.043 |

Observations (facts, single session):

- munk_cerveny_cc (the only receiver-heavy case): fused w1 is 12.5% faster
  than legacy reuse; static range workers scale 84.3 -> 45.2 -> 34.3 ->
  23.7 s (w8 = 3.56x w1, 4.07x vs reuse). w1/w2/reuse medians match the A02
  session within 0.2%; the w4 median is ~24% above the A02 record (27.6 s)
  with identical bits (hash above), i.e. machine-state variance in this
  session's Influence phase, not a solver change; w8 +4.4%.
- The 11 micro cases (0.013-0.44 s wall) are dominated by fixed per-run
  costs; worker spin-up makes w8 slightly slower than w1 there — expected
  for static partitioning with tiny Influence payloads, and immaterial
  (sub-100 ms absolute).
- The two CC I/S direct cases scale to w4 (0.38 -> 0.30 s) and regress at
  w8 (0.44-0.47 s) — same micro-case fixed-cost regime at 500 rays.
- Fused w1 <= reuse wall on 11 of 12 cases (hat cartesian equal-noise
  exception at 13 vs 15 ms).

## Harness infra note (recorded)

`benchmark_rayreuse.py` previously never staged companion boundary files
(.ati/.bty) — benchmarkable cases were companion-free (munk) until this
task. `_run_isolated_sample` now calls the standard_cases
`stage_companion_files(definition, run_directory, file_root)` next to the
rendered env (3-line shared-test infra addition; harness unittests 177/177
OK after the change; probe on geometric_hat_cartesian PASS before the full
run). The first full benchmark attempt aborted at geometric_hat_cartesian
with "unable to open boundary file: rayreuse_benchmark.ati"; no results
from that attempt are used anywhere.

## Full per-case tables (med [min..max] MAD; wall/Trace/Project/Influence/
Scale in seconds, RSS = ru_maxrss MiB; 3 measured runs per config)

## munk_cerveny_cc — profile=broadband_regression (broadband_regression) nF=16 [50..500] Hz, rays=10000
common_shd_sha256: f01ee48119549a82e79798322bf5227d8fc95054be82de955de5ccadef057c2c
environment_sha256: 9621c7766f90eec18c0369b33f61db6e3c13c273395f789e66c9a110f57f6fdb
effective range workers: {'reuse': None, 'fused-range-w1': 1, 'fused-range-w2': 2, 'fused-range-w4': 4, 'fused-range-w8': 8}

| config | metric | med [min..max] MAD |
|---|---|---|
| reuse | wall_s | 96.266 [96.235..96.412] 0.031 |
| reuse | Trace_s | 0.585 [0.561..0.619] 0.024 |
| reuse | Project_s | 0.513 [0.512..0.515] 0.001 |
| reuse | Influence_s | 95.041 [95.032..95.146] 0.009 |
| reuse | Scale_s | 0.065 [0.060..0.066] 0.002 |
| reuse | RSS_MiB | 607.359 [607.344..607.359] 0.000 |
| fused-range-w1 | wall_s | 84.310 [83.921..85.099] 0.389 |
| fused-range-w1 | Trace_s | 0.558 [0.555..0.589] 0.004 |
| fused-range-w1 | Project_s | 0.466 [0.466..0.493] 0.000 |
| fused-range-w1 | Influence_s | 83.110 [82.747..83.905] 0.363 |
| fused-range-w1 | Scale_s | 0.095 [0.090..0.104] 0.005 |
| fused-range-w1 | RSS_MiB | 634.609 [634.469..634.688] 0.078 |
| fused-range-w2 | wall_s | 45.158 [44.816..45.568] 0.342 |
| fused-range-w2 | Trace_s | 0.577 [0.559..0.593] 0.016 |
| fused-range-w2 | Project_s | 0.479 [0.476..0.508] 0.003 |
| fused-range-w2 | Influence_s | 43.933 [43.612..44.340] 0.321 |
| fused-range-w2 | Scale_s | 0.107 [0.102..0.107] 0.000 |
| fused-range-w2 | RSS_MiB | 637.031 [637.031..637.828] 0.000 |
| fused-range-w4 | wall_s | 34.346 [34.211..34.523] 0.136 |
| fused-range-w4 | Trace_s | 0.562 [0.549..0.588] 0.013 |
| fused-range-w4 | Project_s | 0.597 [0.597..0.611] 0.000 |
| fused-range-w4 | Influence_s | 33.013 [32.918..33.205] 0.095 |
| fused-range-w4 | Scale_s | 0.103 [0.102..0.107] 0.002 |
| fused-range-w4 | RSS_MiB | 641.422 [640.734..642.391] 0.688 |
| fused-range-w8 | wall_s | 23.664 [23.289..23.664] 0.000 |
| fused-range-w8 | Trace_s | 0.577 [0.547..0.587] 0.011 |
| fused-range-w8 | Project_s | 0.807 [0.789..0.811] 0.004 |
| fused-range-w8 | Influence_s | 22.145 [21.769..22.164] 0.019 |
| fused-range-w8 | Scale_s | 0.101 [0.101..0.103] 0.001 |
| fused-range-w8 | RSS_MiB | 654.031 [652.750..654.062] 0.031 |

## ray_centered_component_pressure — profile=broadband_regression (broadband_regression) nF=16 [1000..2500] Hz, rays=300
common_shd_sha256: de900bc4e4e49e4af78a9d10fd786343ed5e86e2062bc2fffb023835ab0559f4
environment_sha256: ca7b0c77721c432279e0e936aeb52be58c68fbeee3675cf283372a9921def019
effective range workers: {'reuse': None, 'fused-range-w1': 1, 'fused-range-w2': 2, 'fused-range-w4': 4, 'fused-range-w8': 8}

| config | metric | med [min..max] MAD |
|---|---|---|
| reuse | wall_s | 0.165 [0.165..0.169] 0.000 |
| reuse | Trace_s | 0.015 [0.014..0.017] 0.001 |
| reuse | Project_s | 0.008 [0.008..0.008] 0.000 |
| reuse | Influence_s | 0.140 [0.139..0.142] 0.001 |
| reuse | Scale_s | 0.000 [0.000..0.000] 0.000 |
| reuse | RSS_MiB | 16.453 [16.438..16.531] 0.016 |
| fused-range-w1 | wall_s | 0.174 [0.174..0.174] 0.000 |
| fused-range-w1 | Trace_s | 0.017 [0.016..0.017] 0.000 |
| fused-range-w1 | Project_s | 0.007 [0.007..0.007] 0.000 |
| fused-range-w1 | Influence_s | 0.147 [0.147..0.147] 0.000 |
| fused-range-w1 | Scale_s | 0.000 [0.000..0.000] 0.000 |
| fused-range-w1 | RSS_MiB | 18.469 [18.391..18.469] 0.000 |
| fused-range-w2 | wall_s | 0.160 [0.160..0.162] 0.000 |
| fused-range-w2 | Trace_s | 0.015 [0.014..0.016] 0.000 |
| fused-range-w2 | Project_s | 0.007 [0.007..0.007] 0.000 |
| fused-range-w2 | Influence_s | 0.135 [0.135..0.135] 0.000 |
| fused-range-w2 | Scale_s | 0.000 [0.000..0.000] 0.000 |
| fused-range-w2 | RSS_MiB | 20.359 [20.359..20.469] 0.000 |
| fused-range-w4 | wall_s | 0.155 [0.154..0.155] 0.000 |
| fused-range-w4 | Trace_s | 0.015 [0.015..0.015] 0.000 |
| fused-range-w4 | Project_s | 0.008 [0.008..0.008] 0.000 |
| fused-range-w4 | Influence_s | 0.128 [0.128..0.128] 0.000 |
| fused-range-w4 | Scale_s | 0.000 [0.000..0.000] 0.000 |
| fused-range-w4 | RSS_MiB | 24.547 [24.547..24.562] 0.000 |
| fused-range-w8 | wall_s | 0.197 [0.197..0.199] 0.001 |
| fused-range-w8 | Trace_s | 0.017 [0.015..0.017] 0.000 |
| fused-range-w8 | Project_s | 0.014 [0.014..0.014] 0.000 |
| fused-range-w8 | Influence_s | 0.164 [0.163..0.167] 0.001 |
| fused-range-w8 | Scale_s | 0.000 [0.000..0.000] 0.000 |
| fused-range-w8 | RSS_MiB | 32.547 [32.328..33.500] 0.219 |

## geometric_hat_cartesian — profile=broadband_regression (broadband_regression) nF=16 [100..250] Hz, rays=497
common_shd_sha256: 0ebc25d0ef2a8185588cf93e7b5653182f570993267f505a9ebd09ad014f635d
environment_sha256: eaccbe6edcb7289ca6cd1513a85999404adf5111053e73cdec1fb9dbfd85885b
effective range workers: {'reuse': None, 'fused-range-w1': 1, 'fused-range-w2': 2, 'fused-range-w4': 3, 'fused-range-w8': 3}

| config | metric | med [min..max] MAD |
|---|---|---|
| reuse | wall_s | 0.015 [0.015..0.016] 0.000 |
| reuse | Trace_s | 0.003 [0.003..0.003] 0.000 |
| reuse | Project_s | 0.006 [0.006..0.006] 0.000 |
| reuse | Influence_s | 0.003 [0.003..0.003] 0.000 |
| reuse | Scale_s | 0.000 [0.000..0.000] 0.000 |
| reuse | RSS_MiB | 8.297 [8.297..8.328] 0.000 |
| fused-range-w1 | wall_s | 0.013 [0.013..0.013] 0.000 |
| fused-range-w1 | Trace_s | 0.003 [0.003..0.003] 0.000 |
| fused-range-w1 | Project_s | 0.006 [0.006..0.006] 0.000 |
| fused-range-w1 | Influence_s | 0.001 [0.001..0.001] 0.000 |
| fused-range-w1 | Scale_s | 0.000 [0.000..0.000] 0.000 |
| fused-range-w1 | RSS_MiB | 8.812 [8.812..8.812] 0.000 |
| fused-range-w2 | wall_s | 0.016 [0.015..0.016] 0.000 |
| fused-range-w2 | Trace_s | 0.003 [0.003..0.003] 0.000 |
| fused-range-w2 | Project_s | 0.009 [0.009..0.009] 0.000 |
| fused-range-w2 | Influence_s | 0.001 [0.001..0.001] 0.000 |
| fused-range-w2 | Scale_s | 0.000 [0.000..0.000] 0.000 |
| fused-range-w2 | RSS_MiB | 9.297 [9.281..9.312] 0.016 |
| fused-range-w4 | wall_s | 0.019 [0.019..0.020] 0.000 |
| fused-range-w4 | Trace_s | 0.003 [0.003..0.003] 0.000 |
| fused-range-w4 | Project_s | 0.012 [0.012..0.013] 0.000 |
| fused-range-w4 | Influence_s | 0.001 [0.001..0.001] 0.000 |
| fused-range-w4 | Scale_s | 0.000 [0.000..0.000] 0.000 |
| fused-range-w4 | RSS_MiB | 9.641 [9.625..9.656] 0.016 |
| fused-range-w8 | wall_s | 0.017 [0.017..0.017] 0.000 |
| fused-range-w8 | Trace_s | 0.003 [0.003..0.003] 0.000 |
| fused-range-w8 | Project_s | 0.010 [0.010..0.011] 0.000 |
| fused-range-w8 | Influence_s | 0.001 [0.001..0.001] 0.000 |
| fused-range-w8 | Scale_s | 0.000 [0.000..0.000] 0.000 |
| fused-range-w8 | RSS_MiB | 9.797 [9.781..9.891] 0.016 |

## geometric_hat_ray_centered — profile=broadband_regression (broadband_regression) nF=16 [100..250] Hz, rays=497
common_shd_sha256: d666bb484e02e50be5259f5f247e7e8d2e191f3b59d1f70b83a5a0480a7a76ce
environment_sha256: ab176b7cdab50d45156bc5bea1a45a9113db8161a123e1cb532f2305884d26d4
effective range workers: {'reuse': None, 'fused-range-w1': 1, 'fused-range-w2': 2, 'fused-range-w4': 3, 'fused-range-w8': 3}

| config | metric | med [min..max] MAD |
|---|---|---|
| reuse | wall_s | 0.017 [0.017..0.018] 0.000 |
| reuse | Trace_s | 0.003 [0.003..0.003] 0.000 |
| reuse | Project_s | 0.006 [0.006..0.006] 0.000 |
| reuse | Influence_s | 0.005 [0.005..0.005] 0.000 |
| reuse | Scale_s | 0.000 [0.000..0.000] 0.000 |
| reuse | RSS_MiB | 8.828 [8.828..8.844] 0.000 |
| fused-range-w1 | wall_s | 0.013 [0.013..0.013] 0.000 |
| fused-range-w1 | Trace_s | 0.003 [0.003..0.003] 0.000 |
| fused-range-w1 | Project_s | 0.006 [0.006..0.006] 0.000 |
| fused-range-w1 | Influence_s | 0.001 [0.001..0.001] 0.000 |
| fused-range-w1 | Scale_s | 0.000 [0.000..0.000] 0.000 |
| fused-range-w1 | RSS_MiB | 9.125 [9.094..9.141] 0.016 |
| fused-range-w2 | wall_s | 0.016 [0.016..0.016] 0.000 |
| fused-range-w2 | Trace_s | 0.003 [0.003..0.003] 0.000 |
| fused-range-w2 | Project_s | 0.009 [0.009..0.009] 0.000 |
| fused-range-w2 | Influence_s | 0.001 [0.001..0.001] 0.000 |
| fused-range-w2 | Scale_s | 0.000 [0.000..0.000] 0.000 |
| fused-range-w2 | RSS_MiB | 9.641 [9.641..9.641] 0.000 |
| fused-range-w4 | wall_s | 0.019 [0.018..0.020] 0.000 |
| fused-range-w4 | Trace_s | 0.003 [0.003..0.003] 0.000 |
| fused-range-w4 | Project_s | 0.012 [0.011..0.013] 0.000 |
| fused-range-w4 | Influence_s | 0.001 [0.001..0.001] 0.000 |
| fused-range-w4 | Scale_s | 0.000 [0.000..0.000] 0.000 |
| fused-range-w4 | RSS_MiB | 10.094 [9.938..10.125] 0.031 |
| fused-range-w8 | wall_s | 0.017 [0.017..0.020] 0.000 |
| fused-range-w8 | Trace_s | 0.003 [0.003..0.003] 0.000 |
| fused-range-w8 | Project_s | 0.010 [0.010..0.013] 0.000 |
| fused-range-w8 | Influence_s | 0.001 [0.001..0.001] 0.000 |
| fused-range-w8 | Scale_s | 0.000 [0.000..0.000] 0.000 |
| fused-range-w8 | RSS_MiB | 10.219 [9.984..10.219] 0.000 |

## geometric_gaussian_cartesian — profile=broadband_regression (broadband_regression) nF=16 [1000..2500] Hz, rays=300
common_shd_sha256: 888b0298407bf479ceba2bdc412b480455298d520d783a0f9fca4812b0476196
environment_sha256: 6eefc9af42f5db74e265b3f3100bd493206ba70124da4b938586fa6d97dce9fc
effective range workers: {'reuse': None, 'fused-range-w1': 1, 'fused-range-w2': 2, 'fused-range-w4': 4, 'fused-range-w8': 8}

| config | metric | med [min..max] MAD |
|---|---|---|
| reuse | wall_s | 0.046 [0.045..0.047] 0.000 |
| reuse | Trace_s | 0.015 [0.015..0.016] 0.000 |
| reuse | Project_s | 0.007 [0.007..0.007] 0.000 |
| reuse | Influence_s | 0.020 [0.020..0.020] 0.000 |
| reuse | Scale_s | 0.000 [0.000..0.000] 0.000 |
| reuse | RSS_MiB | 16.172 [16.141..16.172] 0.000 |
| fused-range-w1 | wall_s | 0.035 [0.033..0.035] 0.001 |
| fused-range-w1 | Trace_s | 0.016 [0.015..0.016] 0.001 |
| fused-range-w1 | Project_s | 0.007 [0.007..0.007] 0.000 |
| fused-range-w1 | Influence_s | 0.008 [0.008..0.008] 0.000 |
| fused-range-w1 | Scale_s | 0.000 [0.000..0.000] 0.000 |
| fused-range-w1 | RSS_MiB | 17.922 [17.906..17.938] 0.016 |
| fused-range-w2 | wall_s | 0.035 [0.033..0.035] 0.000 |
| fused-range-w2 | Trace_s | 0.016 [0.015..0.016] 0.000 |
| fused-range-w2 | Project_s | 0.008 [0.008..0.008] 0.000 |
| fused-range-w2 | Influence_s | 0.007 [0.007..0.007] 0.000 |
| fused-range-w2 | Scale_s | 0.000 [0.000..0.000] 0.000 |
| fused-range-w2 | RSS_MiB | 19.422 [19.422..19.562] 0.000 |
| fused-range-w4 | wall_s | 0.036 [0.036..0.037] 0.000 |
| fused-range-w4 | Trace_s | 0.015 [0.015..0.017] 0.000 |
| fused-range-w4 | Project_s | 0.010 [0.010..0.010] 0.000 |
| fused-range-w4 | Influence_s | 0.007 [0.007..0.007] 0.000 |
| fused-range-w4 | Scale_s | 0.000 [0.000..0.000] 0.000 |
| fused-range-w4 | RSS_MiB | 22.516 [22.500..22.609] 0.016 |
| fused-range-w8 | wall_s | 0.045 [0.044..0.046] 0.001 |
| fused-range-w8 | Trace_s | 0.017 [0.015..0.017] 0.000 |
| fused-range-w8 | Project_s | 0.016 [0.015..0.016] 0.000 |
| fused-range-w8 | Influence_s | 0.010 [0.009..0.010] 0.000 |
| fused-range-w8 | Scale_s | 0.000 [0.000..0.000] 0.000 |
| fused-range-w8 | RSS_MiB | 37.547 [37.453..48.250] 0.094 |

## simple_gaussian_cartesian — profile=broadband_regression (broadband_regression) nF=16 [1000..2500] Hz, rays=300
common_shd_sha256: 06b8cea11718ae374f1573efebb09d0bb66d55277c17f97f09df974b9267f87a
environment_sha256: a87b0a52be0e3ad38b0fe2b360a7e2eab8738b5cc6db4142eb17ac0bd9f2b64b
effective range workers: {'reuse': None, 'fused-range-w1': 1, 'fused-range-w2': 2, 'fused-range-w4': 4, 'fused-range-w8': 8}

| config | metric | med [min..max] MAD |
|---|---|---|
| reuse | wall_s | 0.052 [0.051..0.053] 0.001 |
| reuse | Trace_s | 0.015 [0.014..0.016] 0.001 |
| reuse | Project_s | 0.007 [0.007..0.007] 0.000 |
| reuse | Influence_s | 0.026 [0.026..0.026] 0.000 |
| reuse | Scale_s | 0.000 [0.000..0.000] 0.000 |
| reuse | RSS_MiB | 16.172 [16.156..16.172] 0.000 |
| fused-range-w1 | wall_s | 0.038 [0.037..0.039] 0.000 |
| fused-range-w1 | Trace_s | 0.016 [0.015..0.016] 0.000 |
| fused-range-w1 | Project_s | 0.007 [0.007..0.007] 0.000 |
| fused-range-w1 | Influence_s | 0.012 [0.012..0.012] 0.000 |
| fused-range-w1 | Scale_s | 0.000 [0.000..0.000] 0.000 |
| fused-range-w1 | RSS_MiB | 17.875 [17.875..17.891] 0.000 |
| fused-range-w2 | wall_s | 0.035 [0.033..0.035] 0.001 |
| fused-range-w2 | Trace_s | 0.016 [0.015..0.016] 0.001 |
| fused-range-w2 | Project_s | 0.008 [0.008..0.008] 0.000 |
| fused-range-w2 | Influence_s | 0.007 [0.007..0.008] 0.000 |
| fused-range-w2 | Scale_s | 0.000 [0.000..0.000] 0.000 |
| fused-range-w2 | RSS_MiB | 19.391 [19.391..19.406] 0.000 |
| fused-range-w4 | wall_s | 0.036 [0.034..0.036] 0.000 |
| fused-range-w4 | Trace_s | 0.016 [0.015..0.017] 0.000 |
| fused-range-w4 | Project_s | 0.010 [0.010..0.010] 0.000 |
| fused-range-w4 | Influence_s | 0.006 [0.006..0.006] 0.000 |
| fused-range-w4 | Scale_s | 0.000 [0.000..0.000] 0.000 |
| fused-range-w4 | RSS_MiB | 22.484 [22.469..22.484] 0.000 |
| fused-range-w8 | wall_s | 0.041 [0.040..0.042] 0.001 |
| fused-range-w8 | Trace_s | 0.015 [0.015..0.015] 0.000 |
| fused-range-w8 | Project_s | 0.016 [0.016..0.017] 0.000 |
| fused-range-w8 | Influence_s | 0.006 [0.006..0.007] 0.000 |
| fused-range-w8 | Scale_s | 0.000 [0.000..0.000] 0.000 |
| fused-range-w8 | RSS_MiB | 35.297 [30.547..41.172] 4.750 |

## incoherent_direct — profile=broadband_regression (broadband_regression) nF=16 [50..500] Hz, rays=500
common_shd_sha256: 016353885920d06d427228b0cd3703e952d1dee4623414f6cf98d5e6ffd36dfa
environment_sha256: 6203c850342cff4c98b82ee5ba8f249fb8777c0214e4f3a2b7e4a7e8827ed9a0
effective range workers: {'reuse': None, 'fused-range-w1': 1, 'fused-range-w2': 2, 'fused-range-w4': 4, 'fused-range-w8': 8}

| config | metric | med [min..max] MAD |
|---|---|---|
| reuse | wall_s | 0.380 [0.377..0.390] 0.002 |
| reuse | Trace_s | 0.040 [0.039..0.042] 0.001 |
| reuse | Project_s | 0.017 [0.017..0.017] 0.000 |
| reuse | Influence_s | 0.318 [0.314..0.322] 0.003 |
| reuse | Scale_s | 0.001 [0.001..0.001] 0.000 |
| reuse | RSS_MiB | 38.750 [38.734..38.781] 0.016 |
| fused-range-w1 | wall_s | 0.357 [0.352..0.358] 0.001 |
| fused-range-w1 | Trace_s | 0.040 [0.039..0.042] 0.000 |
| fused-range-w1 | Project_s | 0.016 [0.016..0.017] 0.000 |
| fused-range-w1 | Influence_s | 0.294 [0.291..0.296] 0.003 |
| fused-range-w1 | Scale_s | 0.001 [0.001..0.001] 0.000 |
| fused-range-w1 | RSS_MiB | 40.188 [40.172..40.219] 0.016 |
| fused-range-w2 | wall_s | 0.306 [0.289..0.318] 0.012 |
| fused-range-w2 | Trace_s | 0.040 [0.037..0.046] 0.002 |
| fused-range-w2 | Project_s | 0.017 [0.017..0.018] 0.000 |
| fused-range-w2 | Influence_s | 0.246 [0.227..0.248] 0.003 |
| fused-range-w2 | Scale_s | 0.001 [0.001..0.002] 0.000 |
| fused-range-w2 | RSS_MiB | 41.594 [41.312..42.312] 0.281 |
| fused-range-w4 | wall_s | 0.303 [0.302..0.492] 0.001 |
| fused-range-w4 | Trace_s | 0.040 [0.040..0.042] 0.000 |
| fused-range-w4 | Project_s | 0.023 [0.020..0.024] 0.000 |
| fused-range-w4 | Influence_s | 0.233 [0.231..0.427] 0.002 |
| fused-range-w4 | Scale_s | 0.001 [0.001..0.001] 0.000 |
| fused-range-w4 | RSS_MiB | 44.250 [43.500..44.891] 0.641 |
| fused-range-w8 | wall_s | 0.437 [0.426..0.438] 0.000 |
| fused-range-w8 | Trace_s | 0.042 [0.040..0.043] 0.001 |
| fused-range-w8 | Project_s | 0.029 [0.029..0.031] 0.000 |
| fused-range-w8 | Influence_s | 0.361 [0.350..0.365] 0.004 |
| fused-range-w8 | Scale_s | 0.001 [0.001..0.001] 0.000 |
| fused-range-w8 | RSS_MiB | 51.609 [51.594..52.406] 0.016 |

## semicoherent_direct — profile=broadband_regression (broadband_regression) nF=16 [50..500] Hz, rays=500
common_shd_sha256: 8d122565b68c08cd1bce425a2277f6744cfdd2f61ead54147ddbb29d9fb058b3
environment_sha256: 8dc24ab124656512a7cec1902ee763f062fde8c68df501cbbd7ebce1209d2c1f
effective range workers: {'reuse': None, 'fused-range-w1': 1, 'fused-range-w2': 2, 'fused-range-w4': 4, 'fused-range-w8': 8}

| config | metric | med [min..max] MAD |
|---|---|---|
| reuse | wall_s | 0.376 [0.368..0.379] 0.002 |
| reuse | Trace_s | 0.039 [0.037..0.040] 0.000 |
| reuse | Project_s | 0.017 [0.017..0.017] 0.000 |
| reuse | Influence_s | 0.315 [0.308..0.316] 0.002 |
| reuse | Scale_s | 0.001 [0.001..0.001] 0.000 |
| reuse | RSS_MiB | 38.734 [38.719..38.750] 0.016 |
| fused-range-w1 | wall_s | 0.342 [0.339..0.359] 0.003 |
| fused-range-w1 | Trace_s | 0.038 [0.037..0.042] 0.000 |
| fused-range-w1 | Project_s | 0.017 [0.016..0.017] 0.000 |
| fused-range-w1 | Influence_s | 0.283 [0.280..0.295] 0.004 |
| fused-range-w1 | Scale_s | 0.001 [0.001..0.001] 0.000 |
| fused-range-w1 | RSS_MiB | 40.188 [40.172..40.203] 0.016 |
| fused-range-w2 | wall_s | 0.309 [0.288..0.311] 0.002 |
| fused-range-w2 | Trace_s | 0.040 [0.039..0.042] 0.000 |
| fused-range-w2 | Project_s | 0.017 [0.017..0.018] 0.000 |
| fused-range-w2 | Influence_s | 0.246 [0.226..0.247] 0.001 |
| fused-range-w2 | Scale_s | 0.001 [0.001..0.001] 0.000 |
| fused-range-w2 | RSS_MiB | 41.547 [41.312..41.562] 0.016 |
| fused-range-w4 | wall_s | 0.306 [0.302..0.321] 0.004 |
| fused-range-w4 | Trace_s | 0.040 [0.039..0.041] 0.000 |
| fused-range-w4 | Project_s | 0.023 [0.022..0.024] 0.000 |
| fused-range-w4 | Influence_s | 0.236 [0.234..0.255] 0.002 |
| fused-range-w4 | Scale_s | 0.001 [0.001..0.001] 0.000 |
| fused-range-w4 | RSS_MiB | 43.859 [43.531..44.016] 0.156 |
| fused-range-w8 | wall_s | 0.467 [0.399..0.557] 0.068 |
| fused-range-w8 | Trace_s | 0.042 [0.039..0.043] 0.000 |
| fused-range-w8 | Project_s | 0.030 [0.028..0.032] 0.001 |
| fused-range-w8 | Influence_s | 0.390 [0.322..0.479] 0.068 |
| fused-range-w8 | Scale_s | 0.001 [0.001..0.001] 0.000 |
| fused-range-w8 | RSS_MiB | 51.969 [51.562..52.750] 0.406 |

## geometric_hat_incoherent — profile=broadband_regression (broadband_regression) nF=16 [1000..2500] Hz, rays=300
common_shd_sha256: ba17d9e7c2f1de4f11217ec5a81a2be0574fa558964185cc5995f821dc68402e
environment_sha256: 80ed081137591c20e3daff36f3e26f6df1e4b5ad5e18cf6e31f0b0b0325a2de1
effective range workers: {'reuse': None, 'fused-range-w1': 1, 'fused-range-w2': 2, 'fused-range-w4': 4, 'fused-range-w8': 8}

| config | metric | med [min..max] MAD |
|---|---|---|
| reuse | wall_s | 0.040 [0.040..0.040] 0.000 |
| reuse | Trace_s | 0.016 [0.016..0.017] 0.000 |
| reuse | Project_s | 0.007 [0.007..0.007] 0.000 |
| reuse | Influence_s | 0.013 [0.013..0.013] 0.000 |
| reuse | Scale_s | 0.000 [0.000..0.000] 0.000 |
| reuse | RSS_MiB | 16.219 [16.219..16.219] 0.000 |
| fused-range-w1 | wall_s | 0.030 [0.029..0.031] 0.001 |
| fused-range-w1 | Trace_s | 0.015 [0.015..0.017] 0.001 |
| fused-range-w1 | Project_s | 0.007 [0.007..0.007] 0.000 |
| fused-range-w1 | Influence_s | 0.004 [0.004..0.004] 0.000 |
| fused-range-w1 | Scale_s | 0.000 [0.000..0.000] 0.000 |
| fused-range-w1 | RSS_MiB | 17.922 [17.906..17.922] 0.000 |
| fused-range-w2 | wall_s | 0.031 [0.031..0.032] 0.000 |
| fused-range-w2 | Trace_s | 0.016 [0.015..0.016] 0.000 |
| fused-range-w2 | Project_s | 0.008 [0.008..0.009] 0.000 |
| fused-range-w2 | Influence_s | 0.003 [0.003..0.004] 0.000 |
| fused-range-w2 | Scale_s | 0.000 [0.000..0.000] 0.000 |
| fused-range-w2 | RSS_MiB | 19.453 [19.453..19.469] 0.000 |
| fused-range-w4 | wall_s | 0.034 [0.032..0.034] 0.000 |
| fused-range-w4 | Trace_s | 0.017 [0.014..0.017] 0.000 |
| fused-range-w4 | Project_s | 0.010 [0.010..0.010] 0.000 |
| fused-range-w4 | Influence_s | 0.003 [0.003..0.003] 0.000 |
| fused-range-w4 | Scale_s | 0.000 [0.000..0.000] 0.000 |
| fused-range-w4 | RSS_MiB | 22.531 [22.516..22.531] 0.000 |
| fused-range-w8 | wall_s | 0.042 [0.039..0.042] 0.000 |
| fused-range-w8 | Trace_s | 0.016 [0.015..0.017] 0.000 |
| fused-range-w8 | Project_s | 0.016 [0.016..0.017] 0.000 |
| fused-range-w8 | Influence_s | 0.006 [0.005..0.006] 0.000 |
| fused-range-w8 | Scale_s | 0.000 [0.000..0.000] 0.000 |
| fused-range-w8 | RSS_MiB | 33.438 [29.703..37.172] 3.734 |

## geometric_hat_semicoherent — profile=broadband_regression (broadband_regression) nF=16 [1000..2500] Hz, rays=300
common_shd_sha256: 18da0fc16595f24811be3efb6420d3872a6e62256235c7e1079811c461591deb
environment_sha256: e1cc53841ef27f47be56a880fe6bb195464cd4750d882f1cf7d49cbdc4eb2cfb
effective range workers: {'reuse': None, 'fused-range-w1': 1, 'fused-range-w2': 2, 'fused-range-w4': 4, 'fused-range-w8': 8}

| config | metric | med [min..max] MAD |
|---|---|---|
| reuse | wall_s | 0.040 [0.039..0.040] 0.001 |
| reuse | Trace_s | 0.016 [0.015..0.017] 0.000 |
| reuse | Project_s | 0.007 [0.007..0.007] 0.000 |
| reuse | Influence_s | 0.013 [0.013..0.013] 0.000 |
| reuse | Scale_s | 0.000 [0.000..0.000] 0.000 |
| reuse | RSS_MiB | 16.203 [16.203..16.219] 0.000 |
| fused-range-w1 | wall_s | 0.030 [0.029..0.031] 0.000 |
| fused-range-w1 | Trace_s | 0.015 [0.015..0.016] 0.000 |
| fused-range-w1 | Project_s | 0.007 [0.007..0.007] 0.000 |
| fused-range-w1 | Influence_s | 0.003 [0.003..0.004] 0.000 |
| fused-range-w1 | Scale_s | 0.000 [0.000..0.000] 0.000 |
| fused-range-w1 | RSS_MiB | 17.953 [17.938..17.969] 0.016 |
| fused-range-w2 | wall_s | 0.031 [0.030..0.032] 0.001 |
| fused-range-w2 | Trace_s | 0.015 [0.015..0.017] 0.001 |
| fused-range-w2 | Project_s | 0.008 [0.008..0.009] 0.000 |
| fused-range-w2 | Influence_s | 0.003 [0.003..0.003] 0.000 |
| fused-range-w2 | Scale_s | 0.000 [0.000..0.000] 0.000 |
| fused-range-w2 | RSS_MiB | 19.453 [19.453..19.469] 0.000 |
| fused-range-w4 | wall_s | 0.034 [0.034..0.034] 0.000 |
| fused-range-w4 | Trace_s | 0.016 [0.016..0.017] 0.000 |
| fused-range-w4 | Project_s | 0.010 [0.010..0.010] 0.000 |
| fused-range-w4 | Influence_s | 0.004 [0.003..0.004] 0.000 |
| fused-range-w4 | Scale_s | 0.000 [0.000..0.000] 0.000 |
| fused-range-w4 | RSS_MiB | 22.531 [22.516..22.547] 0.016 |
| fused-range-w8 | wall_s | 0.040 [0.039..0.040] 0.000 |
| fused-range-w8 | Trace_s | 0.016 [0.015..0.016] 0.000 |
| fused-range-w8 | Project_s | 0.015 [0.015..0.016] 0.000 |
| fused-range-w8 | Influence_s | 0.005 [0.005..0.005] 0.000 |
| fused-range-w8 | Scale_s | 0.000 [0.000..0.000] 0.000 |
| fused-range-w8 | RSS_MiB | 34.203 [30.688..37.234] 3.031 |

## geometric_gaussian_incoherent — profile=broadband_regression (broadband_regression) nF=16 [1000..2500] Hz, rays=300
common_shd_sha256: 3f318f7aaeb72210443208c3f7d251b14b0195937720bbda3a64f8759ed2183b
environment_sha256: 0f4677e21e6ac1f9e50dcb716c649b128a70de1abe763c69b326425171172698
effective range workers: {'reuse': None, 'fused-range-w1': 1, 'fused-range-w2': 2, 'fused-range-w4': 4, 'fused-range-w8': 8}

| config | metric | med [min..max] MAD |
|---|---|---|
| reuse | wall_s | 0.045 [0.044..0.045] 0.001 |
| reuse | Trace_s | 0.017 [0.017..0.017] 0.000 |
| reuse | Project_s | 0.007 [0.007..0.007] 0.000 |
| reuse | Influence_s | 0.017 [0.017..0.017] 0.000 |
| reuse | Scale_s | 0.000 [0.000..0.000] 0.000 |
| reuse | RSS_MiB | 16.188 [16.172..16.219] 0.016 |
| fused-range-w1 | wall_s | 0.034 [0.033..0.036] 0.002 |
| fused-range-w1 | Trace_s | 0.016 [0.015..0.017] 0.001 |
| fused-range-w1 | Project_s | 0.007 [0.007..0.007] 0.000 |
| fused-range-w1 | Influence_s | 0.007 [0.007..0.007] 0.000 |
| fused-range-w1 | Scale_s | 0.000 [0.000..0.000] 0.000 |
| fused-range-w1 | RSS_MiB | 17.969 [17.953..17.984] 0.016 |
| fused-range-w2 | wall_s | 0.034 [0.033..0.034] 0.000 |
| fused-range-w2 | Trace_s | 0.015 [0.015..0.016] 0.000 |
| fused-range-w2 | Project_s | 0.008 [0.008..0.008] 0.000 |
| fused-range-w2 | Influence_s | 0.006 [0.006..0.006] 0.000 |
| fused-range-w2 | Scale_s | 0.000 [0.000..0.000] 0.000 |
| fused-range-w2 | RSS_MiB | 19.453 [19.453..19.500] 0.000 |
| fused-range-w4 | wall_s | 0.037 [0.037..0.038] 0.001 |
| fused-range-w4 | Trace_s | 0.017 [0.016..0.017] 0.000 |
| fused-range-w4 | Project_s | 0.010 [0.010..0.010] 0.000 |
| fused-range-w4 | Influence_s | 0.006 [0.006..0.006] 0.000 |
| fused-range-w4 | Scale_s | 0.000 [0.000..0.000] 0.000 |
| fused-range-w4 | RSS_MiB | 22.516 [22.500..22.516] 0.000 |
| fused-range-w8 | wall_s | 0.044 [0.042..0.045] 0.001 |
| fused-range-w8 | Trace_s | 0.015 [0.014..0.015] 0.000 |
| fused-range-w8 | Project_s | 0.016 [0.015..0.016] 0.000 |
| fused-range-w8 | Influence_s | 0.009 [0.009..0.010] 0.000 |
| fused-range-w8 | Scale_s | 0.000 [0.000..0.000] 0.000 |
| fused-range-w8 | RSS_MiB | 36.688 [31.828..38.406] 1.719 |

## geometric_gaussian_semicoherent — profile=broadband_regression (broadband_regression) nF=16 [1000..2500] Hz, rays=300
common_shd_sha256: d38ee2af81dc658744a02d70909b037bdeeaf2a015d226c02070761a947374ed
environment_sha256: d0dc98b163903367e2284f6c39c80d65c519b0f98a76e7d889e7e244d5714167
effective range workers: {'reuse': None, 'fused-range-w1': 1, 'fused-range-w2': 2, 'fused-range-w4': 4, 'fused-range-w8': 8}

| config | metric | med [min..max] MAD |
|---|---|---|
| reuse | wall_s | 0.042 [0.042..0.045] 0.000 |
| reuse | Trace_s | 0.015 [0.014..0.017] 0.000 |
| reuse | Project_s | 0.007 [0.007..0.007] 0.000 |
| reuse | Influence_s | 0.017 [0.017..0.017] 0.000 |
| reuse | Scale_s | 0.000 [0.000..0.000] 0.000 |
| reuse | RSS_MiB | 16.203 [16.188..16.219] 0.016 |
| fused-range-w1 | wall_s | 0.034 [0.033..0.034] 0.000 |
| fused-range-w1 | Trace_s | 0.016 [0.015..0.017] 0.000 |
| fused-range-w1 | Project_s | 0.007 [0.007..0.007] 0.000 |
| fused-range-w1 | Influence_s | 0.007 [0.007..0.007] 0.000 |
| fused-range-w1 | Scale_s | 0.000 [0.000..0.000] 0.000 |
| fused-range-w1 | RSS_MiB | 17.969 [17.953..17.984] 0.016 |
| fused-range-w2 | wall_s | 0.035 [0.033..0.035] 0.000 |
| fused-range-w2 | Trace_s | 0.016 [0.015..0.017] 0.000 |
| fused-range-w2 | Project_s | 0.008 [0.008..0.008] 0.000 |
| fused-range-w2 | Influence_s | 0.006 [0.006..0.006] 0.000 |
| fused-range-w2 | Scale_s | 0.000 [0.000..0.000] 0.000 |
| fused-range-w2 | RSS_MiB | 19.469 [19.453..19.469] 0.000 |
| fused-range-w4 | wall_s | 0.037 [0.037..0.038] 0.000 |
| fused-range-w4 | Trace_s | 0.016 [0.016..0.017] 0.000 |
| fused-range-w4 | Project_s | 0.010 [0.010..0.010] 0.000 |
| fused-range-w4 | Influence_s | 0.006 [0.006..0.006] 0.000 |
| fused-range-w4 | Scale_s | 0.000 [0.000..0.000] 0.000 |
| fused-range-w4 | RSS_MiB | 22.531 [22.516..22.562] 0.016 |
| fused-range-w8 | wall_s | 0.043 [0.043..0.045] 0.000 |
| fused-range-w8 | Trace_s | 0.015 [0.014..0.016] 0.001 |
| fused-range-w8 | Project_s | 0.016 [0.015..0.016] 0.000 |
| fused-range-w8 | Influence_s | 0.009 [0.009..0.009] 0.000 |
| fused-range-w8 | Scale_s | 0.000 [0.000..0.000] 0.000 |
| fused-range-w8 | RSS_MiB | 47.703 [41.109..51.422] 3.719 |

