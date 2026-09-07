# IGR-1 — Cross-Frequency Cartesian Cerveny Influence Fusion — EXECUTION WORKLIST

> **Status: ACCEPTED / CLOSED (2026-09-02).** Final review: [`../reviews/IGR1_CC_FUSION_FINAL_REVIEW_2026-09-02.md`](../reviews/IGR1_CC_FUSION_FINAL_REVIEW_2026-09-02.md) — verdict `ACCEPTED`. Batch outcome: correctness fully preserved (Levels A–D bitwise); geometry dedup exactly 1/Nf; wall-time generalization of the v1 fused layout judged `NOT_VIABLE` per the frozen R06 escape gate (fused stays opt-in experimental). Closure docs: [`../reports/REPORT_IGR1_CC_FUSION_IMPLEMENTATION.md`](../reports/REPORT_IGR1_CC_FUSION_IMPLEMENTATION.md), [`../reports/REPORT_IGR1_CC_FUSION_BATCH_ACCEPTANCE.md`](../reports/REPORT_IGR1_CC_FUSION_BATCH_ACCEPTANCE.md). This closes IGR-1 only; it does not declare Influence Geometry Reuse complete.
> **Branch:** `feat/igr-influence-geometry-reuse`
> **HEAD at freeze:** `3ed475e` (clean tree)
> **Date frozen:** 2026-09-02
> **Baseline re-anchor:** byte-parity baseline is re-anchored to IGR-1 start HEAD `3ed475e` (V2-GATE-07). R01 instrumentation must leave SHD byte-identical, so the anchor extends to the post-R01 accepted state; every later Level A–D comparison is intra-binary fused vs current-reuse at the same build.
> **Authority:** This worklist is the IGR-1 execution authority and **supersedes** [`IGR-1_CC_FUSION_DESIGN_DRAFT.md`](IGR-1_CC_FUSION_DESIGN_DRAFT.md) once frozen. The draft and [`../reports/REPORT_IGR0_REVISION_CROSS_FREQUENCY_FUSION_2026-09-01.md`](../reports/REPORT_IGR0_REVISION_CROSS_FREQUENCY_FUSION_2026-09-01.md) remain design rationale, not execution state.
> **Build:** cmake presets; batch-clean build dir `Bellhop_RayReuse/build/igr1-clean` (release).

---

## 1. Frozen Batch Scope

- TL only.
- Cartesian Cerveny only; coherent pressure only (CC intensity path stays on existing path, out of IGR-1).
- Rectilinear receivers with uniform receiver ranges only.
- Single source only.
- Shared frozen launch fan (trace is frequency-independent; one frozen trace pass).
- Serial fused reference path; `Bf = Nf` (all-frequency fusion).
- Existing reuse / nonreuse / parallel paths unchanged.
- `RayPathCache` schema and `FrequencyProjector` semantics unchanged.

## 2. Out of Scope (hard)

Parallel fused; multisource fused; automatic frequency blocking; persistent geometry caches (segment-range stencil, receiver-geometry, depth-interval caches); receiver-depth/image materialization; frequency interpolation/reconstruction; rolling FrequencyProjector; SIMD; GPU; nested parallelism; tree/ray-local/thread-local reduction; Arrival/Eigenray IGR; other beam families (Simple Gaussian, GeoGaussian); RayPathCache schema redesign; unrelated refactor.

## 3. Batch Hard Gates (from user-approved batch prompt, binding on all tasks)

- **HARD GATE — Accumulation order.** Per fixed frequency, the addition stream
  `ray → segment → range → depth → True/Surface/Bottom → one workspace add`
  must be preserved bitwise. No per-image workspace adds; no reductions or reassociation of any kind. (Enforces D10/D13/D14; verified by V2-GATE-07.)
