# IGR-1 — Fused Cross-Frequency Cartesian Cerveny Path — FROZEN DESIGN

> **Status: FROZEN (R02). Binding for R03–R06. Changes require architect re-approval.**
> **Date frozen:** 2026-09-02
> **Branch / base:** `feat/igr-influence-geometry-reuse`, HEAD `3ed475e` + accepted R01 instrumentation diff
> **Authority:** executes [`IGR-1_CC_FUSION_WORKLIST.md`](IGR-1_CC_FUSION_WORKLIST.md) §3–§8 (hard gates, D1–D14, §5 hierarchy, §6 memory, V2-GATE set). Where this doc and the worklist §5 hierarchy agree, both are authoritative; this doc freezes the API, ownership, counter, seam, rejection, and test-harness details the worklist delegates to R02.
> **Scope restated (frozen):** TL only; Cartesian Cerveny coherent pressure only; rectilinear receivers, uniform ranges; single source; one frozen trace pass; serial fused; `Bf = Nf`; existing `nonreuse`/`reuse`/`parallel` paths unchanged (HARD GATE §3). Out-of-scope list = worklist §2, including: parallel fused, multisource fused, frequency blocking, persistent geometry caches, SIMD, other beam families, ray-centered Cerveny, arrivals/eigenray IGR, RayPathCache schema change.

All source anchors below were re-verified against the working tree (R01 instrumentation applied) on 2026-09-02. Line numbers are current-tree numbers.

> **Post-R03/R04 review ratifications (2026-09-02, architect; frozen decisions unchanged):**
> 1. §2 R10 (non-uniform receiver ranges): RATIFIED as implemented — rejection lives in the environment parser `requireUniformRanges` (`src/io/environment_parser.cpp:1174-1179` → definition `:311-328`), with the `CartesianCervenyInfluence` ctor `validateUniformReceiverRanges` as the numeric last line of defense; no separate fused-specific uniformity message (consistent with the R02-review precedent of removing dead rejection branches).
> 2. As-built deviations from the §12 sketch RATIFIED (no behavioral impact): `FusedRayReuseStatistics` is a field-identical mirror of `SerialRayReuseStatistics` (not verbatim reuse); `accumulateFusedPrevalidated`/`accumulateFusedImpl` return `bool` (`false` = shared early range exit); field name `FusedAccumulationResult::rawWorkspaces`. §12 sketches below are updated to the as-built signatures; R05 writes against those.
> 3. Byte-level (memcmp) workspace comparison remains R05 Level B/C scope — R03's value-equality test assertion is preliminary only; the divergent-prefix fixture (Fixture B) is R05 scope (§10.1).

---

## 1. Execution Mode, CLI Token, PRT Surface

Frozen (V2-GATE-05 domain routing; no silent entry):

1. `include/rayreuse/io/command_line.hpp:12-16` — extend the enum:
   ```cpp
   enum class BroadbandExecutionMode { NonReuse, Reuse, Parallel, Fused };
   ```
2. CLI token `fused`: `--execution-mode fused`. Parse site `src/io/command_line.cpp:121-145`; usage string in `app/main.cpp` gains `fused` in the `--execution-mode` alternation. Both parse error messages at the parse site are part of the frozen CLI surface and must be updated to include `fused`: the missing-value error (`command_line.cpp:126-128`, `"--execution-mode requires 'nonreuse', 'reuse', or 'parallel'"`) and the bad-token error (`command_line.cpp:138-140`, `"--execution-mode must be 'nonreuse', 'reuse', or 'parallel'"`) each gain `fused` in the accepted-token list. No existing test asserts these exact strings (`tests/unit/command_line_test.cpp` asserts throw-only), so updating them is test-safe.
3. PRT marker (fused TL branch, mirrors the reuse branch `app/main.cpp:1036`):
   `execution mode = broadband fused reuse`
4. Wall field: `fused reuse wall seconds = <SerialRayReuseStatistics::wallSeconds>`.
5. `Trace passes = <sourceCount>` (= 1 for the in-scope single-source run), printed via the same frozen FP-2F §1.5 semantics as reuse.
6. Fused supports `--verify-cache` (fingerprint before/after, reuse-branch output shape `app/main.cpp:1051-1062`) and `--profile-influence` (fused is CC by definition, so the existing CC-only rule at `app/main.cpp:278-286` is consistent). `--profile-frequency-tasks`, `--worker-count`, `--output-queue-capacity`, `--memory-budget-mib` remain parallel-only; the existing guard at `src/io/command_line.cpp:236-243` already rejects them for any non-parallel mode including `Fused` — no change needed there.
7. `--frequencies-hz` override applies to fused exactly as to reuse.
8. `app/main.cpp:613-640` `writeProductExecutionMode` gains `case BroadbandExecutionMode::Fused: stream << "fused reuse\n"; break;` for switch exhaustiveness only — unreachable in practice because fused is rejected for every product that calls this helper (eigenray/arrivals; §2).

