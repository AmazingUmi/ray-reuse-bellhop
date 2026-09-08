# IGR-1 R06 — Performance and Memory Acceptance Report

> **Batch:** IGR-1 — Cross-Frequency Cartesian Cerveny Influence Fusion
> **Task:** R06 (performance + memory acceptance; executed after R05 all-PASS)
> **Date:** 2026-09-02
> **Machine:** Apple M4, 10 cores, 24 GiB (macOS, arm64)
> **Binary:** `Bellhop_RayReuse/build/igr1-clean/bellhop_rayreuse` (release), SHA-256 `95b820f2…`
> **Git:** branch `feat/igr-influence-geometry-reuse`, base HEAD `3ed475e` + uncommitted IGR-1 working tree (harness records `dirty: true`)
> **Case:** `munk_cerveny_cc` (201 depths × 501 ranges; shared_fmax fan)
> **Archive:** `Bellhop_RayReuse/build/igr1-clean/benchmarks/igr1_r06_munk_{2f,8f,16f,32f,64f}.json`, counter PRTs under `benchmarks/counters/`
> **Baseline:** R01 (`REPORT_IGR1_R01_BASELINE.md`, `build/igr1-clean/baseline/igr1_r01_baseline.json`)

## 1. Protocol

- Modes: `nonreuse`, `reuse`, `fused`, `parallel` (parallel = 8 workers, queue 2, no memory budget; reference only).
- Mandatory rows 2F / 8F / 16F: 5 repeats + 1 warmup each, alternating (rotated) sample order, same build/threads/inputs; median + min/max + MAD reported. Memory rows 32F / 64F: **deferred by user decision (2026-09-02, runtime budget)**; the memory model is validated on 2F/8F/16F instead.
- Frequencies: 2F = profile `broadband_smoke` [50, 250]; 8F/32F = linspace(50, 500, N) via `--frequencies-csv`; 16F = profile `broadband_regression`; 64F = profile `broadband_stress` (50–1000 Hz).
- External wall (`real_seconds`) measured by the benchmark harness; phase timings (Trace/Project/Influence/Scale/SHD seconds) parsed from PRT; peak RSS via isolated helper `RUSAGE_CHILDREN.ru_maxrss`.
- Counters measured in separate `--profile-influence` runs (not in timed runs).
- Cross-mode SHD byte-identity enforced by the harness on every row (all PASS: single `common_shd_sha256` per row).
- Harness fixes delivered with R06: `parse_prt_metrics` required field corrected to `Total solver and product seconds` (pre-existing staleness); `fused` mode added to `benchmark_rayreuse.py` (mode markers, wall field `fused reuse wall seconds`, command construction) and registered in `standard_cases.py` mode validation (`RAYREUSE_EXECUTION_MODES`); `model_matrix.py` default modes pinned to `nonreuse,reuse,parallel` with fused accepted only when explicitly requested (fused is CC-coherent-only scope).

## 2. Wall-time results (median [min..max], MAD; seconds)

| Nf | nonreuse | reuse | fused | parallel (w8) |
|---|---|---|---|---|
| 2F | 8.19 [8.18..8.23] 0.002 | 7.89 [7.87..7.91] 0.014 | **11.24** [11.21..11.36] 0.033 | 5.13 [5.11..5.15] 0.013 |
| 8F | 53.19 [53.11..55.48] 0.036 | 49.16 [49.10..49.21] 0.040 | **51.04** [50.90..51.07] 0.002 | 13.42 [13.37..13.49] 0.023 |
| 16F | 104.22 [103.76..105.11] 0.209 | 95.66 [95.51..95.71] 0.041 | **103.04** [102.72..103.96] 0.319 | 22.14 [22.03..22.16] 0.016 |

32F/64F: deferred by user decision (see §5).

Dispersion is tight on every row (spread ≤ 1.4 % of median), so the mode gaps below are far outside run noise.

### Speedup ratios (median wall)

| Nf | fused/reuse | reuse/nonreuse | parallel/reuse |
|---|---|---|---|
| 2F | 0.70 (fused 42 % slower) | 1.04 | 1.54 |
| 8F | 0.96 (fused 3.8 % slower) | 1.08 | 3.66 |
| 16F | 0.93 (fused 7.7 % slower) | 1.09 | 4.32 |

## 3. Phase medians (seconds)