- **HARD GATE — Existing paths unchanged.** `nonreuse` / `reuse` / `parallel` modes keep current semantics and outputs (V2-GATE-08).
- **CLI exposure** is designed in R02 with the recommended shape: new `BroadbandExecutionMode::Fused` enum value, CLI token `fused`, PRT marker, and explicit deterministic rejection of unsupported combinations at CLI and solver level. Exact form frozen in R02 before construction.
- **R05 seam policy:** the reuse path currently scales in-place inside `solveFrequencyFromSourceCache` before returning; a narrow opt-in raw-workspace seam is allowed only if default behavior and the numerical path of existing callers are unchanged. Exact seam frozen in R02.

## 4. Frozen Decisions D1–D14 (inherited from design draft §2)

1. **D1** Transient cross-frequency geometry reuse: geometry is computed once per traversal and consumed immediately; no long-term storage.
2. **D2** No persistent geometry cache; full pair materialization remains REJECTED.
3. **D3** Frequency-local physics boundary (complex τ, attenuation, reflection amplitude/phase, active prefix, ε(f), p/q/γ frequency-local combinations, σ1, window membership, complex phase/exponential, pressure/intensity accumulation) is never written back to the frozen cache.
4. **D4** Dynamic-ray bases (p1/p2, q1/q2, geometry, events, quadrature) keep coming from the frozen `RayPath`; `pVB(f)=p1+ε(f)p2` etc. are constructed in the frequency-local layer.
5. **D5** Per-frequency active prefix: prefix differences arise from frequency-local source/reflection amplitude evolution and the cumulative `<0.005F` cutoff, not directly from volume attenuation; traversal upper bound = union of per-frequency prefixes; each frequency independently retains its left-endpoint active check, terminal retention, and cutoff.
6. **D6** `Bf = Nf`; extra long-term memory ≈ `Nf ×` frequency field workspace.
7. **D7** Frequency blocking is a memory policy of the same algorithm; not implemented in v1.
8. **D8** Frozen `RayPathCache` contract unchanged (immutable, frequency-independent, schema-stable; zero write-back).
9. **D9** Parallel deferred; serial fused reference path first.
10. **D10** Integration point: new multi-frequency fused entry at the serial solver layer; the existing per-frequency path is retained verbatim as reference/fallback, selected by execution policy; no deletion or semantic rewrite of existing paths.
11. **D11** Timing semantics: per-frequency Influence time is not separable in fused mode; report shared traversal and frequency-kernel work at block level; Project / Influence / Scale / wall reported separately; no fabricated per-frequency timing precision.
12. **D12** Byte-parity target: "conditions designed to preserve bitwise parity" — verified, never claimed, via cache fingerprint, raw workspace, scaled workspace, and SHD SHA-256 in the same binary.
13. **D13** Image addition shape: per-frequency `imageSum[f]` accumulated strictly True → Surface → Bottom; exactly one `workspace[f] += corrected[f] * imageSum[f]` per beam/depth after the complete imageSum; per-image workspace writes forbidden.
14. **D14** No parallelism/reassociation in v1: no tree/ray-local/thread-local reduction, SIMD reassociation, or depth/image reorder.

## 5. Authoritative Fused Loop Hierarchy (verbatim from design report §B.2)

```text
Trace source fan once
        ↓
Frozen RayPathCache
        ↓
for source                                      // IGR-1 v1: single source
    allocate raw workspace[f = 0 .. Nf-1]       // long-lived; Bf = Nf

    for ray in existing order
        for frequency
            FrequencyProjector::project(ray, f) // exact current semantics
            epsilon[f]
            p/q/gamma/kmah precompute[f]

        unionPrefix = max reachable prefix over frequencies

        for segment in existing order
            activeMask[f] = frequencyState[f].points[leftIndex].active
            if no frequency is active: continue

            shared segment geometry
            shared range-crossing topology

            for crossed receiver range in existing order
                shared W
                shared interpolated position/slowness/real sound speed
                rangeEligible[f] = activeMask[f]

                for frequency where rangeEligible[f]
                    interpolate q[f] / tau[f] / gamma[f]
                    if gamma[f].imag() > 0
                        rangeEligible[f] = false
                        continue
                    principal / corrected / KMAH state

                if no frequency is rangeEligible: continue receiver range

                for receiver depth in existing order
                    imageSum[f] = 0 for rangeEligible frequencies

                    for image in True → Surface → Bottom order
                        shared Delta-z / Delta-z-squared / polarity

                        for frequency where rangeEligible[f]
                            window
                            taper
                            phase
                            exponential
                            image contribution
                            imageSum[f] += image contribution

                    for frequency where rangeEligible[f]
                        contribution = corrected[f] * imageSum[f]
                        workspace[f] += contribution

    for frequency
        scaleCoherentCartesianPressure(workspace[f])
        consumer / writeFrequency(f)
```

