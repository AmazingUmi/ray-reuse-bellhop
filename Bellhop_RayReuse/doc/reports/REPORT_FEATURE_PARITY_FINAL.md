# Bellhop_F2CPP → Bellhop_RayReuse Feature Parity Final Report

> Acceptance date: 2026-08-30  
> Accepted HEAD: `0721fb3036ebaa26bbd72fcb20458e9118317457` (`0721fb3`)  
> Verdict: **Production Feature Parity COMPLETE**  
> Remaining F2CPP production parity GAP: **0**

## 1. Executive Summary

At `0721fb3`, the repository-level evidence supports the formal conclusion that
`Bellhop_F2CPP → Bellhop_RayReuse` two-dimensional production Feature Parity is
**COMPLETE**, with **0 remaining GAPs**. The accepted surface has a traceable
`parser → model → runtime → product → regression/oracle` chain. This
conclusion covers the documented production slices; it does not mean that every
Cartesian product of independently supported feature axes was tested or is legal.

This is a closeout audit, not a replay of FP-1A–FP-2I. It reconciles the retained
status documents, parity audit, support matrix, frozen Worklists, Batch Reports,
usage contract, Git history, current-HEAD health gates, and a small representative
executable smoke set. No production code was changed by this acceptance.

The authoritative closeout records are the
[sequence status](../status/STATUS_FEATURE_PARITY_SEQUENCE_2026-08-29.md),
[repository status](../status/STATUS_PROGRESS.md),
[production parity audit](REPORT_F2CPP_RAYREUSE_PARITY_2026-08-25.md), and
[feature support matrix](../reference/REFERENCE_FEATURE_SUPPORT_MATRIX.md).

## 2. Scope and Status Semantics

The parity target is the production-supported two-dimensional feature surface of
`Bellhop_F2CPP`. RayReuse preserves that surface while adding a broadband
execution model based on a frozen, frequency-independent ray cache and
frequency-local acoustic projection and products.

Status terms in this report are deliberately narrow:

- **PARITY**: F2CPP production support and a complete RayReuse
  `parser → model → runtime → product → regression/oracle` evidence chain
  exist for the stated slice.
- **F2CPP_OUT_OF_SCOPE**: F2CPP itself has no formal production support for the
  item; its absence in RayReuse is not a parity GAP.
- **RAYREUSE_EXTENSION / DEFERRED**: a RayReuse-specific broadband, research, or
  optimization direction outside the F2CPP parity target.
- **Intentional divergence**: an intentionally different external lifecycle or
  validation behavior that preserves the accepted numerical product semantics.

The principal scope references are the
[final support matrix](../reference/REFERENCE_FEATURE_SUPPORT_MATRIX.md),
[usage guide](../guides/GUIDE_USAGE.md), and the accepted FP-2 closeout evidence:
[PCHIP Worklist](../worklists/FP-2B_PCHIP_SSP_WORKLIST.md) /
[Batch Report](../workreports/FP-2B_PCHIP_SSP_BATCH_REPORT.md),
[N² Worklist](../worklists/FP-2C_N2_LINEAR_SSP_WORKLIST.md) /
[Batch Report](../workreports/FP-2C_N2_LINEAR_SSP_BATCH_REPORT.md),
[Spline Worklist](../worklists/FP-2D_CUBIC_SPLINE_SSP_WORKLIST.md) /
[Batch Report](../workreports/FP-2D_CUBIC_SPLINE_SSP_BATCH_REPORT.md),
[Q Worklist](../worklists/FP-2E_QUADRILATERAL_SSP_WORKLIST.md) /
[Batch Report](../workreports/FP-2E_QUADRILATERAL_SSP_BATCH_REPORT.md),
[Source/Receiver Worklist](../worklists/FP-2F_SOURCE_RECEIVER_GENERALIZATION_WORKLIST.md) /
[Batch Report](../workreports/FP-2F_SOURCE_RECEIVER_GENERALIZATION_BATCH_REPORT.md),
[Boundary Worklist](../worklists/FP-2G_BOUNDARY_MATERIAL_CLOSURE_WORKLIST.md) /
[Batch Report](../workreports/FP-2G_BOUNDARY_MATERIAL_CLOSURE_BATCH_REPORT.md),
[Attenuation Worklist](../worklists/FP-2H_ATTENUATION_CLOSURE_WORKLIST.md) /
[Batch Report](../workreports/FP-2H_ATTENUATION_CLOSURE_BATCH_REPORT.md), and
[Line Source Worklist](../worklists/FP-2I_WORKLIST.md) /
[Batch Report](../workreports/FP-2I_LINE_SOURCE_CLOSURE_BATCH_REPORT.md).

