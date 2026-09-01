# IGR-0 Final Remediation — Cross-Frequency Influence Geometry Fusion

> **Batch:** IGR-0 Final Documentation Remediation / Architecture Freeze
> **Date:** 2026-09-01
> **Branch:** `feat/igr-influence-geometry-reuse`
> **Inspected production HEAD:** `b33abfb`
> **Status:** `ACCEPTED` — independent final-review passed; record: [`../reviews/IGR0_FINAL_DOCUMENTATION_REMEDIATION_FINAL_REVIEW_2026-09-01.md`](../reviews/IGR0_FINAL_DOCUMENTATION_REMEDIATION_FINAL_REVIEW_2026-09-01.md)
> **Scope:** documentation only. No production/test change; no benchmark execution; no IGR-1 CONSTRUCT.
> **Current authority:** this report together with [`../../../doc/reference/REFERENCE_INFLUENCE_GEOMETRY_REUSE.md`](../../../doc/reference/REFERENCE_INFLUENCE_GEOMETRY_REUSE.md).
> **IGR-1 state:** [`../worklists/IGR-1_CC_FUSION_DESIGN_DRAFT.md`](../worklists/IGR-1_CC_FUSION_DESIGN_DRAFT.md) remains `DESIGN DRAFT — NOT APPROVED, NOT IN CONSTRUCTION`.

## A. VERDICT

`ACCEPTED`

Independent final-review passed after the reviewer finding was remediated and re-validated. This verdict closes IGR-0 documentation remediation only; it does not authorize IGR-1 construction.

## B. FINAL ARCHITECTURE

### B.1 Production facts that determine the nesting

Current serial reuse is frequency-major: trace all source fans once, then `frequency → source → solveFrequencyFromSourceCache` (`serial_ray_reuse_solver.cpp:40-83`). The single-frequency field path allocates one workspace, iterates rays, projects frequency-local state, accumulates Influence, performs pressure scaling, and only then returns the workspace (`single_frequency_solver.cpp:227-392`).

Current Cartesian Cerveny order is:

```text
ray
→ segment (rightIndex ascending)
→ crossed receiver range (ascending)
→ receiver depth (ascending)
→ image (True → Surface → Bottom)
→ corrected * imageSum
→ one workspace addition
```

Evidence: `cartesian_cerveny_influence.cpp:678-735, 758-822`. The fast path forms `imageSum` in True/Surface/Bottom order (`:458-488`) and adds `corrected * imageSum` to the workspace once (`:806-822`). This addition shape is part of the numerical contract.

### B.2 Frozen exact hierarchy

For Cartesian Cerveny, frequency belongs at two frequency-local consumption points inside ray traversal:

1. a range-local preparation loop for q/tau/gamma/principal and guards;
2. the innermost image-kernel loop, after shared image geometry is built.

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

Why `image → frequency`, rather than `frequency → image`: it constructs Delta-z/Delta-z-squared/polarity once per receiver/image geometry and immediately serves every range-eligible frequency. `activeMask[f]` is the segment-left-endpoint state; `rangeEligible[f]` is reinitialized from it for every crossed range, then cleared when that frequency's interpolated `gamma.imag()>0`. Only range-eligible frequencies initialize/use `corrected[f]`, enter depth/image evaluation, or add to their workspace. This prevents a frequency rejected by the production range-local gamma guard from consuming uninitialized range state. A per-frequency `imageSum[f]` retains the production True → Surface → Bottom addition sequence and preserves the single workspace addition per beam/depth.

The authoritative lifecycle is therefore:

```text
Trace
→ Frozen RayPathCache
→ per-ray multi-frequency projection
→ fused Influence accumulation into raw workspace[f]
→ per-frequency Pressure Scaling
→ consumer
→ SHD write
```

Scaling cannot be collapsed into writer delivery: Cartesian coherent scale contains `sqrt(workspace.frequency())` (`pressure_scaling.cpp:55-60`), is timed separately as `scaleSeconds`, and runs before the solver returns (`single_frequency_solver.cpp:355-392`). The writer only validates and serializes scaled pressure (`shd_writer.cpp:138-207`).

## C. FROZEN DECISIONS