App dispatch (`app/main.cpp`): a new `else if (options.executionMode == BroadbandExecutionMode::Fused)` TL branch is inserted between the `Reuse` branch (:1005-1066) and the final `else` (parallel, :1067). This insertion point is mandatory: the TL dispatch chain ends in `else -> solveParallel`, so a `Fused` value that slipped validation would silently run the parallel solver (see §2, CRITICAL).

## 2. Validation — Rejection Matrix (CLI + solver, defense in depth)

No silent fallback anywhere (V2-GATE-05, V2-GATE-08). Two independent layers:

- **CLI layer** — `validateProductOptions` (`app/main.cpp:249-297`). Fused checks are added as **new, additive branches with fused-specific messages**; the existing reuse/parallel error strings are not edited (HARD GATE: existing paths unchanged — existing tests may match those strings). Two pre-existing catch-all conditions already reject any specified non-NonReuse mode (including `Fused`, which always sets `executionModeSpecified` per `command_line.cpp:142-143`): the R-product rule (`app/main.cpp:259-263`) and the single-frequency-TL rule (`app/main.cpp:271-277`). These two conditions are **narrowed to `(executionMode == Reuse || executionMode == Parallel)`** — the thrown reuse/parallel message strings stay byte-identical, so existing behavior and any text-matching tests are unchanged — and **fused-specific rejection branches are appended after them** with fused-specific messages, so a fused R-product or fused single-frequency-TL run fails with a fused-scoped diagnostic instead of the reuse/parallel text. This narrowing is a message-quality remediation, not a hazard fix: both rows already reject `Fused` today.
- **Solver layer** — `FusedRayReuseSolver` (§3) re-validates the same conditions on `SimulationCase` before tracing, with messages prefixed `fused ray-reuse solver requires ...` / `... is not supported by the fused ray-reuse solver`. Distinct text from the CLI layer so the failing layer is identifiable.

| # | Condition (on a `--execution-mode fused` run) | CLI (`validateProductOptions`) | Solver (`FusedRayReuseSolver`) |
|---|---|---|---|
| R1 | RayTrace (R product) | REJECT — already rejected today by the `app/main.cpp:259-263` catch-all (`executionMode != NonReuse`); that condition is narrowed to Reuse/Parallel (same message string) and a fused-specific branch is appended after it | n/a (unreachable; R dispatch precedes solver) |
| R2 | Arrivals (ASCII/binary) | REJECT — new branch in the `app/main.cpp:287-296` block. **CRITICAL:** the arrivals dispatch (`app/main.cpp:814-822`) ends in `else -> solveParallel`; without this rejection a fused arrivals run silently executes the parallel arrivals solver | n/a (unreachable) |
| R3 | Eigenray | REJECT — same new branch. Same hazard: `app/main.cpp:895-903` ends in `else -> solveParallel` | n/a (unreachable) |
| R4 | Single-frequency TL (`frequencies().size() == 1U`) | REJECT — already rejected today by the `app/main.cpp:271-277` catch-all (`executionMode != NonReuse`); that condition is narrowed to Reuse/Parallel (same message string) and a fused-specific branch is appended after it. No silent-ignore hazard exists: the run never reaches the single-frequency TL branch at `:932` | REJECT (`frequencies count >= 2`) |
| R5 | Non-coherent TL (Incoherent / SemiCoherent) | REJECT | REJECT |
| R6 | Beam family != `CervenyGaussian` | REJECT | REJECT |
| R7 | `cervenyCoordinateSystem() == RayCentered` | REJECT | REJECT |
| R8 | `sourceCount() > 1` | REJECT | REJECT |
| R9 | Irregular receivers (`receivers().isIrregular()`) | REJECT | REJECT |
| R10 | Non-uniform receiver ranges | REJECT (explicit fused message) | REJECT (pre-construction check; the `CartesianCervenyInfluence` ctor `validateUniformReceiverRanges`, `cartesian_cerveny_influence.cpp:104-122`, remains the shared last line of defense) |
| R11 | Non-TL, non-R, non-arrival, non-eigenray modes | covered by R5/R6 dispatch structure | covered by solver run-mode check (`isTransmissionLossMode` + `Coherent`) |

Acceptance seam for R03: each rejection row is exercised by a component/CLI-level test with the exact distinct error message (R03 acceptance "rejection paths").

## 3. Fused Solver — `FusedRayReuseSolver`

New files `include/rayreuse/solver/fused_ray_reuse_solver.hpp`, `src/solver/fused_ray_reuse_solver.cpp`. Structurally separate from `SerialRayReuseSolver` (D10: no rewrite of existing paths; the per-frequency path stays verbatim as reference).

### 3.1 Public API (exact signatures in §12)