| Nf | mode | Trace | Project | Influence | Scale | SHD |
|---|---|---|---|---|---|---|
| 2F | reuse | 0.29 | 0.03 | 7.55 | 0.01 | — |
| 2F | fused | 0.29 | 0.03 | **10.90** | 0.01 | — |
| 8F | reuse | 0.58 | 0.26 | 48.22 | 0.03 | — |
| 8F | fused | 0.60 | 0.23 | **50.14** | 0.03 | — |
| 16F | reuse | 0.59 | 0.52 | 94.40 | 0.07 | — |
| 16F | fused | 0.59 | 0.46 | **101.88** | 0.06 | — |

Trace and Project are unchanged (shared fan; same projection count). Scale is unchanged. **The entire wall-time difference lives in Influence.**

## 4. Counters (from separate `--profile-influence` runs)

See §7 for the full table. Headline at 16F: fused computes geometry once per traversal — `geometrySegment/Range/Depth/ImageGeometry` are **exactly** reuse/16 (e.g. image geometry 2.999e9 vs 4.798e10) — while `frequencyRangeKernel`/`frequencyImageKernel` and window/taper/nonzero counters equal reuse exactly. **Geometry de-duplication is fully realized in the counters; it simply does not convert into wall time** (§8).

## 5. Memory (peak RSS, median; MiB)

| Nf | reuse | fused | ΔRSS | expected Δ (Nf × 1.61 MB field) |
|---|---|---|---|---|
| 2F | 305.7 | 307.6 | +1.9 | +3.2 |
| 8F | 607.3 | 620.0 | +12.7 | +12.9 |
| 16F | 607.3 | 634.3 | +27.0 | +25.8 |
| 32F | deferred | deferred | — | (+51.5) |
| 64F | deferred | deferred | — | (+103.0) |

32F/64F rows are **DEFERRED by user decision (2026-09-02)**: the 64F stress row was judged too slow to measure in this session and the batch proceeds with Nf ≤ 16. The memory-model expectation ΔRSS ≈ Nf × 1.61 MB is already validated by the three measured rows (linear within measurement resolution); no OOM was encountered anywhere.

Frozen-cache bytes are constant across modes and Nf rows (`ray cache bytes` identical; the frozen cache does not grow with Nf). Long-lived fused field memory scales as expected: ΔRSS ≈ Nf × 1.61 MB.

## 6. Verdict

**NOT_VIABLE (for wall-time generalization of the v1 fused layout as implemented).**

Per the frozen Go/No-Go gates (worklist R06; no preset speedup threshold):

- Correctness fully preserved: PASS (R05 Levels A–D; cross-mode SHD identity in every benchmark row).
- Geometry de-duplication realized in counters: PASS (≈ 1/Nf geometry evaluations).
- Statistically meaningful end-to-end wall improvement: **FAIL** — fused is 42 % / 3.8 % / 7.7 % *slower* than reuse at 2F / 8F / 16F (all gaps ≫ noise).
- RSS reasonable: PASS (≈ Nf × field bytes, no cache growth).

Consequence per the frozen escape gate: the fused path stays an opt-in experimental execution mode (`--execution-mode fused`); it does not become a default or required production path, and no further batch is authorized to build on it without a new decision.

## 7. Counter table (reuse vs fused; from `--profile-influence` runs, archived under `benchmarks/counters/`)

| Counter | 2F reuse | 2F fused | 8F reuse | 8F fused | 16F reuse | 16F fused |
|---|---|---|---|---|---|---|
| geometrySegment | 3,304,654 | 1,652,327 (**0.500**) | 26,438,072 | 3,304,759 (**0.125**) | 52,876,144 | 3,304,759 (**0.0625**) |
| geometryRange | 4,972,960 | 2,486,480 (**0.500**) | 39,784,128 | 4,973,016 (**0.125**) | 79,568,256 | 4,973,016 (**0.0625**) |
| geometryDepth | 9.996e8 | 4.998e8 (**0.500**) | 7.997e9 | 9.996e8 (**0.125**) | 1.599e10 | 9.996e8 (**0.0625**) |
| geometryImage | 2.999e9 | 1.499e9 (**0.500**) | 2.399e10 | 2.999e9 (**0.125**) | 4.798e10 | 2.999e9 (**0.0625**) |
| frequencyRangeKernel | 4,972,960 | 4,972,960 (1.000) | 39,784,128 | 39,784,128 (1.000) | 79,568,256 | 79,568,256 (1.000) |
| frequencyImageKernel | 2.999e9 | 2.999e9 (1.000) | 2.399e10 | 2.399e10 (1.000) | 4.798e10 | 4.798e10 (1.000) |
| windowRejections | 1.697e9 | 1.697e9 (1.000) | 1.586e10 | 1.586e10 (1.000) | 3.199e10 | 3.199e10 (1.000) |
| taperRejections | 8.946e8 | 8.946e8 (1.000) | 6.443e9 | 6.443e9 (1.000) | 1.290e10 | 1.290e10 (1.000) |
| nonzero | 4.068e8 | 4.068e8 (1.000) | 1.687e9 | 1.687e9 (1.000) | 3.092e9 | 3.092e9 (1.000) |