- **D1 — Frequency loop below ray traversal.** Cross-frequency work is performed inside each ray; the full frequency-major Influence replay is not the IGR path.
- **D2 — Transient geometry reuse.** Receiver geometry is produced once and immediately consumed across frequencies in the current block.
- **D3 — Persistent geometry cache deferred.** Segment-range stencil, receiver geometry cache, receiver-depth interval and sparse geometry cache are future candidates, not IGR-1 v1. Full receiver-depth/image materialization remains REJECTED.
- **D4 — Frequency-local physics boundary.** Complex travel time, source/reflection amplitude evolution, reflection phase, active prefix, epsilon, p/q/gamma/KMAH frequency combinations, principal, window, taper, phase, exponential and contribution remain exact per frequency.
- **D5 — Per-frequency active prefix.** Geometry uses the union/max reachable prefix, but every frequency applies its own left-endpoint active guard. No reference-frequency prefix is allowed.
- **D6 — `Bf=Nf` default.** Single-source v1 fuses all frequencies.
- **D7 — Blocking is future memory policy.** `Bf<Nf` is the same algorithm executed by frequency blocks, not a second algorithm. Automatic blocking is not in v1.
- **D8 — Frozen `RayPathCache` unchanged.** It remains immutable, frequency-independent and schema-stable; no frequency-local state is written back.
- **D9 — Per-frequency pressure scaling retained.** Raw workspaces are scaled individually before consumer/writer; Scale seconds remain a first-class timing.
- **D10 — Per-frequency accumulation order preserved.** Fixed f observes the existing ray → segment → range → depth → True/Surface/Bottom sequence and one workspace addition per beam/depth.
- **D11 — Serial fused reference first.** IGR-1 v1 targets a serial correctness/reference path with the current path retained as reference/fallback.
- **D12 — Parallel deferred.** No nested parallelism, thread-local reduction or parallel ownership redesign in v1.
- **D13 — `FrequencyProjector` semantics unchanged.** It still projects every ray at every frequency exactly; no rolling projector.
- **D14 — IGR removes geometry duplication, not frequency physics.** Frequency-kernel work is expected to remain proportional to active frequencies.

### C.1 PHYSICS AND ACTIVE-PREFIX BOUNDARY

#### C.1.1 Geometry outside the frequency kernel

For Cartesian Cerveny, the shared set is: segment endpoints and crossing topology; degenerate/last-range geometry guards; W; interpolated position, slowness and real sound speed; receiver depth geometry; and image Delta-z/Delta-z-squared/polarity. Frozen p1/p2/q1/q2 and path events remain shared inputs, but their epsilon-dependent combinations are not shared results.

Production image signs (`cartesian_cerveny_influence.cpp:408-418`) are:

| Image | Delta-z | polarity |
|---|---:|---:|
| True | `receiverDepth - interpolatedDepth` | `+1` |
| Surface | `-receiverDepth + 2*seaSurfaceDepth - interpolatedDepth` | `-1` |
| Bottom | `-receiverDepth + 2*seabedDepth - interpolatedDepth` | `+1` |

The sign is semantically material because phase contains `interpolatedSlowness.depth * deltaDepth` (`:440-443`); Delta-z-squared alone is insufficient.

#### C.1.2 Frequency-local layer

Per frequency: q/tau/gamma interpolation; epsilon-dependent precompute; principal/corrected/KMAH; `gamma.imag()>0`; `-omega*Im(gamma)*Delta-z-squared` window; `radiusMax=30*c0/f` taper; phase; exponential; image contribution; imageSum; final contribution; workspace addition.

Lossy `complexTravelTime.real()` is frequency-local because production integrates reciprocal complex sound speed at that frequency (`frequency_projector.cpp:169-190`). The reference adopts an exact geometry/frequency-state decomposition, not a universal lossy multiplicative theorem.

#### C.1.3 Active prefix

Source point starts `active=true` (`frequency_projector.cpp:107-113`). Reflection events update accumulated amplitude and phase per frequency; active is the monotone conjunction with the `<0.005F` accumulated projected-amplitude cutoff (`:60-65, 153-155, 192`). The first inactive terminal point is retained, and only an inactive segment left endpoint suppresses the suffix (`cartesian_cerveny_influence.cpp:633-641, 700-705`).

Frozen statement:

> Per-frequency active-prefix differences arise from frequency-local source/reflection amplitude evolution and the cumulative active cutoff, not directly from volume attenuation.

Volume attenuation enters field contribution through complex travel time and does not directly modify `active`. A fused block uses `unionPrefix = max reachablePointCount(f)` and independently executes the equivalent of `if (!frequencyState[f].points[leftIndex].active) continue;` for each frequency.

#### C.1.4 Reference corrections beyond CC

- **Simple Gaussian:** production uses `legacyArcLength=(rightIndex+W)*configuredStepLength`, `closestPointDistance=abs(deltaDepth*segmentRange)/segmentLength`, `offRayDistance=sqrt(deltaDepth^2-closestPointDistance^2)`, `effectiveDistance=legacyArcLength+offRayDistance`, and `angularOffset=atan(closestPointDistance/effectiveDistance)` (`simple_gaussian_influence.cpp:193-245`). Receiver range and simple depth difference are not substitutes.
- **GeoGaussian:** near-field sigma uses `0.2*f*complexTravelTime.real()` (`geometric_gaussian_influence.cpp:240-252, 287-300`); it is frequency-local for lossy media and cannot use frozen `tau_real`.