## 6. Memory Model (from design report §F)

`M_total ≈ M_frozen cache + Bf·M_field + M_per-ray frequency temporaries + M_small orchestration`

| Lifetime | State |
|---|---|
| Long-lived across all rays | `Bf × FrequencyWorkspace` (v1: `Bf=Nf`); Munk 201×501 complex<double> ≈ 1.61 MB/frequency; 16F ≈ 26 MB; 64F ≈ 103 MB |
| Per-ray temporary (release or reuse after one ray) | `Nf × RayFrequencyState`, `Nf × Cerveny precompute`, `Nf × epsilon`, active mask, `rangeEligible[f]`, `imageSum[f]` |
| Unchanged | immutable frozen `RayPathCache` (bytes must not grow with Nf) |
| Small orchestration | frequency metadata, counters/timings, workspace handles |

No `Nray × Nsegment × Nreceiver` persistent geometry storage may be (re)introduced.

## 7. Correctness Gates (gate set v2; mapping in design draft §4)

| Gate | Content | Inherited from |
|---|---|---|
| V2-GATE-01 | Lossy re-travel time integrated exactly per frequency, real and imaginary parts free of frozen-geometry contamination; lossless uses real travel time directly | IGR1-GATE-01 |
| V2-GATE-02 | Threshold semantics: source initially active; `<0.005F` cumulative cutoff; `<1e-5F` only for acoustic-half-space single suppression | IGR1-GATE-02 |
| V2-GATE-03 | Per-frequency terminal retention + union traversal upper bound (D5) | IGR1-GATE-03 (revised) |
| V2-GATE-04 | Regular-grid crossing-set consistency (computed once, naturally consistent, still verified) | IGR1-GATE-04 |
| V2-GATE-05 | Domain routing: fused only for rectilinear/uniform-range CC coherent; everything else explicitly takes the existing path; no silent entry | IGR1-GATE-05 (revised) |
| V2-GATE-06 | Mode coverage: coherent runs fused; I/S run existing path with unchanged output (spot-checked) | IGR1-GATE-06 (narrowed) |
| V2-GATE-07 | Level A–D parity: cache fingerprint; raw workspace bitwise; scaled workspace bitwise; SHD SHA-256; baseline re-anchored to IGR-1 start accepted HEAD | IGR1-GATE-10 (extended) |
| V2-GATE-08 | Fallback / execution-policy parity: both switch sides produce identical output; fused-off is exactly the existing path; nonreuse (cache-off) unchanged | IGR1-GATE-09 |
| V2-GATE-09 | Frozen-cache integrity: fingerprint before/after; `RayPathCache` zero write-back (D8) | IGR1-GATE-08/09 cache part |
| V2-GATE-10 | Memory model measured: `Nf ×` field bytes + per-ray temporaries + peak RSS; no geometry cache exists | IGR1-GATE-11 (rewritten) |
| V2-GATE-11 | Origin / F2CPP oracle suites all continue to pass | IGR1-GATE-12 |
| V2-GATE-12 | Net performance gain + geometry/frequency-kernel grouped counters + Scale seconds; protocol and `NOT_VIABLE` escape inherited | IGR1-GATE-13 (extended) |
| V2-GATE-D1 (DEFERRED) | Multi-source isolation (`Nsource × Bf` + writer all-source constraint); deferred to IGR-2/later batch | IGR1-GATE-07 |

## 8. Source Anchors (established facts for executors)