1. `static FusedAccumulationResult accumulateFrequencies(simulation, const RayPathCache& sourceCache, epsilonMultiplier, loopRange, influenceSettings = {})` — the **Level B parity/test seam**: traces nothing, touches no cache beyond `const&`, returns **raw (unscaled)** per-frequency workspaces + timings + statistics. Used by the R05 component test and by nothing in the production dispatch path.
2. `static SerialRayReuseStatistics solveStreaming(simulation, epsilonMultiplier, loopRange, consumer, influenceSettings = {}, verifyCacheFingerprint = false)` — production entry, mirroring `SerialRayReuseSolver::solveStreaming` (`serial_ray_reuse_solver.cpp:29-100`) semantics. **Statistics struct frozen: reuse `SerialRayReuseStatistics` verbatim** (same fields, same meanings; `phaseTotals.influenceStatistics` holds the fused-run counters of §5). Rationale: identical shape keeps the PRT writer, fingerprint reporting, and R06 harness reuse-mode-compatible with zero new plumbing.

### 3.2 `solveStreaming` frozen semantics

1. Validate (§2 solver layer). Trace once via `SingleFrequencySolver::traceAllSourceFans` (validated `sourceCount()==1`, so exactly one fan; shared trace code, not duplicated). `tracePassCount = 1`.
2. If `verifyCacheFingerprint`: record `contentFingerprint()` before (reuse-branch statistics shape).
3. Allocate `Nf` `FrequencyWorkspace`, constructed exactly as the existing path constructs them (`FrequencyWorkspace(frequencies[f], simulation.receivers())`, zero-initialized; `single_frequency_solver.cpp:229-235` analog). Long-lived until consumer delivery (§7).
4. Per ray, in `RayPathCache::paths()` order:
   - shared per ray: `patternAmplitude = simulation.sourceBeamPattern().amplitudeForLaunchAngle(path.launchAngle)`; `baseSourceAmplitude = source.amplitude * patternAmplitude`; Lloyd branch is frequency-dependent but **dead in fused** (coherent-only, `usesLloydMirror` false; retained structurally identical to `single_frequency_solver.cpp:283-292` so the code shape mirrors production);
   - per frequency `f`: `FrequencyProjector::project(path, frequencies[f], projectedSourceAmplitude)` — **Project timing** (exact current call sequence);
   - per frequency `f`: `pickBeamEpsilon(widthMode, frequencies[f], sourceSoundSpeed, sourceSample.soundSpeedGradient.depth, path.launchAngle, launchFan.launchAngleStep, loopRange, epsilonMultiplier)` — **Influence timing** (matches production placement: epsilon pick sits between `projectEnd` and `influenceEnd`, `single_frequency_solver.cpp:321-352`);
   - then **one** `CartesianCervenyInfluence::accumulateFusedPrevalidated(workspaces, path, frequencyStates, epsilons, stats)` call — **Influence timing**.
5. After all rays, per frequency in index order: `scaleCoherentCartesianPressure(workspaces[f], simulation.receivers(), launchFan.launchAngleStep, sourceSoundSpeed, simulation.sourceGeometry())` — **Scale timing** — then `consumer(f, {std::move(workspaces[f])}, perFrequencyTimings)`. Consumer invocation is per frequency index AFTER scaling, identical contract to reuse mode (`RayReuseFrequencyConsumer`).
   Per-frequency `SingleFrequencyTimings` passed to the consumer: `scaleSeconds` = that frequency's scaling time; `projectSeconds`/`influenceSeconds` = 0.0; block-level totals live only in `statistics.phaseTotals` (D11: no fabricated per-frequency timing precision). The app-level fused consumer ignores timings (the reuse consumer does too).
6. Fingerprint after; mismatch throws (reuse-mode message shape, fused-prefixed text). `wallSeconds` at the end.
7. No `RayPathCache` mutation anywhere; the cache is handed out as `const&` only (V2-GATE-09, D8).

## 4. Fused CC Kernel — `CartesianCervenyInfluence::accumulateFusedPrevalidated`

Added to `CartesianCervenyInfluence` (keeps constructor validation shared with the existing path). Private, `const` member (needs `receivers_`, `settings_`, `widthMode_`, `sourceGeometry_`, `soundSpeedProfile_`), `friend class FusedRayReuseSolver` (mirrors the existing `friend class SingleFrequencySolver` for `accumulatePrevalidated`). Exact signature in §12. Implementation is a template `<CollectStatistics, ImageCount>` in `cartesian_cerveny_influence.cpp`, dispatched on `settings_.imageCount` (1/2/3) exactly like `accumulateWithImageCount` (`cartesian_cerveny_influence.cpp:600-621`).

Frozen kernel body (implements worklist §5 verbatim; current-kernel references are `accumulateImpl`, `cartesian_cerveny_influence.cpp:623-903`):