Some retained Batch Reports were not subsequently rewritten to embed every
standalone final-review transcript. This is an archival limitation, not a feature
GAP: the repository closeout and status records contain no open finding, retained
remediation/re-review records close the known `CHANGES_REQUIRED` findings, and
the final repository-level review is required to accept this report before the
acceptance is closed.

## 3. Feature Parity Summary

| Feature domain | F2CPP production surface | RayReuse accepted implementation | Status | Primary evidence | Notes |
|---|---|---|---|---|---|
| Core Ray / Geometry | 2D ray tracing, dynamic ray state, boundary reflection events | Frozen `RayPath`/`RayPathCache` geometry, dynamic-ray bases and raw reflection events; per-frequency acoustic projection | **PARITY** | [Parity audit](REPORT_F2CPP_RAYREUSE_PARITY_2026-08-25.md), [Q report](../workreports/FP-2E_QUADRILATERAL_SSP_BATCH_REPORT.md), [boundary report](../workreports/FP-2G_BOUNDARY_MATERIAL_CLOSURE_BATCH_REPORT.md) | Frequency-local amplitude, phase, complex travel time and reflection results never become cache state |
| SSP | C-linear, PCHIP `P`, N²-linear `N`, cubic spline `S`, quadrilateral `Q`/`.ssp` | Matching real geometry evaluators and frequency-local acoustic evaluators for the accepted product slices | **PARITY** | [P report](../workreports/FP-2B_PCHIP_SSP_BATCH_REPORT.md), [N report](../workreports/FP-2C_N2_LINEAR_SSP_BATCH_REPORT.md), [S report](../workreports/FP-2D_CUBIC_SPLINE_SSP_BATCH_REPORT.md), [Q report](../workreports/FP-2E_QUADRILATERAL_SSP_BATCH_REPORT.md) | Q is limited to the explicit FP-2E slice described below |
| Beam / Influence | Cartesian/ray-centered Cerveny and GeoHat, Cartesian GeoGaussian, coherent Simple Gaussian; applicable coherence, width, curvature, component and source-pattern options | Corresponding parser dispatch, Influence algorithms, frequency projection and products | **PARITY** | [Parity audit §4.1](REPORT_F2CPP_RAYREUSE_PARITY_2026-08-25.md), [support matrix](../reference/REFERENCE_FEATURE_SUPPORT_MATRIX.md) | Illegal combinations remain explicit errors; ray-centered GeoGaussian is out of F2CPP production scope |
| Source / Receiver | Point/line source, single/multiple source depths, rectilinear and Cartesian paired irregular receivers | Value-owned source geometry, per-source frozen fan caches, stable source sequencing, rectilinear and paired-irregular addressing | **PARITY** | [FP-2F report](../workreports/FP-2F_SOURCE_RECEIVER_GENERALIZATION_BATCH_REPORT.md), [FP-2I report](../workreports/FP-2I_LINE_SOURCE_CLOSURE_BATCH_REPORT.md) | Ray-centered irregular receivers are out of scope; ray-centered products retain regular/equal-range restrictions |
| Boundary / Material | V/R/A/G/F, `.ati/.bty`, LS/LL, canonical curvilinear `C`, acoustic and elastic media, `.trc/.brc` | Matching geometry/material parsing, frozen raw event identity and frequency-local reflection evaluation | **PARITY** | [FP-2G Worklist](../worklists/FP-2G_BOUNDARY_MATERIAL_CLOSURE_WORKLIST.md), [FP-2G report](../workreports/FP-2G_BOUNDARY_MATERIAL_CLOSURE_BATCH_REPORT.md) | Canonical curvilinear support is short-format V/R; `CS`/`CL` are rejected |
| Attenuation | N/F/M/W/Q/L units; Thorp, Francois–Garrison, Biological; water-column and supported boundary paths | Exactly-once conversion across five SSP backends, volume models, and frequency-local boundary acoustic properties | **PARITY** | [FP-2H Worklist](../worklists/FP-2H_ATTENUATION_CLOSURE_WORKLIST.md), [FP-2H report](../workreports/FP-2H_ATTENUATION_CLOSURE_BATCH_REPORT.md) | Environment owns immutable attenuation parameters; converted acoustic state is not cached in geometry |
| Products | SHD, RAY, ASCII ARR, Binary ARR, Eigenray | Compatible single-frequency products plus ordered per-frequency broadband extensions where defined | **PARITY** | [Parity audit §4.7](REPORT_F2CPP_RAYREUSE_PARITY_2026-08-25.md), [usage guide](../guides/GUIDE_USAGE.md), [FP-2I report](../workreports/FP-2I_LINE_SOURCE_CLOSURE_BATCH_REPORT.md) | Broadband R is explicitly rejected because F2CPP/Origin defines no merged multi-frequency R product |
| Execution Modes | Repeated single-frequency execution | `nonreuse`, `reuse`, and `parallel`, with identical per-frequency products in accepted evidence | **PARITY + RAYREUSE_EXTENSION** | [Parity audit §5](REPORT_F2CPP_RAYREUSE_PARITY_2026-08-25.md), [support matrix](../reference/REFERENCE_FEATURE_SUPPORT_MATRIX.md), accepted FP reports above | Broadband orchestration is a RayReuse extension; product identity and cache invariance are parity-preserving contracts |