- `src/solver/serial_ray_reuse_solver.cpp` — `solveStreaming`: trace once, then `for frequency → for source → solveFrequencyFromSourceCache` (frequency-major reference).
- `src/solver/single_frequency_solver.cpp` — one `FrequencyWorkspace` per frequency; projects `RayFrequencyState`, accumulates CC influence, scales in-place, returns scaled workspace.
- `src/field/cartesian_cerveny_influence.cpp:610-878` — `CartesianCervenyInfluence::accumulateImpl`: `precomputeRayValues` (p/q/gamma/kmah over per-frequency active prefix, epsilon-dependent); `rightIndex 2..activePrefixPointCount`; early return `rightRange > receiverRanges.back()`; degenerate-segment skip; left-endpoint-inactive skip; `fortranUpperRangeIndex` crossing (`firstUpper/secondUpper`); shared W + interpolated position/slowness/soundSpeed; frequency-local q/tau/gamma interpolation; `gamma.imag()>0` guard (`continue`); principal/corrected via `updateCervenyKmah` on interpolated q; per depth: True→Surface→Bottom imageSum (shared deltaDepth/polarity; freq-local windowMetric/taper/phase/exp); ONE `pressure[d*ranges+r] += corrected*imageSum`.
- `src/field/frequency_projector.cpp` — `project(path, frequency, projectedSourceAmplitude)`: exact per ray per frequency; active prefix cutoff `<0.005F` (`kLegacyActiveAmplitudeThreshold`), monotone, first inactive point retained.
- `pickBeamEpsilon(widthMode, frequency, sourceSoundSpeed, sourceDepthGradient, launchAngle, launchAngleStep, loopRange, epsilonMultiplier)` — per ray per frequency.
- `include/rayreuse/io/command_line.hpp:12-16` — `BroadbandExecutionMode { NonReuse, Reuse, Parallel }`; app dispatch `app/main.cpp` (reuse branch ≈:993; `validateProductOptions` ≈:249 rejects reuse/parallel for single-frequency TL and `--profile-influence` for non-CC; `writeInfluenceStatistics` :529, prints "Influence image evaluations" :546; "Total solver and product seconds" :1142).
- `CartesianCervenyStatistics` (`include/rayreuse/field/cartesian_cerveny_influence.hpp`): rayAccumulations, validatedRayPoints, validatedWorkspaceValues, activeRayPoints, segmentCandidates, eligibleSegments, receiverRangeEvaluations, receiverDepthEvaluations, imageEvaluations, windowRejections, taperRejections, nonzeroImageContributions, validationSeconds, precomputeSeconds, hotLoopSeconds. `imageEvaluations` increments at entry of `evaluateImageContribution` (per-frequency image kernel).
- Case: `test/standard_cases/cases/munk_cerveny_cc` — profiles single `[50]`, broadband_smoke `[50,250]` (2F), broadband_regression 16F 50–500, broadband_stress 64F 50–1000; 201 depths × 501 ranges.
- Benchmark: `test/standard_cases/codes/benchmark_rayreuse.py` — modes (nonreuse, reuse, parallel); wall + peak RSS (`RUSAGE_CHILDREN`); requires byte-identical SHD; `parse_prt_metrics` (:225) expects "Total solver and SHD seconds" (:246) while the binary prints "Total solver and product seconds" — pre-existing staleness, fixed in R06.
- `FrequencyWorkspace`: depthCount × rangeCount `complex<double>` pressure + `frequency()`; `ShdFrequencyWriter::writeFrequency(index, sourceWorkspaces)` writes per-frequency blocks.

## 9. Tasks

### R01 [STANDARD, instrumentation only] Baseline Instrumentation
Status: DONE
Reviewer: N/A