1. `rayAccumulations += Nf` (one fused ray call; preserves the per-frequency-ray meaning — R01 baseline 160,000 at 16F stays comparable).
2. Validation parity: `validatePrevalidatedInput` (`:228-285`) invoked once per `f` on `(workspaces[f], frequencyStates[f], epsilons[f])`; timed into `validationSeconds` under `CollectStatistics`. `validatedRayPoints`/`validatedWorkspaceValues` stay 0 (prevalidated route — identical to reuse mode, which never increments them in `accumulatePrevalidated`, `:976-997`).
3. Per frequency `f`:
   - `activePrefixPointCount[f]` — the exact scan of `:646-652` over `frequencyStates[f]` (first inactive point retained: `index + 1U`);
   - `precompute[f] = precomputeRayValues(path, soundSpeedProfile_, epsilons[f], activePrefixPointCount[f], widthMode_)` — **per-frequency precompute over ITS OWN prefix, never extended to the union prefix** (Obligation P3); timed into `precomputeSeconds`, `activeRayPoints += activePrefixPointCount[f]`;
   - `angularFrequency[f] = 2π · frequencyStates[f].frequency` (`:660-661`);
   - `radiusMax[f] = 30.0 · path.points.front().soundSpeed / frequencyStates[f].frequency` (`:662-663`).