### 3.1 Core Ray / Geometry

`RayPath` and `RayPathCache` contain frequency-independent trajectory geometry,
quadrature/dynamic-ray bases, termination information, and raw reflection-event
material/segment identity. Geometry tracing includes the accepted C/P/N/S/Q SSP
semantics and flat, piecewise-linear, and canonical curvilinear boundary paths.
Dynamic-ray curvature and discontinuity rules are computed during trace and frozen
with the path. Projection at a target frequency derives amplitude, phase, complex
travel time, active/terminal prefix and complex reflection results without writing
them back into the cache. For multisource cases, the reuse unit is one independent
frozen fan per source depth.

### 3.2 SSP

- **C-linear**: piecewise-linear sound speed geometry and frequency-local complex
  acoustic evaluation.
- **PCHIP `P`**: monotone Hermite coefficients, continuous gradient and nonzero
  Hessian integrated into dynamic ray and per-frequency projection.
- **N²-linear `N`**: segment-linear N² geometry, node gradient jumps, nonzero
  in-segment Hessian, and frequency-local complex N² evaluation.
- **Cubic spline `S`**: exact F2CPP-compatible not-a-knot coefficient construction,
  continuous node gradient, cubic edge extrapolation, nonzero Hessian and
  independently rebuilt complex coefficients per frequency.
- **Quadrilateral `Q` / `.ssp`**: only the accepted FP-2E narrow slice is claimed:
  2D, one point source at one source depth, rectilinear receivers; TL Cartesian
  Cerveny `CC`, single-frequency R, and Cartesian GeoHat `G` A/a/E. The accepted
  Q geometry is range-dependent bilinear interpolation with cell-boundary gradient
  jumps and explicit domain failure. This report does **not** claim Q support for
  multisource, line source, irregular receivers, ray-centered families, arbitrary
  R variants, or products outside that slice.