Acceptance:
- Extend Influence profiling with geometry-side counters `geometrySegmentEvaluations`, `geometryRangeEvaluations`, `geometryDepthEvaluations`, `geometryImageGeometryEvaluations` and frequency-kernel counters `frequencyRangeKernelEvaluations`, `frequencyImageKernelEvaluations`; `windowRejections` / `taperRejections` / `nonzeroImageContributions` semantics unchanged.
- Legacy counters (`segmentCandidates`, `receiverRangeEvaluations`, `receiverDepthEvaluations`, `imageEvaluations`) keep their current increment semantics (backward compat with PRT output and existing tests); new counters are added alongside and, in the current frequency-major kernel, coincide in count with their legacy counterparts (recorded as a self-check; `frequencyImageKernelEvaluations` takes over the frequency-image-kernel reading role of `imageEvaluations`, which is not a pure geometry counter).
- Numerical path MUST NOT change: SHD byte-identical before/after instrumentation on `munk_cerveny_cc` broadband_regression and at least one more CC case.
- New counters printed under `--profile-influence`; extend `benchmark_rayreuse.py` `parse_prt_metrics` to record them as optional fields (must not break on PRT files lacking them).
- Baseline (current reuse mode) collected for 2F / 8F / 16F on `munk_cerveny_cc` (same build, same inputs). 8F frozen choice: `--frequencies-hz` CLI override with 8 evenly spaced frequencies over the same 50–500 Hz span as broadband_regression (exact values recorded in the baseline JSON; no new permanent standard-case profile in R01).
- Baseline extraction route (frozen): because `parse_prt_metrics` retains its pre-existing stale required field until R06, R01 baseline numbers are extracted directly from PRT text (thin standalone extractor or scripted runs); the optional-counter extension is delivered here but its end-to-end harness exercise happens in R06.
- Baseline archived as JSON + markdown under `Bellhop_RayReuse/doc/reports/` or the build artifacts dir: all counters + wall/phase times (Trace/Project/Influence/Scale/SHD) + Nf.

Gate: byte-identical SHD on both cases; baseline archived and reproducible.

Evidence:
- Instrumentation diff (counters only; no numerical-path change): `include/rayreuse/field/cartesian_cerveny_influence.hpp`, `src/field/cartesian_cerveny_influence.cpp` (increments inside `if constexpr (CollectStatistics)` only), `app/main.cpp` `writeInfluenceStatistics`, `tests/component/cartesian_cerveny_influence_test.cpp` (counter-identity + default-off assertions), `test/standard_cases/codes/benchmark_rayreuse.py` (6 optional integer fields).
- Byte-identity proof (build `igr1-clean`, binary SHA256 `a9717477d1df64f8326fef41d75fd89da4fb0f06067e4f4743315df8c3461e5a`, git HEAD `3ed475e`): `munk_cerveny_cc` 16F SHD `f01ee48119549a82e79798322bf5227d8fc95054be82de955de5ccadef057c2c` (== pre-R01), `constant_speed_direct` 16F SHD `edc818ea763eea92c1553818e2130d4021329a787242d3a2c45e06b4766cbb47` (== pre-R01). Post-R01 PRTs archived at `Bellhop_RayReuse/build/igr1-clean/baseline/{munk_cerveny_cc,constant_speed_direct}_16f_post_r01.prt`.
- Tests: `ctest -R cartesian_cerveny_influence` PASS; `python -m unittest codes.tests.test_benchmark_rayreuse` PASS (20 tests).
- Baseline archived: `Bellhop_RayReuse/build/igr1-clean/baseline/igr1_r01_baseline.json` (machine-readable: Nf, exact frequency CSVs, timings, all counters, SHD SHA256, binary id, cache fingerprints) and `Bellhop_RayReuse/doc/reports/REPORT_IGR1_R01_BASELINE.md` (factual report with reproduction commands; no performance conclusions). 2F/8F PRTs archived as `munk_cerveny_cc_{2f,8f}_post_r01.prt`.
- Counter identity new==legacy holds exactly in all three baseline runs (2F/8F/16F), matching the component-test self-check for the frequency-major kernel.

### R02 [ADVANCED] DESIGN Freeze — Fused Path Design Document
Status: DONE
Reviewer: PASS (2026-09-02; first round CHANGES_REQUIRED with 2 doc-only findings — dead rejection-branch placement in §2 R1/R4 and unstale-safe parse error messages in §1 — remediated by architect and re-verified PASS)