## D. COUNTER MODEL

Existing `imageEvaluations` is incremented immediately before the complete per-frequency image kernel and is followed by window/taper/phase/contribution work (`cartesian_cerveny_influence.cpp:398-455`). It is not a pure image-geometry counter. IGR-1 instrumentation must split the model.

### D.1 Geometry-side counters

| Counter | Definition | Expected ideal trend for `Bf=Nf` |
|---|---|---|
| `geometrySegmentEvaluations` | shared segment candidate/eligibility geometry evaluation | approaches baseline / Nf |
| `geometryRangeEvaluations` | shared crossed-range topology plus W/position/slowness/c | approaches baseline / Nf |
| `geometryDepthEvaluations` | shared receiver-depth geometry unit | approaches baseline / Nf |
| `geometryImageGeometryEvaluations` | shared Delta-z/Delta-z-squared/polarity construction | approaches baseline / Nf |

“Approaches” is intentional: union-prefix work can exceed any one frequency's prefix, so an exact division identity is not promised.

### D.2 Frequency-kernel counters

| Counter | Definition | Expected trend |
|---|---|---|
| `frequencyRangeKernelEvaluations` | one active frequency performs q/tau/gamma interpolation, principal and range-local guards on prepared range geometry | generally remains per frequency |
| `frequencyImageKernelEvaluations` | one frequency executes the complete Cerveny image kernel on prepared receiver/image geometry | generally remains per frequency |
| `windowRejections` | frequency image kernel rejected by window | per-frequency physics |
| `taperRejections` | window survivor rejected by taper | per-frequency physics |
| `nonzeroImageContributions` | nonzero per-frequency image contribution | per-frequency physics |

No document may claim `frequencyImageKernelEvaluations ≈ baseline/Nf`. The performance thesis is geometry de-duplication, not elimination of exact frequency kernels. Timings must report at least Wall, Trace, Project, Influence, Scale, and consumer/write where measured.

## E. NUMERICAL PARITY MODEL

The design uses **Conditions designed to preserve bitwise parity**; it does not claim a theorem or sufficiency/necessity proof. Implementation verification is authoritative.

For fixed f, moving physical execution from `f0(all rays), f1(all rays)` to `ray0(f0,f1), ray1(f0,f1)` preserves f's observed ray order. Within each ray, segment/range/depth/image order stays unchanged. The `imageSum[f]` form prevents three image contributions from becoming three workspace additions. Other frequencies write distinct workspaces and do not enter f's addition stream.

Forbidden in IGR-1 v1: tree reduction, ray-local field reduction, thread-local reduction, SIMD reassociation, depth/image reorder, or any optimization that changes the floating-point addition stream.

Four mandatory levels:

- **Level A — Frozen cache integrity:** `RayPathCache` fingerprint before == after.
- **Level B — Unscaled workspace parity:** for every frequency, raw fused `workspace[f]` bitwise equals raw current-reuse `workspace[f]` in the same binary/compiler/FP environment.
- **Level C — Scaled workspace parity:** after per-frequency Pressure Scaling, scaled fused `workspace[f]` bitwise equals scaled current-reuse `workspace[f]`.
- **Level D — Final product parity:** `SHA256(fused SHD) == SHA256(reference reuse SHD)`.

If an intermediate workspace gate cannot be exposed during implementation, the exact engineering blocker must be documented and reviewed. Level B/C may not be silently skipped in favor of SHD-only comparison.

## F. MEMORY MODEL

The frozen model is

$$
M_{total}\approx M_{frozen\ cache}+B_f M_{field}
+M_{per-ray\ frequency\ temporaries}+M_{small\ orchestration}.
$$

| Lifetime | State |
|---|---|
| Long-lived across all rays | `Bf × FrequencyWorkspace`; v1 has `Bf=Nf` |
| Per-ray temporary, released or reused after one ray | `Nf × RayFrequencyState`, `Nf × Cerveny precompute`, `Nf × epsilon`, active mask, `imageSum[f]` |
| Unchanged | immutable frozen `RayPathCache` |
| Small orchestration | frequency metadata, counter/timing state, workspace handles |

Future memory pressure may select `Bf<Nf`; every block still runs the same fused algorithm. v1 does not implement automatic blocking and does not introduce `Nray × Nsegment × Nreceiver` persistent geometry storage.