### 3.3 Beam / Influence

The accepted surface comprises Cartesian Cerveny and ray-centered Cerveny;
Cartesian GeoHat and ray-centered GeoHat; Cartesian GeoGaussian; and coherent
Cartesian Simple Gaussian. Where defined by F2CPP, this includes coherent,
incoherent and semicoherent modes (`C/I/S`), Cerveny width modes `F/M/W`, curvature
modes `D/S/Z`, pressure/vertical/horizontal component selectors `P/V/H`,
directional `.sbp`, and point/line source scaling. Each family retains its own
Influence law and legal combination matrix. Simple Gaussian remains coherent-only
and point-source-only; ray-centered families retain their regular receiver and
equal-range requirements.

### 3.4 Source / Receiver

Point and line sources, one or multiple source depths, and rectilinear receivers
are supported across their documented product slices. Multisource inputs are
stably depth-sorted; each source owns an independent frozen launch fan, while
writers preserve F2CPP-compatible source ordering and headers. Cartesian paired
irregular receivers require `NRz == NRr`. Cartesian Cerveny intentionally preserves
the Origin/F2CPP irregular legacy rule of using `Rz(1)`; Cartesian GeoHat and
GeoGaussian use paired addressing. Ray-centered irregular receivers are not
claimed, and ray-centered Arrival/Eigenray support retains its narrower
single-source/rectilinear/equal-range contract.

### 3.5 Boundary / Material

The accepted material surface includes flat V/R/A/G/F boundaries, tabulated
`.trc/.brc`, piecewise-linear `.ati/.bty` (`LS`/`LL`), canonical curvilinear `C`
short format, acoustic halfspaces, flat ordinary elastic P/S halfspaces, and
acoustic/elastic LL. Geometry and raw event/material identity are frozen;
frequency-dependent acoustic or elastic reflection results are evaluated locally
for each frequency. Canonical curvilinear support is intentionally limited to the
accepted V/R short-format slice; unsupported `CS`/`CL` combinations fail early.

### 3.6 Attenuation

Supported units are N/F/M/W/Q/L. Thorp, parameterized Francois–Garrison and
0–200-layer Biological volume attenuation are implemented, including overlapping
Biological layers. The five frequency-domain SSP backends perform node-first
attenuation conversion with their accepted semantics, and supported boundary
materials receive acoustic attenuation exactly once. Immutable attenuation model
parameters belong to `Environment`; converted complex acoustic values remain
frequency-local and never alter frozen geometry.

### 3.7 Products

- **SHD**: F2CPP-compatible single-frequency semantics and one ordered
  multi-frequency RayReuse SHD file.
- **RAY (R)**: Origin-compatible single-frequency trajectory/prefix output,
  including multisource sequencing; explicit multi-frequency R is rejected.
- **ASCII ARR / Binary ARR**: F2CPP-compatible single-frequency records, line-source
  amplitude scaling and multisource sequencing; broadband runs publish one
  frequency-addressed file per frequency.
- **Eigenray (E)**: frozen ray-prefix output for accepted Cartesian G/B and
  ray-centered g receiver traversals; broadband runs publish one ordered file per
  frequency.

Writers are not shared by parallel workers. Products are first written to temporary
files and atomically published; failed broadband runs clean up products created by
that run. These lifecycle safeguards are intentional divergences from the legacy
single-run interface, not numerical parity gaps.

### 3.8 Execution Modes

- `nonreuse`: traces once per `(frequency, source)` and produces frequency-local
  products.
- `reuse`: traces once per source, then serially projects the frozen fan for every
  requested frequency.
- `parallel`: traces once per source; frequency workers read the const cache vector
  and compute independent frequency-local state, while an ordered serial consumer
  publishes products.