Acceptance:
- After R01 DONE, architect freezes `Bellhop_RayReuse/doc/worklists/DESIGN_IGR1_CC_FUSION.md` covering: fused solver API; workspace ownership; per-ray frequency temporary ownership; fused CC kernel API; counter definitions for fused mode (which traversal events increment which counters, including whether `geometryRangeEvaluations` counts ranges where all frequencies were post-guard rejected — geometry was computed, so expected yes; freeze exactly); raw/scaled parity hooks (test seams for Level B/C); fallback semantics; CLI/run-mode exposure.
- CLI/run-mode exposure frozen with the recommended shape: new `BroadbandExecutionMode::Fused` value, CLI token `fused`, PRT marker, explicit deterministic rejection of unsupported combinations at CLI (`validateProductOptions` analog: fused requires broadband CC coherent rectilinear/uniform ranges, single source) and at solver level; no silent fallback anywhere.
- R05 seam choice frozen exactly (e.g. opt-in raw-workspace return from `solveFrequencyFromSourceCache`) with proof obligation: default behavior and numerical path of all existing callers unchanged.
- Timing reporting for fused mode per D11.
- Reviewer checkpoint PASS on the design document is required BEFORE any production construction of R03/R04.

Evidence:
- Frozen design doc `DESIGN_IGR1_CC_FUSION.md` (kernel hierarchy, parity obligations P1–P8, counter semantics table, rejection matrix, seam `WorkspaceDelivery::Raw`, R05 case list of 13 CC coherent rows, memory/timing model, risks).
- Reviewer PASS record (independent source-verified review; remediation loop closed).

### R03 [ADVANCED] Fused Serial Orchestration
Status: DONE
Reviewer: PASS (2026-09-02; ownership/lifecycle/rejection-matrix/existing-path-unchanged verified; 4 advisory findings routed: uniform-range placement + interface deviations architect-ratified, byte-level comparison deferred to R05 by design, one minor test-coverage gap noted)