**Geometry counters are exactly 1/Nf** (0.500 / 0.125 / 0.0625 — the Munk case has identical active prefixes across frequencies here, so the union prefix equals each frequency's prefix and the ratio is exact). **Frequency-kernel counters are exactly 1.0** — frequency physics is not eliminated, as designed (D14). This is the counter-side confirmation of V2-GATE-12's geometry-dedup requirement.

Footnote: Influence seconds *within these profiled runs* (instrumentation overhead present): 2F 9.69 vs 10.77 (+11 %), 8F 66.96 vs 61.76 (−7.8 %), 16F 130.5 vs 131.3 (+0.6 %). The verdict in §6 uses the clean un-profiled benchmark medians (§2–§3), where profiling overhead is absent.

## 8. Attribution (why geometry de-dup does not pay here)

1. **Geometry was never the bottleneck.** In the frequency-major kernel the shared geometry (W, interpolation, Δz/Δz², polarity) is a handful of pipelined FLOPs per (range, depth, image) executed in tight loops with everything frequency-fixed in registers. The expensive parts — `std::exp`/`cos`/`sin` in the image kernel, window/taper branches — are frequency-local physics that IGR-1 deliberately keeps per frequency (D14). Eliminating 15/16 of the *cheap* work cannot buy much.
2. **The fused layout pays locality for it.** Per (depth, image) the fused kernel walks Nf scattered per-frequency state arrays (q/τ/γ/corrected/radiusMax/omega), branches on eligibility masks, and writes Nf distinct workspace rows — versus one register-resident frequency and one contiguous write stream in the frequency-major loop. At 2F the loop-overhead/mask cost dominates ( +42 %); at 8–16F the arithmetic weight recovers some of it but never crosses over (−4 %…−8 %).
3. **Parallel remains the effective wall-time lever** (4.3× vs reuse at 16F with 8 workers) — orthogonal to IGR-1 and unchanged by this batch.

## 9. Limitations

- Single machine (Apple M4, 10 cores), single case family (Munk CC coherent), single grid size; no claim about other CPUs/caches.
- v1 fused kernel deliberately forbids performance tricks that would alter the addition stream (HARD GATE) — no SIMD, no depth/image reorder, no reassociation; a layout-tuned variant would exit the bitwise-parity contract and is therefore out of scope for IGR-1.
- 32F/64F memory rows not measured (user decision, runtime budget); ΔRSS ≈ Nf × 1.61 MB is validated on 2F/8F/16F only.

## 10. Reproduction

```bash
cd test/standard_cases
BIN=../../Bellhop_RayReuse/build/igr1-clean/bellhop_rayreuse
OUT=../../Bellhop_RayReuse/build/igr1-clean/benchmarks
# 16F row
uv run python codes/benchmark_rayreuse.py --case munk_cerveny_cc --profile broadband_regression \
  --modes nonreuse,reuse,fused,parallel --repeats 5 --warmups 1 --parallel-workers 8 \
  --executable "$BIN" --output "$OUT/igr1_r06_munk_16f.json" --machine-label "Apple M4, 10 cores, 24 GiB" --allow-dirty
# memory rows (ILLUSTRATIVE — 32F/64F were deferred this batch per user decision; §1/§5/§9)
uv run python codes/benchmark_rayreuse.py --case munk_cerveny_cc --profile broadband_stress \
  --modes reuse,fused --repeats 3 --warmups 1 --executable "$BIN" \
  --output "$OUT/igr1_r06_munk_64f.json" --machine-label "Apple M4, 10 cores, 24 GiB" --allow-dirty
```

Counter runs: single `--execution-mode {reuse,fused} --profile-influence --verify-cache` invocations per Nf; PRTs archived under `benchmarks/counters/`.