For `Nfreq` frequencies and `NSz` source depths, the frozen trace-count contract is
`Nfreq×NSz / NSz / NSz` for `nonreuse / reuse / parallel`. Accepted evidence verifies
per-frequency product identity across the three modes and `before == after` cache
fingerprints for reuse and parallel. The current frequency-level parallel ownership
is an implementation choice, not a permanent restriction on future scheduling.

## 4. Architecture Preservation

The final implementation preserves the central RayReuse contract:

```text
RayPath / RayPathCache
→ frequency-independent frozen geometry

amplitude / phase / complex travel time / reflection result
Arrival / Eigenray / Influence workspace / writer state
→ frequency-local
```

No global current-frequency state is used. No supported projection or product path
writes frequency-local acoustic state into the cache. Multisource generalization
adds a value-owned vector of independent per-source caches without changing the
cache schema or `contentFingerprint()` algorithm. Parallel workers share only const
geometry/cache state; mutable acoustic workspaces are worker-local, and publishing
is ordered and serial. The final protected-core audit found no change to the three
`RayPath`/`RayPathCache` files over the audited Feature Parity range.

## 5. Product Compatibility

| Product | Single-frequency compatibility | Broadband RayReuse behavior | Sequencing / lifecycle boundary |
|---|---|---|---|
| SHD | Compatible field layout and accepted numerical semantics | One multi-frequency SHD | Frequency index is stable; multisource header/addressing is preserved |
| RAY (`R`) | Origin-compatible ray/prefix output | Explicitly unsupported; no invented merged format | Per-source blocks and header order are stable |
| ASCII ARR (`A`) | Compatible arrival records | One frequency-named `.arr` per frequency | Source blocks and frequency publish order are stable |
| Binary ARR (`a`) | Compatible binary arrival records | One frequency-named `.arr` per frequency | Source blocks and frequency publish order are stable |
| Eigenray (`E`) | Compatible accepted ray-prefix semantics | One frequency-named `.ray` per frequency | Per-source sections and frequency publish order are stable |

Line-source field scaling and ARR amplitude scaling follow the accepted F2CPP
semantics. Multisource writers preserve source-depth ordering and compatible
headers. Parallel computation never grants workers shared mutable writer ownership.
The exact command and filename contract is maintained in the
[usage guide](../guides/GUIDE_USAGE.md).

## 6. Final Validation

> **Detailed scientific parity is inherited from the accepted FP Batch evidence; this final acceptance only performs repository-level health verification.**

This final acceptance did **not** repeat all historical Batch oracle matrices,
including the 459-angle probe, the 54/75 attenuation validator matrices, or the
full SSP × beam × product × execution-mode cross-product. Those scientific and
numerical conclusions remain grounded in the accepted Worklists and Batch Reports
linked above.

Current-HEAD repository health results:

| Gate | Result |
|---|---|
| Release configure/build | **PASS** |
| RayReuse Release CTest | **41/41 PASS** |
| Repository pytest | **187 passed**, including **393 subtests** |
| Standard-cases unit suite | **172/172 PASS** |
| `git diff --check` | **PASS** |
| Protected reference diff, `39c0407..HEAD` | `Bellhop_origin/` and `Bellhop_F2CPP/`: **0 changed files** |
| Frozen core diff, `39c0407..HEAD` | Three `RayPath`/`RayPathCache` files: **0 changed files** |

The representative executable smoke set was deliberately small:

| Coverage intent | Case | Profile / mode | Result |
|---|---|---|---|
| Classic TL | `constant_speed_direct` | single / nonreuse | **PASS** |
| Broadband reuse | `munk_spline` | broadband / reuse | **PASS** |
| Cartesian paired irregular receiver | `irregular_receiver_pairs` | broadband / parallel | **PASS** |
| Quadrilateral SSP | `q_range_dependent_cross_gradient` | broadband / reuse | **PASS** |
| Canonical curvilinear boundary | `i3_curvilinear_oracle` | broadband / parallel | **PASS** |
| Attenuation | `volume_attenuation_francois_garrison` | broadband / reuse | **PASS** |
| Line source | `source_geometry_line` | broadband / parallel | **PASS** |
| Arrival / Eigenray family | `arrival_line_directional_multisource` | broadband / reuse | **PASS** |