4. Shared per ray: `beamWindowSquared` (`:664-665`), `ratio` (`:666-668`), receiver vectors, `receiversPerRange`, irregular-depth selection expression (`:678-679`, retained verbatim — frequency-independent and frozen-geometry-driven; irregular grids are rejected upstream anyway), boundary depths.
5. `unionPrefix = max_f activePrefixPointCount[f]` (D5).
6. `for rightIndex in [2, unionPrefix)`:
   - `segmentCandidates++`, `geometrySegmentEvaluations++` (union traversal: once per segment candidate);
   - shared `leftIndex`, `leftRange`, `rightRange`; **shared early return** when `rightRange > receiverRanges.back()` (with `hotLoopSeconds` accumulation, `:703-709`); **shared degenerate skip** (`:710-713`);
   - `activeMask[f] = frequencyStates[f].points[leftIndex].active`; **if no `f` is active: continue** (position matches the per-frequency left-endpoint skip `:718-720`; segments beyond a frequency's prefix are gated out exactly as the per-frequency loop bound gated them);
   - shared `firstUpper`/`secondUpper` via `fortranUpperRangeIndex` (`:722-725`); `firstUpper >= secondUpper` → continue; `eligibleSegments++` (shared).
7. Per crossed range (`oneBasedRange = firstUpper+1 .. secondUpper`):
   - `receiverRangeEvaluations++`, `geometryRangeEvaluations++` (shared, counted **even if every frequency is subsequently gamma-guard rejected** — the shared geometry was computed);
   - shared `weight`, interpolated `position`/`slowness`/`soundSpeed` (`:740-750`);
   - `rangeEligible[f] = activeMask[f]`; per `f` with `activeMask[f]`: `frequencyRangeKernelEvaluations++` (**including** frequencies that the gamma guard then rejects), interpolate `q[f]`/`tau[f]`/`gamma[f]` (`:758-766`); `gamma[f].imag() > 0.0` → `rangeEligible[f] = false`; else `principal[f] = ratio · sqrt(soundSpeed · |epsilons[f]| / q[f])`, `kmahFinal[f] = updateCervenyKmah(pre[f].q[leftIndex], q[f], pre[f].kmah[leftIndex], widthMode_)`, `corrected[f] = kmahFinal[f] < 0 ? -principal[f] : principal[f]`, with the `requireFiniteComplex` pair (`:771-778`) — all frequency-local;
   - **if no `f` is rangeEligible: continue** (the depth loop is not entered).
8. Per depth (`receiversPerRange`, existing order):
   - `receiverDepthEvaluations++`, `geometryDepthEvaluations++` (only depths of ranges with ≥1 eligible frequency are reached);
   - shared `receiverDepth` selection;
   - `imageSum[f] = 0` **for eligible frequencies** (zero-initialized every depth);
   - **runtime image loop** `imageIndex = 0 .. ImageCount-1`, kind order True → Surface → Bottom (kind derivation exactly as the diagnostic path, `:804-808`): shared `deltaDepth`/`polarity`/`deltaSquared` (`:409-424` semantics); `geometryImageGeometryEvaluations++` (per image, shared); then **per eligible `f`**: `imageEvaluations++`, `frequencyImageKernelEvaluations++`, `windowMetric = -angularFrequency[f] · gamma[f].imag() · deltaSquared`, window reject → `windowRejections++` (contribution 0), `taper = cervenyHermiteTaperUnchecked(deltaDepth, radiusMax[f], 2.0 · radiusMax[f])`, `taper == 0.0` → `taperRejections++` (contribution 0), `phaseArgument`, `negativeImaginaryExponential`, `contribution = polarity · rightAmplitude[f] · taper · exponential`, `nonzeroImageContributions++` if `!= 0`, and **`imageSum[f] += contribution` unconditionally for all three images in kind order, zero contributions included** (Obligation P1);
   - after the image loop, per eligible `f`: **one** `contribution = corrected[f] · imageSum[f]` and one workspace add `pressure[d · rangeCount + r] = pressureValue + contribution` in the exact read-add-assign form of `:838-847`, with the `#ifndef NDEBUG` finiteness checks retained (D13: exactly one workspace add per beam/depth/frequency; per-image workspace writes forbidden).
9. `hotLoopSeconds` on exit (CollectStatistics).

`rightAmplitude`/`rightReflectionPhase` are per-frequency (`frequencyStates[f].points[rightIndex]`) and are read per `f` inside the image kernel — never hoisted (D4).

## 5. Counter Semantics for Fused Mode (frozen table)

| Counter | Fused-mode increment site | Relation to R01 reuse baseline (per same inputs) |
|---|---|---|
| `rayAccumulations` | `+= Nf` per fused ray call | **equal** (160,000 @ 16F) |
| `validatedRayPoints` / `validatedWorkspaceValues` | never (prevalidated route) | equal (0, same as reuse) |
| `activeRayPoints` | `+= Σ_f activePrefixPointCount[f]` per ray | **equal** |
| `segmentCandidates` / `geometrySegmentEvaluations` | once per union-traversal segment candidate (`rightIndex ∈ [2, unionPrefix)`) | ≈ baseline/Nf, union-prefix aware (not exact) |
| `eligibleSegments` | once per candidate passing: no early return, non-degenerate, ≥1 active `f` at left endpoint, `firstUpper < secondUpper` | ≈ baseline/Nf (union aware) |
| `receiverRangeEvaluations` / `geometryRangeEvaluations` | once per crossed range of an eligible segment — **including ranges where all frequencies are then gamma-guard rejected** (shared geometry was computed) | ≈ baseline/Nf (union aware) |
| `frequencyRangeKernelEvaluations` | per (crossed range, `f` with `activeMask[f]`) **including gamma-guard-rejected `f`** | **equal per frequency** (sum over f equals baseline) |
| `receiverDepthEvaluations` / `geometryDepthEvaluations` | once per depth of a range with ≥1 rangeEligible `f` | ≈ baseline/Nf (union of per-frequency eligibility) |
| `geometryImageGeometryEvaluations` | once per (such depth, image) — shared | ≈ baseline/Nf |
| `imageEvaluations` / `frequencyImageKernelEvaluations` | per (such depth, image, rangeEligible `f`) — `imageEvaluations` keeps its per-frequency-kernel-entry meaning, so the R01 identity `imageEvaluations == frequencyImageKernelEvaluations` **also holds in fused mode** | **equal per frequency** |
| `windowRejections` / `taperRejections` / `nonzeroImageContributions` | per (depth, image, rangeEligible `f`) — unchanged semantics | **equal** |
| `validationSeconds` / `precomputeSeconds` / `hotLoopSeconds` | per-`f` prevalidated checks / per-`f` precompute / fused traversal (CollectStatistics only) | timing, not counter, parity |

R06 expectation (worklist R06): geometry counters ≈ baseline/Nf; frequency-kernel counters remain O(Nf).

## 6. Level B Seam on the Reuse Side — `WorkspaceDelivery`

Frozen exact shape (worklist §3 R05 seam policy — opt-in raw-workspace return, default behavior unchanged):

```cpp
enum class WorkspaceDelivery { Scaled, Raw };
```

Trailing defaulted parameter on `SingleFrequencySolver::solveFrequencyFromSourceCache` (§12). `Raw` semantics, precisely:

1. Skips **only** the coherent post-scale invocation (`scaleCoherentGeometricPressure` / `scaleCoherentCartesianPressure`, `single_frequency_solver.cpp:369-379`) and reports `scaleSeconds = 0.0`.
2. The `FrequencyWorkspace` construction/move (`:358-368`) still executes — the return type requires a workspace, and for CC coherent it is a move, not arithmetic. The intensity→pressure conversion (geometric families, I/S runs) also still executes when present: it is workspace construction, not the coherent scale. (R05 uses CC coherent only, where `Raw` is exactly the raw accumulated field.)
3. All existing callers pass nothing (default `Scaled`) and are byte-for-byte unchanged in behavior and compiled path.

**Proof obligation (V2-GATE-07/08, executed in R05):** reuse-mode SHD SHA256 on `munk_cerveny_cc` 16F and `constant_speed_direct` 16F unchanged before/after seam introduction (Level-D-level check), plus `ctest` green — evidence that the seam is inert for existing callers.

## 7. Ownership / Lifetime (frozen; V2-GATE-09, V2-GATE-10)

| State | Owner | Lifetime |
|---|---|---|
| `RayPathCache` (frozen fan) | `FusedRayReuseSolver::solveStreaming` local (`RayFanTraceResult`) | whole solve; handed out as `const&` only; zero write-back |
| `Nf` × `FrequencyWorkspace` | `solveStreaming` local vector | long-lived across all rays; moved out per frequency at consumer delivery |
| `Nf` × `RayFrequencyState` (projection results) | per-ray scratch vector in the solver loop | per ray (reuse buffer capacity across rays allowed; contents rewritten per ray) |
| `Nf` × epsilon values | per-ray scratch vector | per ray |
| `Nf` × `PrecomputedRayValues` (p/q/gamma/kmah) | inside `accumulateFusedPrevalidated` | per fused ray call (destroyed on return) |
| `activeMask` / `rangeEligible` / `imageSum` / `principal` / `corrected` / per-range interpolated values | kernel-local O(Nf) scratch | per ray / per range / per depth |

No `Nray`-persistent geometry; no geometric cache (D1/D2); no mutable global state; no `RayPathCache` schema change (D8). `Bf = Nf` (D6).

## 8. Timing Semantics (D11)

| Field | Content in fused mode |
|---|---|
| `Trace seconds` | the one fan trace (unchanged) |
| `Project seconds` | pattern amplitude (once per ray) + projection of all frequencies (per ray) |
| `Influence seconds` | epsilon picks (per ray per f) + per-f precompute + fused traversal + workspace adds — **block-level; per-frequency Influence time is not separable and is not fabricated** |
| `Scale seconds` | per-frequency `scaleCoherentCartesianPressure` (all f) |
| wall | `fused reuse wall seconds` |
| SHD seconds | app writer wrapper around `ShdFrequencyWriter` (setup + per-`writeFrequency` + finalize), reuse-branch pattern |

## 9. Memory Model

`Bf = Nf`. Long-lived extra vs reuse streaming ≈ `Nf × depthCount × rangeCount × 16 B` (+ small headers): Munk 201×501 ≈ 1.61 MB/frequency → 16F ≈ 26 MB, 64F ≈ 103 MB (worklist §6). Per-ray ≈ `Nf × (RayFrequencyState points + PrecomputedRayValues + epsilon + masks)`. No geometry cache. Verified by measurement in R06 (peak RSS, V2-GATE-10).

## 10. R05 Parity Harness (frozen)

### 10.1 Component test — `tests/component/fused_cc_parity_test.cpp` (new)

- Fixture A: small in-code munk-like CC coherent `SimulationCase`, Nf = 3 (e.g. 50/150/250 Hz), regular grid, single source, vacuum/rigid boundaries (image coverage).
- **Level B:** trace once (`traceSourceFan`); `raw_ref`: `for f in 0..Nf-1 → solveFrequencyFromSourceCache(sim, freqs[f], cache, 0, ..., WorkspaceDelivery::Raw)` in exact reuse loop order; `raw_fused`: `FusedRayReuseSolver::accumulateFrequencies(sim, cache, ...)`; assert **bitwise ==** per element per frequency (byte comparison of the pressure spans).
- **Level C:** scale both sides with production `scaleCoherentCartesianPressure` → bitwise ==.
- Fixture B (multi-frequency-prefix / lossy): constant-speed case with volume attenuation where per-frequency `<0.005F` cutoffs differ (different `activePrefixPointCount[f]`), exercising union-prefix traversal, terminal retention, and per-frequency gamma ineligibility. Levels B/C as above.
- Additionally: fused rejection-path unit checks (§2 solver layer) and statistics-shape checks (counter identities of §5 that are baseline-equal).

### 10.2 CLI-level Level A + D — frozen case list

Protocol: for each row, run the same `igr1-clean` binary twice in the case's results dir — `--execution-mode reuse --verify-cache` and `--execution-mode fused --verify-cache` (add `--profile-influence` on the munk rows for counter comparison) — record both PRTs and both SHD SHA256; **Level A** = fused fingerprint before == after (and == reuse fingerprint); **Level D** = `SHA256(SHD fused) == SHA256(SHD reuse)`; archive hashes + PRTs. Also re-run the two 16F reuse rows pre/post seam (§6 proof obligation).

All rows verified: `'CC'` coherent run option, Cerveny beams, regular rectilinear uniform-range grids, single source, rayreuse-compatible (present in `test/standard_cases/results/rayreuse/`), broadband profile available.

| # | Case | Profile (Nf) | Coverage |
|---|---|---|---|
| 1 | `munk_cerveny_cc` | `broadband_smoke` (2F: 50, 250) | Munk CC primary smoke |
| 2 | `munk_cerveny_cc` | `broadband_regression` (16F: 50..500 step 30, exact R01 CSV) | primary regression; R01 baseline anchor |
| 3 | `constant_speed_direct` | `broadband_smoke` (2F: 50, 250) | direct/simple CC |
| 4 | `constant_speed_direct` | `broadband_regression` (16F) | scale |
| 5 | `constant_speed_thorp` | `broadband_regression` (16F: 1000..10000) | lossy volume attenuation (complex travel time re-integrated per frequency) at scale |
| 6 | `volume_attenuation_francois_garrison` | `broadband_smoke` (2F: 5000, 10000) | Francois-Garrison lossy attenuation |
| 7 | `constant_speed_vacuum_rigid` | `broadband_smoke` (2F: 100, 500) | reflection polarity: vacuum surface + rigid bottom |
| 8 | `elastic_halfspace_flat` | `broadband_smoke` (2F: 1000, 2000) | frequency-dependent elastic half-space bottom reflection |
| 9 | `cerveny_width_wkb_flat_gradient` | `broadband_smoke` (2F: 100, 250) | WKB epsilon variant (real epsilon, alternate KMAH branch rule) |
| 10 | `cerveny_width_space_filling_flat_gradient` | `broadband_smoke` (2F) | space-filling epsilon variant |
| 11 | `cerveny_curvature_zero_flat_gradient` | `broadband_smoke` (2F) | curvature variant |
| 12 | `cerveny_curvature_double_flat_gradient` | `broadband_smoke` (2F) | curvature variant |
| 13 | `munk_spline` | `broadband_smoke` (2F: 50, 250) | Munk spline CC (worklist R05 "Munk spline CC") |

Excluded, with reason: `attenuation_unit_w` (CC coherent 2F but no rayreuse results tree — would require env generation; lossy coverage is already carried by rows 5-6); `cerveny_width_wkb` / `cerveny_width_space_filling` / `cerveny_curvature_*` non-flat-gradient variants (origin/f2cpp-only compatibility; the flat-gradient rows 9-12 carry the same width/curvature coverage in the rayreuse suite). Epsilon single-frequency-only cases are covered instead via the `--frequencies-hz` broadband profiles above; no override-only rows are needed.

If Level B/C cannot be exposed at R05: STOP and record BLOCKER (worklist R05; no silent SHD-only substitution).

## 11. Parity Obligations (binding on R03/R04; verified by V2-GATE-07)

- **P1 — imageSum addition stream.** Per eligible frequency, all three image contributions are added unconditionally in True → Surface → Bottom order, zeros included, starting from `imageSum[f] = 0`: the complex addition stream `((0 + T) + S) + B` is preserved bitwise. Skipping a zero add or reassociating is forbidden — `0.0 + (-0.0) == +0.0` makes even "harmless" restructuring a bitwise hazard (D10/D13/D14).
- **P2 — exact production call sequence.** Pattern amplitude → (Lloyd branch, dead in fused) → `FrequencyProjector::project` → `pickBeamEpsilon` → influence accumulate, with the same argument order and the same frequency-independent inputs (`sourceSoundSpeed`, `sourceSample.soundSpeedGradient.depth` evaluated once per solve from the geometry SSP — value-identical to the per-frequency recomputation in reuse mode). Timings split at the same points (§8).
- **P3 — per-frequency precompute over its own prefix.** `precomputeRayValues` is never extended to the union prefix. Values at shared indices are identical anyway (p/q/gamma are pointwise in the frozen path; the kmah chain is forward-sequential from index 0, so prefix length cannot change earlier entries), but own-prefix also keeps the unconditional `requireFiniteComplex` validation coverage per frequency identical to reuse mode.
- **P4 — shared checks are frequency-independent by construction.** Early range exit, degenerate-segment skip, crossing topology (`firstUpper`/`secondUpper`), weight/position/slowness/soundSpeed interpolation, Δz/Δz²/polarity depend only on frozen `RayPath` geometry and the receiver grid — hoisting them is exact, not approximate.
- **P5 — prevalidated validation parity.** `validatePrevalidatedInput` runs once per `(workspace[f], frequencyState[f], epsilon[f])` before the fused traversal (defense in depth at kernel entry; mirrors `accumulatePrevalidated` `:981-996`).
- **P6 — one workspace add.** Exactly one `workspace[f] += corrected[f] · imageSum[f]` per (ray, range, depth, eligible f), in the read-add-assign form of the current kernel; per-image workspace writes forbidden (D13).
- **P7 — existing paths unchanged.** `nonreuse`/`reuse`/`parallel` outputs and PRTs byte-stable (V2-GATE-08); the only reuse-side production diff allowed is the inert `WorkspaceDelivery` seam (§6) plus the additive enum/CLI/dispatch surface of §1-§2.
- **P8 — cache integrity.** Fingerprint before == after under fused; zero `RayPathCache` write-back (V2-GATE-09).

## 12. Interface Sketch (exact signatures; bodies are R03/R04 scope)

```cpp
// include/rayreuse/io/command_line.hpp
enum class BroadbandExecutionMode { NonReuse, Reuse, Parallel, Fused };
// CLI token: "fused"; usage text updated accordingly.

// include/rayreuse/solver/single_frequency_solver.hpp
enum class WorkspaceDelivery { Scaled, Raw };

class SingleFrequencySolver {
 public:
  [[nodiscard]] static SingleFrequencyResult solveFrequencyFromSourceCache(
      const SimulationCase& simulation, double frequency,
      const RayPathCache& rayCache, std::size_t sourceIndex,
      double epsilonMultiplier, double loopRange,
      CartesianCervenySettings influenceSettings = {},
      WorkspaceDelivery delivery = WorkspaceDelivery::Scaled);
  // All other signatures unchanged; solveFrequencyFromCache / solveAtFrequency
  // forward with the default (Scaled) and are untouched.
};

// include/rayreuse/solver/fused_ray_reuse_solver.hpp
struct FusedAccumulationResult {
  std::vector<FrequencyWorkspace> rawWorkspaces;  // raw (unscaled); size == Nf; index == frequency index
  SingleFrequencyTimings timings;              // scaleSeconds == 0; fused-run influenceStatistics
  std::size_t rayCount{};
  std::size_t totalRayPointCount{};
  std::size_t rayCacheBytes{};
};

class FusedRayReuseSolver {
 public:
  // Level B seam: no tracing, no scaling, no cache mutation, no consumer.
  [[nodiscard]] static FusedAccumulationResult accumulateFrequencies(
      const SimulationCase& simulation, const RayPathCache& sourceCache,
      double epsilonMultiplier, double loopRange,
      CartesianCervenySettings influenceSettings = {});

  // Production entry; FusedRayReuseStatistics is a field-identical mirror of
  // SerialRayReuseStatistics (same fields, same meanings; ratified 2026-09-02).
  [[nodiscard]] static FusedRayReuseStatistics solveStreaming(
      const SimulationCase& simulation, double epsilonMultiplier,
      double loopRange, const RayReuseFrequencyConsumer& consumer,
      CartesianCervenySettings influenceSettings = {},
      bool verifyCacheFingerprint = false);
};

// include/rayreuse/field/cartesian_cerveny_influence.hpp (private + friend)
class CartesianCervenyInfluence {
 private:
  friend class FusedRayReuseSolver;

  // bool return: false = shared early range exit (as-built, ratified 2026-09-02).
  [[nodiscard]] bool accumulateFusedPrevalidated(
      std::span<FrequencyWorkspace> workspaces, const RayPath& path,
      std::span<const RayFrequencyState> frequencyStates,
      std::span<const std::complex<double>> epsilons,
      CartesianCervenyStatistics* statistics = nullptr) const;

  template <bool CollectStatistics, std::size_t ImageCount>
  [[nodiscard]] bool accumulateFusedImpl(
      std::span<FrequencyWorkspace> workspaces, const RayPath& path,
      std::span<const RayFrequencyState> frequencyStates,
      std::span<const std::complex<double>> epsilons,
      CartesianCervenyStatistics* statistics) const;  // .cpp, explicit ImageCount dispatch 1/2/3
};
```

## 13. Risks & Residual Risks

1. **Signed-zero / addition-stream drift (top numerical risk).** Any reassociation of `imageSum` or the workspace add breaks bitwise parity even when numerically equal. Mitigation: P1/P6 are hard obligations; R05 Level B catches it element-wise.
2. **Enum fallthrough regressions.** Adding `Fused` to `BroadbandExecutionMode` makes every existing `if (Reuse) ... else /* parallel */` chain a potential silent-fused-into-parallel hazard (arrivals `app/main.cpp:814-822`, eigenray `:895-903`, TL `:1067`). Mitigation: additive CLI rejections (§2) + explicit fused dispatch branch; `-Wswitch=enum` surfaces non-exhaustive switches (`writeProductExecutionMode` case added).
3. **Union-prefix traversal vs per-frequency semantics.** Segments beyond a short-prefix frequency's cutoff must contribute nothing for that frequency; guaranteed only by `activeMask[f]` gating identical to the per-frequency loop bound plus terminal retention. Mitigation: R05 Fixture B (lossy, divergent cutoffs).
4. **Residual (accepted):** fused-mode `receiverRangeEvaluations`/`receiverDepthEvaluations`/`eligibleSegments`/`segmentCandidates` legacy counters count union-traversal events (shared), so new==legacy counter identity from R01 does **not** hold for these in fused mode — intentional (geometry dedup is the point); only `imageEvaluations == frequencyImageKernelEvaluations` and the per-frequency-kernel counters remain baseline-equal (§5). PRT consumers must not assume fused==reuse counter magnitudes.
5. **Residual (accepted):** `WorkspaceDelivery::Raw` on non-CC-coherent runs still performs the intensity→pressure conversion (construction, §6.2) — the seam is only guaranteed "raw" for CC coherent, which is the only R05 usage; documented to prevent misuse.
6. **Performance not guaranteed.** If R06 shows no statistically meaningful wall improvement or unacceptable RSS, verdict is `NOT_VIABLE`: the fused path stays non-default, does not become required production, and IGR-1 records the escape (worklist R06; no scope creep to "fix" performance inside this batch).

## 14. Out of Scope (restated)

Everything in worklist §2, explicitly including: parallel fused, multisource fused, frequency blocking, persistent geometry caches of any kind, receiver-depth/image materialization, frequency interpolation, rolling projector, SIMD/GPU, nested parallelism, tree/ray-local/thread-local reductions, Arrival/Eigenray IGR, other beam families, RayPathCache schema redesign, unrelated refactoring. CC intensity fusion stays on the existing path.
