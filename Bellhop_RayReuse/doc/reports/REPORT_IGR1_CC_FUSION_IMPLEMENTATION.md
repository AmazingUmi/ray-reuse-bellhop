# IGR-1 Implementation Report — Cross-Frequency Cartesian Cerveny Influence Fusion

> **Batch:** IGR-1 · **Status:** `ACCEPTED / CLOSED` (final review [`../reviews/IGR1_CC_FUSION_FINAL_REVIEW_2026-09-02.md`](../reviews/IGR1_CC_FUSION_FINAL_REVIEW_2026-09-02.md))
> **Date:** 2026-09-02 · **Branch:** `feat/igr-influence-geometry-reuse` (base `3ed475e`) · **Build:** `build/igr1-clean` (release)
> **Worklist:** [`../worklists/IGR-1_CC_FUSION_WORKLIST.md`](../worklists/IGR-1_CC_FUSION_WORKLIST.md) · **Design:** [`../worklists/DESIGN_IGR1_CC_FUSION.md`](../worklists/DESIGN_IGR1_CC_FUSION.md)
> **Companion evidence:** [`REPORT_IGR1_R01_BASELINE.md`](REPORT_IGR1_R01_BASELINE.md) · [`REPORT_IGR1_R05_PARITY.md`](REPORT_IGR1_R05_PARITY.md) · [`REPORT_IGR1_R06_PERFORMANCE.md`](REPORT_IGR1_R06_PERFORMANCE.md) · [`REPORT_IGR1_CC_FUSION_BATCH_ACCEPTANCE.md`](REPORT_IGR1_CC_FUSION_BATCH_ACCEPTANCE.md)

## 1. Scope delivered

TL · Cartesian Cerveny · coherent pressure · rectilinear uniform-range receivers · single source · shared frozen launch fan · serial fused reference path · `Bf = Nf` — exactly the frozen IGR-1 scope. Existing `nonreuse` / `reuse` / `parallel` paths unchanged (proven by byte-anchors). New opt-in execution mode `--execution-mode fused`.

## 2. Implemented architecture (as-built loop hierarchy)

```text
Trace source fan once → frozen RayPathCache (immutable, const&)
for source (single)
    allocate raw FrequencyWorkspace[f = 0..Nf-1]        // long-lived, Bf=Nf
    for ray
        for f: pattern amplitude → Lloyd branch → FrequencyProjector::project
        for f: pickBeamEpsilon (after projection, production order)
        ONE fused kernel call: CartesianCervenyInfluence::accumulateFusedPrevalidated
            for f: own-prefix precomputeRayValues (p/q/gamma/kmah)
            unionPrefix = max_f prefixLen[f]
            for segment (union bound)
                shared: early range exit, degenerate skip, crossing topology
                activeMask[f] = left-endpoint active ∧ rightIndex < prefix[f]
                for crossed receiver range
                    shared: W, interpolated position/slowness/real c
                    rangeEligible[f] = activeMask[f]
                    for f: q/tau/gamma interpolation → gamma.imag()>0 clears
                            eligibility → principal/corrected (KMAH on interp q)
                    for receiver depth
                        imageSum[f] = 0
                        for image True → Surface → Bottom
                            shared: Δz, Δz², polarity
                            for f: window → taper → phase → exp → contribution
                                    imageSum[f] += contribution (zeros included)
                        for f: workspace[f] += corrected[f] * imageSum[f]   // ONE add
    for f: scaleCoherentCartesianPressure(workspace[f]) → consumer → writeFrequency(f)
```

Key components: `FusedRayReuseSolver` (`src/solver/fused_ray_reuse_solver.cpp`; `accumulateFrequencies` raw seam + `solveStreaming`), fused kernel in `src/field/cartesian_cerveny_influence.cpp`, `WorkspaceDelivery::{Scaled,Raw}` seam on `solveFrequencyFromSourceCache`, `BroadbandExecutionMode::Fused` CLI, deterministic rejection matrix at CLI + solver layers.

## 3. Correctness results