These gates provide final-HEAD health evidence; they do not replace the accepted
scientific oracles cited in Section 3.

## 7. Performance Snapshot

Performance is a machine-specific snapshot of parity-complete HEAD `0721fb3`, not
a release threshold or a cross-hardware guarantee. External wall time is the main
cross-mode metric; internal Trace, Project and Influence phases are diagnostic
because stage ownership is not identical between F2CPP and RayReuse. The sampling
protocol and interpretation rules follow the
[benchmarking guide](../guides/GUIDE_BENCHMARKING.md) and
[single-thread microbenchmark guide](../guides/GUIDE_SINGLE_THREAD_MICROBENCHMARK.md).

### 7.1 Environment

| Item | Snapshot value |
|---|---|
| CPU / machine | Apple M4 Mac mini, 10 cores (4 performance + 6 efficiency), 24 GiB |
| Operating system | macOS 26.6.2, arm64 |
| Compiler / CMake | Apple clang 21.0.0 / CMake 4.4.2 |
| Build mode | Release, `-O3 -DNDEBUG` |
| Parallel configuration | 8 workers, output queue 2, memory budget 2048 MiB |
| Broadband sampling | 1 warmup + 3 measured repetitions |
| Single-frequency sampling | External wall: 1 warmup + 5 measured repetitions; formula-core stages: 1 warmup + 3 measured repetitions |
| Thread controls | `OMP_NUM_THREADS=1`, `OPENBLAS_NUM_THREADS=1`, `VECLIB_MAXIMUM_THREADS=1` |
| RayReuse executable SHA-256 | `5e60ac10…e85136c` |
| F2CPP executable SHA-256 | `1689973a…f03e9` |

### 7.2 Single-frequency snapshot

Times are seconds. For the two C++ implementations, formula core is
`Trace + Project + Influence`; it is a diagnostic subtotal, not a replacement for
external wall time.

| Case | F2CPP wall | RayReuse wall | RayReuse / F2CPP | F2CPP formula core | RayReuse formula core | F2CPP Influence | RayReuse Influence |
|---|---:|---:|---:|---:|---:|---:|---:|
| Direct TL, 50 Hz | 0.04188 | 0.04143 | 0.989 | 0.03772 | 0.03788 | — | — |
| Munk TL, 50 Hz | 1.39029 | 1.04154 | 0.749 | 1.38823 | 1.03477 | 1.32820 | 0.97180 |

On these two cases, the current RayReuse single-frequency executable did not show
a positive external-wall overhead relative to F2CPP: it was about 1.1% lower on
direct and 25.1% lower on Munk. Direct formula-core time was effectively equal (RayReuse
0.4% higher), while the Munk difference was concentrated in the measured formula
core and Influence time. This is a two-case snapshot, not a general claim that
RayReuse is always faster than F2CPP, and it is not a causal profiler result.

### 7.3 Broadband end-to-end snapshot

Times are external wall-clock medians in seconds. `F2CPP repeated` is the sum of
repeated single-frequency F2CPP execution for the same requested frequencies.

| Case | Frequencies | F2CPP repeated | RayReuse nonreuse | RayReuse reuse | RayReuse parallel | Reuse speedup vs nonreuse | Parallel speedup vs reuse |
|---|---:|---:|---:|---:|---:|---:|---:|
| Direct TL | 16 | 0.71198 | 0.92350 | 0.35159 | 0.12151 | 2.627× | 2.894× |
| Munk TL (Influence-heavy) | 2 | 6.03417 | 8.28172 | 7.96400 | 5.17546 | 1.040× | 1.539× |