## G. SUPERSEDED DOCUMENT MATRIX

| Old document / section | Status | Replacement | Reason |
|---|---|---|---|
| `REPORT_INFLUENCE_FREQUENCY_AUDIT_2026-08-25.md` Executive Summary persistent cache recommendation | SUPERSEDED (PARTIAL), historical audit | this report B/C | current user decision freezes transient cross-frequency fusion |
| same report §7.2 geometry cache layers | SUPERSEDED (PARTIAL), future candidates only | this report C/F | v1 does not build persistent geometry storage |
| same report §15 architecture and §15.1 stencil replay | SUPERSEDED (PARTIAL) | this report B | core loop is fused inside ray traversal |
| same report §15.3 integration inside `solveFrequencyFromCache()` | SUPERSEDED | this report B.2 | a single-frequency API cannot express cross-frequency fused traversal |
| same report §16 Step 1 exact geometry replay prototype | SUPERSEDED | this report H | IGR-1 proposal is CC fused traversal, not replay |
| same report Audit Verdict where it implies the next roadmap | SUPERSEDED (PARTIAL) | this report A/H | its measurements/classification remain historical evidence, not current roadmap |
| `REPORT_IGR0_INFLUENCE_GEOMETRY_REUSE_AUDIT.md` §4/§5 candidate ranking and prototype | SUPERSEDED (PARTIAL) | this report C/G | persistent stencil is deferred |
| same report §6 cache ownership/invalidation contract | DEFERRED / historical | this report F | no v1 geometry cache exists to own/invalidate |
| same report §9.2 cache-replay IGR-1 gates | SUPERSEDED | this report D/E/F/H | gate model now covers fusion, scale and intermediate parity |
| `IGR-0_INFLUENCE_GEOMETRY_REUSE_AUDIT_WORKLIST.md` persistent-cache decisions/gates | SUPERSEDED (PARTIAL), historical worklist | this report and IGR-1 design draft | old batch snapshot is not current execution authority |
| `REFERENCE_INFLUENCE_GEOMETRY_REUSE.md` former universal factorization theorem | CORRECTED | current reference §2 | lossy real and imaginary complex travel time are frequency-local |
| same reference former CC image formulas | CORRECTED | current reference §3.3 | Surface/Bottom signs and linear phase term must match production |
| same reference former Simple Gaussian / GeoGaussian simplifications | CORRECTED | current reference §4 | production geometry and lossy near-field sigma are more specific |
| same reference former persistent stencil implementation strategy | SUPERSEDED | current reference §6/§10 | transient loop fusion is the sole current architecture |
| same reference former Bitwise Parity Theorem | CORRECTED | current reference §7 | implementation gates, not an unproved sufficiency/necessity claim, are authoritative |
| `IGR0_REVISION_FINAL_REVIEW_2026-09-01.md` earlier review of pre-remediation revision | HISTORICAL / SUPERSEDED | `IGR0_FINAL_DOCUMENTATION_REMEDIATION_FINAL_REVIEW_2026-09-01.md` | earlier review did not cover the final scaling/counter/parity/reference corrections |

The following prior facts remain valid: G/M/F/T/O classification where consistent with current source; `<0.005F` cumulative active cutoff; `<1e-5F` acoustic-half-space raw reflection suppression; terminal-point retention; lossy complex travel time real/imag frequency dependence; rejection of full receiver-depth/image materialization; and the historical benchmark measurements as measurements, not roadmap authority.

## H. IGR-1 FINAL SCOPE PROPOSAL

IGR-1 scope is confirmed, not authorized for construction:

- TL only;
- Cartesian Cerveny only;
- coherent pressure only;
- rectilinear receivers with uniform receiver ranges;
- single source;
- shared frozen fan;
- all-frequency fusion, `Bf=Nf`;
- serial fused reference path first;
- existing current-reuse path retained as reference/fallback;
- unchanged immutable `RayPathCache` schema;
- unchanged exact per-ray/per-frequency `FrequencyProjector` semantics;
- exact hierarchy in B.2;
- counter split in E;
- Level A-D parity gates in E;
- memory measurements against G, including Scale seconds and peak RSS during later authorized construction.

Out of scope: production/test changes in IGR-0; IGR-1 implementation before approval; frequency interpolation/reconstruction; persistent geometry cache prototype; rolling projector; parallel fusion; multisource; automatic blocking; all-family unification; FI; SIMD; GPU; Arrival/Eigenray IGR; unrelated refactor.

The frozen direction is singular:

```text
Trajectory reuse
→ Cross-frequency Influence fusion
→ receiver geometry generated once
→ immediate exact multi-frequency consumption
```

It is not “cache all geometry, then replay by frequency.”