Acceptance:
- New experimental serial execution path, structurally separate from existing paths (reuse/nonreuse/parallel unchanged): trace frozen fan once; allocate Nf `FrequencyWorkspace` (zero-initialized exactly as the existing path); iterate source/ray in existing order; prepare per-ray frequency states (project + epsilon + precompute per frequency); invoke fused CC influence once per ray over all frequencies; after all rays, scale all frequencies (`scaleCoherentCartesianPressure`, contains `sqrt(frequency())`); hand to consumer / `writeFrequency(f)` in frequency-index order; writer API and SHD format zero change.
- Explicit deterministic rejection of unsupported combinations at CLI and solver level (no silent fallback to reuse).
- Per-ray temporaries (Nf × RayFrequencyState, Nf × precompute, Nf × epsilon, O(Nf) mask/eligibility/imageSum scratch) released or reused after each ray; Nf workspaces long-lived across rays (memory model §6).
- No `RayPathCache` mutation; `--verify-cache` fingerprint before/after PASS under fused mode.
- Reviewer checkpoint: ownership / lifecycle / no cache mutation / no unrelated path change (existing three modes' diffs must be zero or provably inert).

Evidence:
- New `include/rayreuse/solver/fused_ray_reuse_solver.hpp` + `src/solver/fused_ray_reuse_solver.cpp` + `tests/component/fused_ray_reuse_solver_test.cpp` (rayreuse.component.fused_solver); Fused CLI enum/token/messages; app dispatch + rejection matrix; WorkspaceDelivery seam (defaulted, inert for existing callers).
- Full ctest 42/42 PASS in build/igr1-clean; munk 16F reuse regression SHA256 unchanged (`f01ee481…`); fused-vs-reuse SHD SHA256 identical on munk_cerveny_cc 2F (`cf1f9711…`) and constant_speed_direct 2F (`acd9a2a6…`) and preliminarily munk 16F (`f01ee481…`); fused fingerprints before==after==reuse.
- Reviewer PASS record (item-by-item source review).

### R04 [ADVANCED] Cartesian Cerveny Fused Kernel
Status: DONE
Reviewer: PASS (2026-09-02; item-by-item diff-check vs accumulateImpl: prefix scan/precompute, union traversal + activeMask equivalence incl. terminal retention, range geometry/interpolation/gamma-guard/principal/KMAH, image deltas/polarity/window/taper/phase/exp, unconditional zero imageSum adds, one workspace add, counters, dispatch; advisory: divergent-prefix fixture deferred to R05 Fixture B)

Acceptance:
- Split CC influence into: shared segment/range geometry + frequency-local range state (q/tau/gamma interpolation, `gamma.imag()>0` guard, principal/corrected via `updateCervenyKmah` on interpolated q) + shared image geometry (Δz, Δz², polarity with production signs) + frequency-local image kernel (window/taper/phase/exp), per the §5 hierarchy.
- States exactly as frozen: `activeMask[f]` from segment left endpoint (per D5: each frequency its own left-endpoint active check over union-prefix traversal; terminal retention preserved — the segment ending at the first inactive point still contributes); `rangeEligible[f]` re-initialized from `activeMask[f]` per crossed range and cleared on that frequency's `gamma.imag()>0`; skip the range only if all frequencies become ineligible; `imageSum[f]` per eligible frequency, strictly True → Surface → Bottom, then ONE `workspace[f] += corrected[f] * imageSum[f]`.
- Frequency-local items never hoisted: epsilon, p/q/gamma/KMAH combinations, tau interpolation, principal/corrected, window, taper, phase, exponential. Pure geometry hoisted only: crossing topology, early range exit, degenerate-segment skip, W, interpolated position/slowness/real sound speed, Δz/Δz²/polarity.
- Correctness first; no performance tricks that touch the addition stream (HARD GATE §3).
- Reviewer MUST diff-check against current `CartesianCervenyInfluence::accumulateImpl` source semantics item by item: W; q/tau/gamma interpolation; principal; image Δz; polarity; window; taper; phase; corrected; one workspace add.

Evidence:
- `accumulateFusedPrevalidated` + `accumulateFusedImpl<CollectStatistics, ImageCount>` in src/field/cartesian_cerveny_influence.cpp (per-f own-prefix precompute, union traversal, activeMask/rangeEligible/imageSum states, shared image geometry, one workspace add per depth/frequency).
- Counter observation (munk 2F, --profile-influence): geometry counters exactly reuse/2 (e.g. depths 499,782,480 vs 999,564,960; image-geometry 1,499,347,440 vs 2,998,694,880); frequencyRangeKernel == reuse (4,972,960); frequencyImageKernel == reuse == imageEvaluations (2,998,694,880).
- Preliminary spot comparison: fused vs reuse SHD SHA256 identical on munk_cerveny_cc broadband_smoke 2F, constant_speed_direct 2F, and munk 16F.
- Reviewer item-by-item diff-check PASS record.

### R05 [ADVANCED] Numerical Parity Gates A–D
Status: DONE
Reviewer: PASS (2026-09-02; independent reproduction of 2 matrix rows + ctest; memcmp-level B/C verified in source; Fixture B divergence confirmed non-vacuous, 96/300 rays prefix 263 vs 313; 2 advisory notes: worklist status + cosmetic SHA-prefix column width)

Acceptance:
- Level A: cache fingerprint before == after (fused run, `--verify-cache`).
- Level B: raw workspace bitwise identical per frequency, fused vs current-reuse, same binary/build (uses the R02-frozen seam).
- Level C: scaled workspace bitwise identical per frequency.
- Level D: `SHA256(SHD fused) == SHA256(SHD reuse)` on all applicable CC coherent cases.
- Coverage: direct/simple CC; Munk spline CC; attenuation case; reflection case; curvature/epsilon variants within supported CC scope.
- If Level B/C cannot be exposed: STOP and record BLOCKER (no silent skip in favor of SHD-only comparison).
- Seam introduction itself must be byte-neutral for existing callers (reuse-mode SHD SHA256 unchanged before/after seam).
- Fallback (fused-off) and nonreuse outputs unaffected (V2-GATE-08).

Evidence:
- Parity matrix (case × mode × switch) with SHA256 records, all levels; archived.

### R06 [ADVANCED] Performance + Memory Acceptance
Status: DONE
Reviewer: coordinator-verified 2026-09-02 (independent reviewer subagent infra unavailable: provider error; all numbers recomputed independently from archived JSON/PRTs, harness unittests re-run, diff scope verified — independent stamp deferred to the final-reviewer stage which re-examines the whole batch)

Acceptance:
- Only after R05 PASS. Compare nonreuse / reuse / fused / parallel at 2F / 8F / 16F (+32F / 64F if memory allows; 64F = broadband_stress, 32F via override), ≥5 repeats, report median + dispersion, alternating runs, same build/threads/inputs, warm-up excluded.
- Report wall / Trace / Project / Influence / Scale / writer / peak RSS and all geometry + frequency-kernel counters, against the R01 baseline.
- Fix pre-existing stale parse: `benchmark_rayreuse.py` expects "Total solver and SHD seconds" (:246) while the binary prints "Total solver and product seconds" (`app/main.cpp:1142`); fix on the harness side (or freeze the exact resolution in the harness fix), keeping benchmark infra only — no production change.
- Expected counter behavior to verify: geometry counters ≈ baseline/Nf (union-prefix aware, not an exact identity); frequency-kernel counters remain O(Nf).
- Memory measured (V2-GATE-10): Nf × field bytes, per-ray temporaries peak, peak RSS vs reuse; frozen cache bytes independent of Nf.
- Go/No-Go (no preset speedup threshold): gates = correctness fully preserved; geometry dedup realized in counters; statistically meaningful end-to-end wall improvement beyond run noise; RSS reasonable. Failure ⇒ `NOT_VIABLE` verdict — fused path does not become a default or required production path.

Evidence:
- `../reports/REPORT_IGR1_R06_PERFORMANCE.md` + archived JSON `build/igr1-clean/benchmarks/igr1_r06_munk_{2f,8f,16f}.json` + counter PRTs `benchmarks/counters/munk_{2f,8f,16f}/`.
- Verdict: **NOT_VIABLE** for wall-time generalization of the v1 fused layout (fused/reuse = 0.70 / 0.96 / 0.93 at 2F/8F/16F; all gaps ≫ noise). Geometry dedup fully realized in counters (exactly 1/Nf); frequency-kernel counters exactly 1.0; RSS ≈ reuse + Nf × 1.61 MB; frozen cache bytes constant. Fused stays opt-in experimental mode per the frozen escape gate.
- Harness fixes: stale "Total solver and SHD seconds" parse corrected; fused mode wired into benchmark/standard_cases harnesses with cross-mode SHD identity enforcement retained; model_matrix defaults pinned. 24 harness unittests OK.
- 32F/64F rows DEFERRED by user decision (2026-09-02, runtime budget); memory model validated on 2F/8F/16F.

## 10. Execution Order

R01 → R02 (reviewer PASS gate) → R03 + R04 (construction; both may proceed in either order but R04 reviewer diff-check requires R03 integration for spot parity) → R05 (requires R03+R04 DONE) → R06 (requires R05 PASS) → CHECKPOINT → BATCH ACCEPTANCE → FINAL REVIEW. No next batch before IGR-1 ACCEPTED.

## 11. Findings / Blockers

- 2026-09-02 (process): reviewer-type subagent infrastructure became unavailable during the R06 checkpoint (provider configuration error). R06 acceptance was verified by the coordinator through independent recomputation of every headline number from the archived JSONs/PRTs, re-running the harness unittests, and diff-scope verification. The mandatory independent examination is discharged by the batch final-reviewer, which re-reads the full record.
- 2026-09-02 (scope): 32F/64F memory rows deferred by user decision; recorded in R06 report §5/§9. Not a blocker: ΔRSS ≈ Nf × field bytes validated on 2F/8F/16F.
