# IGR-1 R01 — Baseline Instrumentation Report (Influence counter split)

Date: 2026-09-02
Branch: `feat/igr-influence-geometry-reuse` (HEAD `3ed475e` + R01 instrumentation diff)
Build: `Bellhop_RayReuse/build/igr1-clean` (release), binary SHA256
`a9717477d1df64f8326fef41d75fd89da4fb0f06067e4f4743315df8c3461e5a`
Machine-readable archive: `Bellhop_RayReuse/build/igr1-clean/baseline/igr1_r01_baseline.json`

## 1. Purpose

Split the `--profile-influence` counters into geometry-side (frequency-independent
traversal work) and frequency-kernel-side (per-frequency work on prepared
geometry) groups, without changing any numerical code path, and record the
current reuse-mode baseline at 2F / 8F / 16F for later IGR-1 comparison (R06).
This report contains no performance conclusions; interpretation is R06 scope.

## 2. Method

Instrumentation only:

- `CartesianCervenyStatistics` gains 6 counters; legacy counters keep their
  increment sites and semantics unchanged.
- All new increments sit inside existing `if constexpr (CollectStatistics)`
  blocks; the statistics path stays opt-in (no statistics pointer -> zero
  counters, asserted in the component test).
- PRT output prints the 6 new lines after the existing counters;
  `benchmark_rayreuse.py::parse_prt_metrics` records them as OPTIONAL integer
  fields (PRT files without them still parse).

### Counter definitions (current frequency-major kernel)

| Counter | Increment site | Legacy counterpart (equal count today) |
|---|---|---|
| `geometrySegmentEvaluations` | segment-candidate entry | `segmentCandidates` |
| `geometryRangeEvaluations` | crossed-range entry | `receiverRangeEvaluations` |
| `geometryDepthEvaluations` | per-depth entry | `receiverDepthEvaluations` |
| `geometryImageGeometryEvaluations` | per image, after shared Δz/polarity | `imageEvaluations` |
| `frequencyRangeKernelEvaluations` | per crossed range, after shared position/slowness/sound-speed interpolation (before q/τ/γ) | `receiverRangeEvaluations` |
| `frequencyImageKernelEvaluations` | per image (window/taper/phase/exponential kernel) | `imageEvaluations` |

In the current reuse kernel the whole Influence traversal runs once per
frequency, so each new counter coincides in count with its legacy counterpart.
The baseline records that pre-fusion state; it is a counting fact, not a
performance claim.

## 3. Byte-identity gate (R01 hard gate)

SHD SHA256 before vs after instrumentation, same inputs, reuse mode,
`--verify-cache --profile-influence`:

| Case | Expected (pre-R01 binary) | Observed (post-R01 binary) | Result |
|---|---|---|---|
| `munk_cerveny_cc` broadband_regression 16F | `f01ee48119549a82e79798322bf5227d8fc95054be82de955de5ccadef057c2c` | `f01ee48119549a82e79798322bf5227d8fc95054be82de955de5ccadef057c2c` | PASS |
| `constant_speed_direct` broadband_regression 16F | `edc818ea763eea92c1553818e2130d4021329a787242d3a2c45e06b4766cbb47` | `edc818ea763eea92c1553818e2130d4021329a787242d3a2c45e06b4766cbb47` | PASS |

Tests: `ctest -R cartesian_cerveny_influence` PASS (includes new
counter-identity and default-off assertions);
`python -m unittest codes.tests.test_benchmark_rayreuse` PASS (20 tests).

## 4. Reuse-mode baseline (`munk_cerveny_cc`, same post-R01 binary)

All runs: `--execution-mode reuse --verify-cache --profile-influence`,
Trace passes = 1, cache fingerprint before == after.
2F uses the `broadband_smoke` env root (5000 launch angles);
8F/16F use the `broadband_regression` env root (10000 launch angles) —
absolute counter magnitudes are not directly comparable across the two roots.

### Timings (seconds)

| Run | Nf | Trace | Project | Influence | Scale | reuse wall | SHD |
|---|---|---|---|---|---|---|---|
| 2F (`50,250`) | 2 | 0.2806 | 0.0318 | 9.7537 | 0.0083 | 10.4445 | 0.0188 |
| 8F (linspace(50,500,8)) | 8 | 0.5774 | 0.2680 | 67.1433 | 0.0324 | 68.9146 | 0.1535 |
| 16F (`50..500` step 30) | 16 | 0.5663 | 0.5064 | 131.8655 | 0.0663 | 134.1061 | 0.3587 |

### Influence counters (legacy + new)