- **Level A** — cache fingerprint before==after==reuse on every fused run (`--verify-cache`).
- **Level B** — raw workspace memcmp-bitwise per frequency (4 fixtures: Munk CC 2F; imageCount=2; WKB; divergent-prefix lossy fixture 96/300 rays, prefix 263 vs 313).
- **Level C** — scaled workspace memcmp-bitwise per frequency.
- **Level D** — SHD SHA256 reuse==fused on all 13 CLI rows (munk 2F/16F `cf1f9711…`/`f01ee481…`, direct 2F/16F, thorp 16F, FG, vacuum-rigid, elastic, WKB, space-filling, curvature ±, munk_spline).
- Existing-path anchors unchanged (munk 16F reuse `f01ee481…`).
- Full regression: ctest 43/43; pytest 191; standard-cases unittest 176; harness suite 24.

## 4. Counter results (munk_cerveny_cc)

| Counter | ratio fused/reuse |
|---|---|
| geometrySegment / geometryRange / geometryDepth / geometryImage | **exactly 1/Nf** (0.500 / 0.125 / 0.0625) |
| frequencyRangeKernel / frequencyImageKernel | **exactly 1.000** |
| windowRejections / taperRejections / nonzero | **exactly 1.000** |

Geometry duplication is fully eliminated; frequency physics is fully preserved — the exact designed behavior (D14).

## 5. Performance results (Apple M4, 10 cores; median of 5)

| Frequencies | reuse wall | fused wall | fused/reuse | Influence reuse→fused | RSS reuse | RSS fused |
|---|---|---|---|---|---|---|
| 2F | 7.89 s | 11.24 s | **0.70** | 7.55 → 10.90 s | 305.7 MiB | 307.6 MiB |
| 8F | 49.16 s | 51.04 s | **0.96** | 48.22 → 50.14 s | 607.3 MiB | 620.0 MiB |
| 16F | 95.66 s | 103.04 s | **0.93** | 94.40 → 101.88 s | 607.3 MiB | 634.3 MiB |

Reference: nonreuse 8.19/53.19/104.22 s; parallel (8 workers) 5.13/13.42/22.14 s (4.3× vs reuse at 16F). Dispersion ≤ 1.4 % spread on all rows. 32F/64F deferred (user decision).

**Verdict: NOT_VIABLE for wall-time generalization of the v1 fused layout** — correctness and geometry dedup fully achieved; no wall-time gain. Attribution: geometry is cheap pipelined arithmetic (not the bottleneck); the fused layout trades it for worse memory locality (Nf scattered per-frequency state arrays and Nf workspace write streams per depth/image, plus per-frequency eligibility branching). See R06 §8.

## 6. Memory model (validated)

`M_total ≈ M_frozen + Nf × M_field + per-ray temporaries`; ΔRSS ≈ Nf × 1.61 MB (measured +1.9/+12.7/+27.0 MiB at 2F/8F/16F); frozen cache bytes constant; per-ray temporaries (Nf × state/precompute/epsilon + O(Nf) masks/sums) released per ray; no persistent geometry structure anywhere.

## 7. Limitations / deferred

- Fused scope guard: CC coherent rectilinear single-source TL only; everything else deterministically rejected (CLI + solver).
- Parallel fused, multisource fused, automatic blocking (`Bf<Nf`), persistent geometry caches, frequency interpolation, SIMD/layout tuning (would break the bitwise-parity contract), Arrival/Eigenray IGR, other beam families — all out of scope, none smuggled.
- 32F/64F memory rows deferred by user decision; memory model validated at Nf ≤ 16.
- Single machine / single case family; the negative wall-time result is scoped accordingly.

## 8. Next-step recommendation (data-driven)

The R06 data says: within the bitwise-parity contract, further influence-side loop restructuring of this shape is not promising — geometry is not the bottleneck, and the frequency-local kernel dominates (Project/Influence remain O(Nf) by design). Recommended priority order for any follow-up decision:
1. **Stop further serial-fusion IGR optimization** in the current parity-preserving form (measured: no win).
2. If wall time matters, invest in the **parallel** path (already 4.3× at 16F) and its work partitioning — orthogonal to IGR.
3. A layout-tuned fused variant (frequency-blocked state arrays, SoA over frequencies) could revisit the locality hypothesis but **exits bitwise parity** — only as a separately-approved research batch with oracle-level (not bitwise) gates.
4. Frequency blocking (IGR-3 shape) is pointless for wall time on this evidence (memory is not the constraint at Nf ≤ 16); it remains a memory policy for much larger Nf only.