Relative to repeated F2CPP, direct RayReuse nonreuse was 1.297× slower, reuse was
2.025× faster, and parallel was 5.859× faster. On the two-frequency Munk case,
RayReuse nonreuse was 1.372× slower, reuse was 1.320× slower, and parallel was
1.166× faster. These are production-profile comparisons, not equal-ray
microbenchmarks: repeated F2CPP plans a launch fan independently for every
frequency, while RayReuse broadband uses one shared fan sized by its broadband
planning policy. The comparisons therefore mix launch-fan workload, process and
product lifecycle costs and complement, rather than contradict, the controlled
single-frequency microbenchmark above.

### 7.4 Phase, trace-count and memory diagnostics

Times are seconds. Parallel Project and Influence values are sums of frequency-task
time, so they can exceed external wall time and must not be interpreted as serial
elapsed time.

| Case / mode | Trace | Project | Influence | External wall | Trace passes | Peak RSS (MiB) |
|---|---:|---:|---:|---:|---:|---:|
| Direct, F2CPP repeated | 0.42726 | 0.01348 | 0.22176 | 0.71198 | 16 | 38.44 |
| Direct, RayReuse nonreuse | 0.61222 | 0.01812 | 0.28747 | 0.92350 | 16 | 39.14 |
| Direct, RayReuse reuse | 0.03955 | 0.01787 | 0.28855 | 0.35159 | 1 | 38.75 |
| Direct, RayReuse parallel | 0.03943 | 0.03520 | 0.53657 | 0.12151 | 1 | 39.94 |
| Munk, F2CPP repeated | 0.36979 | 0.02431 | 5.60500 | 6.03417 | 2 | 304.53 |
| Munk, RayReuse nonreuse | 0.58321 | 0.03732 | 7.64525 | 8.28172 | 2 | 332.11 |
| Munk, RayReuse reuse | 0.29253 | 0.03538 | 7.61600 | 7.96400 | 1 | 305.84 |
| Munk, RayReuse parallel | 0.29099 | 0.03666 | 7.71384 | 5.17546 | 1 | 307.61 |

Reuse reduced measured Trace time by 93.54% for direct and 49.84% for Munk;
the corresponding trace-pass reductions were 16→1 and 2→1. The existing memory
estimator reported a 31,453,072-byte direct cache and 31,641,568-byte estimated
parallel working set; for Munk it reported 317,572,080 and 320,794,512 bytes,
respectively. These are estimator outputs, distinct from the measured peak RSS
shown in the table.

The cross-implementation work counts make that comparison boundary concrete.
Direct repeated F2CPP traced 5,570 rays / 2,858,882 points, while RayReuse
nonreuse traced 8,000 / 4,106,080 and reuse/parallel traced 500 / 256,630.
Munk repeated F2CPP traced 6,000 / 2,021,052; RayReuse nonreuse traced 10,000 /
3,367,946 and reuse/parallel traced 5,000 / 1,683,973. The F2CPP/RayReuse wall
ratios in Section 7.3 therefore describe each executable's actual production
planning behavior; they do not isolate implementation overhead at equal ray count.

The independent 1-warmup/5-measurement external-wall evidence was available to
this review as the temporary artifact `/tmp/fp_final_single_external_wall_1w5r.json` (SHA-256
`8d4d7b3708fa7762541782ca6bc39ced911cb17836d630d9f0f653be23a8583a`).
Formula-core and Influence medians come from the separate 1-warmup/3-measurement
stage benchmark; the two sampling sets are not treated as the same observation.
The repeated-F2CPP broadband evidence was likewise reviewed from the temporary
artifact `/tmp/fp_final_f2cpp_repeated_broadband_1w3r.json` (SHA-256
`faa6c1c52fe3c702f75bf0953c455cc9747e125d71a0608400c26baae45cd9db`).