| Counter | 2F | 8F | 16F |
|---|---|---|---|
| rayAccumulations | 10000 | 80000 | 160000 |
| segmentCandidates | 3304654 | 26438072 | 52876144 |
| eligibleSegments | 2247392 | 17979520 | 35959040 |
| receiverRangeEvaluations | 4972960 | 39784128 | 79568256 |
| receiverDepthEvaluations | 999564960 | 7996609728 | 15993219456 |
| imageEvaluations | 2998694880 | 23989829184 | 47979658368 |
| windowRejections | 1697322678 | 15859605621 | 31986302529 |
| taperRejections | 894589970 | 6443391280 | 12901343019 |
| nonzeroImageContributions | 406782232 | 1686832283 | 3092012820 |
| geometrySegmentEvaluations | 3304654 | 26438072 | 52876144 |
| geometryRangeEvaluations | 4972960 | 39784128 | 79568256 |
| geometryDepthEvaluations | 999564960 | 7996609728 | 15993219456 |
| geometryImageGeometryEvaluations | 2998694880 | 23989829184 | 47979658368 |
| frequencyRangeKernelEvaluations | 4972960 | 39784128 | 79568256 |
| frequencyImageKernelEvaluations | 2998694880 | 23989829184 | 47979658368 |

Counter identity self-check (new == legacy counterpart) holds exactly in all
three runs, matching the component-test assertion for the frequency-major
kernel.

### SHD SHA256 (post-R01 binary)

| Run | SHD SHA256 |
|---|---|
| 2F | `cf1f9711aefcab087bd766c395a03b935c1c9cf13980335a368035515fd126bc` |
| 8F | `246aeb65274eb08b73c1ad9d4be2b2aa849f47e28f7eba3891a75fdfadaa753c` |
| 16F | `f01ee48119549a82e79798322bf5227d8fc95054be82de955de5ccadef057c2c` |

### Frozen geometry / cache facts

- 8F and 16F share the identical frozen geometry: cache fingerprint
  `16716541753253518712` (before == after in every run), ray count 10000,
  ray point count 3367964, ray cache bytes 635194768.
- 2F smoke root: fingerprint `2271226459307825052`, ray count 5000,
  ray point count 1683973, ray cache bytes 317572080.

## 5. Reproduction

```bash
# build
cd Bellhop_RayReuse && uv run cmake --build build/igr1-clean -j 8

# 16F gate / baseline (regression root)
cd "../test/standard_cases/results/rayreuse/munk_cerveny_cc/broadband_regression/broadband"
"../../../../../../Bellhop_RayReuse/build/igr1-clean/bellhop_rayreuse" \
  munk_cerveny_cc_broadband_regression_broadband \
  --frequencies-hz "50,80,110,140,170,200,230,260,290,320,350,380,410,440,470,500" \
  --execution-mode reuse --verify-cache --profile-influence
shasum -a 256 munk_cerveny_cc_broadband_regression_broadband.shd

# 8F (same root, numpy.linspace(50,500,8) exact doubles)
"../../../../../../Bellhop_RayReuse/build/igr1-clean/bellhop_rayreuse" \
  munk_cerveny_cc_broadband_regression_broadband \
  --frequencies-hz "50.0,114.28571428571429,178.57142857142858,242.8571428571429,307.14285714285717,371.42857142857144,435.7142857142858,500.0" \
  --execution-mode reuse --verify-cache --profile-influence

# 2F (smoke root; generate env first)
cd ../../../../../..
uv run python codes/standard_cases.py generate --version rayreuse \
  --case munk_cerveny_cc --profile broadband_smoke \
  --executable "../../Bellhop_RayReuse/build/igr1-clean/bellhop_rayreuse"
cd results/rayreuse/munk_cerveny_cc/broadband_smoke/broadband
"../../../../../../Bellhop_RayReuse/build/igr1-clean/bellhop_rayreuse" \
  munk_cerveny_cc_broadband_smoke_broadband \
  --frequencies-hz "50,250" --execution-mode reuse --verify-cache --profile-influence
```

The 8F CSV is the plain-double repr of `numpy.linspace(50, 500, 8)`; the PRT
echoes the same doubles with 17-significant-digit formatting (verified equal as
doubles).

## 6. Scope statement

R01 changed only: statistics struct fields, counter increments inside
`CollectStatistics` guards, PRT printing, optional PRT parsing in
`benchmark_rayreuse.py`, and one component test. No numerical path, cache, or
writer change. Baseline interpretation (geometry dedup, wall effects) is
explicitly deferred to R06.