RayReuse products were byte-identical across its three execution modes. The
repeated F2CPP products passed the standard-case validator (direct 16/16 and Munk
2/2), but no F2CPP/RayReuse byte-identity claim is made across their different
per-frequency versus multi-frequency SHD containers. The benchmark runner had a
field-name drift between historical
`Total solver and SHD seconds` and current `Total solver and product seconds` PRT
output. This snapshot used a one-off alias shim in the benchmark invocation to read
the current field; no production or repository source was changed. The drift is a
benchmark-tool compatibility issue, not a production correctness blocker.

## 8. Performance Analysis

The measured analysis separates three different questions:

1. **Parity performance gap — single-frequency cost.** The isolated single-frequency
   sample showed no positive RayReuse wall overhead on either selected case
   (`0.989×` and `0.749×` F2CPP wall). The direct formula core was essentially
   equal; Munk favored RayReuse in the measured Influence path. By contrast,
   broadband `nonreuse`, which includes repeated orchestration and product
   lifecycle and a different broadband launch-fan workload, was 1.297× and
   1.372× slower than repeated F2CPP. The available data establishes the observed
   production-profile gap but does not attribute it among launch-fan planning,
   abstraction, projection, cache/model ownership, writer lifecycle, or Influence
   without a controlled equal-ray profiler experiment.
2. **RayReuse broadband advantage — geometry reuse.** Reuse improved end-to-end
   RayReuse wall time by 2.627× on direct and 1.040× on Munk. Direct converted a
   16-trace workload into one trace and removed 93.54% of measured Trace time. Munk
   converted two traces into one, but Influence accounted for 95.74% of reuse solver
   wall, leaving little serial reuse headroom. The result demonstrates why trace
   reduction alone is not an end-to-end speedup prediction.
3. **Parallel gain.** Eight-worker parallel execution improved over serial reuse by
   2.894× on direct and 1.539× on Munk. It made both cases faster than repeated
   F2CPP in this snapshot, by 5.859× and 1.166× respectively. The smaller Munk gain
   is consistent with its Influence-heavy work and frequency count of only two;
   these results are specific to the tested Apple M4 configuration and do not
   promise the same scaling on other hardware.

The primary current bottleneck is Influence after geometry reuse, especially for
Munk. Reuse shifts the performance ceiling away from Trace and toward
frequency-local Project/Influence and product work. Parallelism recovers part of
that cost at a modest measured RSS and estimated working-set increase in these
cases, but the cumulative task-stage figures must not be compared directly with
wall time.

## 9. Remaining Non-Parity Work

There are no remaining F2CPP production parity GAPs. The following items are
classified boundaries, not reopened Feature Parity work.

### 9.1 F2CPP_OUT_OF_SCOPE

- 3D / Bellhop3D / N×2D;
- beam shift;
- ray-centered geometric Gaussian;
- analytic continuous SSP formulas outside the supported discrete-grid surface.

### 9.2 RAYREUSE_EXTENSION / DEFERRED

- new broadband algorithms beyond the accepted frozen-geometry projection model;
- frequency interpolation and Influence Geometry Reuse;
- HDF5 containers and a hypothetical merged multi-frequency R container;
- SIMD optimization;
- BARR and other research arrival algorithms.

The existing broadband SHD and per-frequency A/a/E lifecycle is a supported
RayReuse extension. Atomic publication, early validation, and cleanup behavior are
intentional divergences. Future work on any deferred item requires a new scoped
design and evidence set; it must not be treated as a silent fallback or as a
retroactive Feature Parity requirement.

## 10. Final Verdict

Repository evidence, accepted Batch evidence, current-HEAD health validation and
the protected-core audit support the following formal verdict:

```text
Bellhop_F2CPP → Bellhop_RayReuse
Production Feature Parity: COMPLETE
Remaining F2CPP parity GAP: 0
```

No new correctness issue was identified by the repository-level audit or final
smoke validation. Performance differences are recorded separately from functional
parity and do not alter this correctness verdict.
