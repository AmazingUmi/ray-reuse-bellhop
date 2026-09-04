# IGR-3 Design — Unified Fused Influence Execution Architecture

> **Status:** FROZEN DESIGN — IGR-3A ACCEPTED (2026-09-04, closing
> record in the
> [`IGR-3A worklist`](IGR-3A_TL_BEAM_FAMILY_ADAPTATION_WORKLIST.md)):
> the frozen sections below are unchanged and remain the architecture
> authority. IGR-3A construction completed A01-A09 with per-task review
> records in the worklist (authoritative); Final Review returned ACCEPTED
> after one remediated LOW doc-level finding. IGR-3B has NOT started.
> **Branch:** `feat/igr-influence-geometry-reuse`
> **Baseline:** HEAD `38137a4`, clean tree
> **Date frozen:** 2026-09-03
> **Re-frozen:** 2026-09-03 — OPEN #1 resolved by user decision (§13 item 1);
> §1, §2, §3, §4, §5, §6.2, §7, §8, §9, §11, §13 re-frozen for mode-complete
> C/I/S fused coverage. All other PASSed content unchanged.
> **Authority:** [`IGR-3_SCOPE_AND_ARCHITECTURE_DECISION.md`](IGR-3_SCOPE_AND_ARCHITECTURE_DECISION.md)
> is the user-frozen scope authority. This document freezes architecture only
> within that scope and does not expand it.

## 1. Mission recap

One execution architecture — Cross-Frequency Fused + Static Range Parallelism —
carries all broadband Influence products: one fused executor (range partition,
workers, cache, timing, output ownership) + beam-family kernels (CC
[reference], RC Cerveny, GeoHat x2, GeoGauss, SimpleGauss) + sinks (TL in
all legal accumulation modes per family: coherent pressure everywhere,
plus incoherent/semi-coherent intensity→pressure for Cerveny, Geometric
Hat, and Geometric Gaussian — §6.2, user-resolved OPEN #1; Arrival lane in
IGR-3B). IGR-3A adapts the four
remaining TL families; CC stays reference and its A02 migration onto the
unified executor must be bit-identical with performance magnitude preserved.
IGR-3B is boundary-frozen only (§12).

## 2. Layer model

```text
FusedRayReuseSolver (public entry, scope gate, solveStreaming flow)
  └─ accumulateFrequenciesImpl<Adapter, Sink>  [single template, closed set]
       ├─ Adapter (per family, thin, compile-time)  §4
       │    └─ Kernel::accumulateFused{,Intensity}  §5  (family-private science)
       └─ Sink (per run mode, compile-time): FusedPressureWorkspace or
            FusedIntensityWorkspace + per-frequency sink  §6, §7
```

## 3. Unified executor (frozen)

### 3.1 Mechanism

The executor is a **private static member-function template of
`FusedRayReuseSolver`, parameterized over an Adapter struct**, defined in
`src/solver/fused_ray_reuse_solver.cpp`; the closed adapter set (CC, RC
Cerveny, Hat-Cartesian, Hat-RC, GeoGaussian, SimpleGaussian) is instantiated
implicitly via the public `accumulateFrequencies` dispatch within the same
translation unit — namespace-scope explicit instantiation of a private
member template would fail access checking, and none is used (the closed
dispatch preserves the closed set). Rationale: member functions of an
already-friended class keep access to private fused kernel entries (no new
public kernel surface); dispatch is compile-time only — no virtual calls, no
`std::function`, no indirection in any per-ray/per-cell loop; one physical
copy of everything execution-related — adding a family adds one adapter + one
kernel entry + one instantiation, never a solver copy. Rejected: polymorphic
kernel interfaces and `std::function` callbacks (hot-loop dispatch,
user-forbidden); free-function executors (lose friendship, force public
kernel entries).

**Run-mode dimension (re-frozen, §13 item 1 resolved):** the template gains a
second compile-time parameter — a **sink policy**, `CoherentFusedSink` or
`IntensityFusedSink` (closed pair, implicitly instantiated in the same
translation unit). The sink policy owns everything mode-specific at compile
time: the raw workspace type (`FusedPressureWorkspace` complex payload vs
`FusedIntensityWorkspace` double payload, §6), its construction, which
adapter accumulation hook is called (`Adapter::accumulateFused` vs
`Adapter::accumulateFusedIntensity`), and the result type
(`FusedAccumulationResult` vs `FusedIntensityAccumulationResult`). The
executor body is mode-agnostic: run mode is read once per run at the public
entry to select the sink policy; the only run-mode-dependent code inside a
loop is the legacy-verbatim Lloyd-mirror projected-amplitude conditional in
the projection loop (S:207-212, false for C/I, already present in today's
code and now live for semicoherent runs — it is input data computed
identically to legacy `single_frequency_solver.cpp:288-293`, not a control
branch). Rejected: two executor bodies (forbidden duplication across modes);
a variant/union workspace or result (would edit IGR-2 tests, forbidden);
runtime per-ray sink dispatch.

### 3.2 Executor-owned responsibilities (single-sourced)

`accumulateFrequenciesImpl<Adapter, Sink>` owns, verbatim from the current
`accumulateFrequencies` body (`src/solver/fused_ray_reuse_solver.cpp:151-297`):

1. scope + source-cache validation (family gate, §9) and worker-count checks;
2. raw workspace allocation via the sink policy — `FusedPressureWorkspace`
   (complex) or `FusedIntensityWorkspace` (double), single
   `[range][depth][frequency]` field, exactly one workspace per run
   (`fused_pressure_workspace.hpp`, §6);
3. static contiguous range partition: `activeWorkers = min(requested,
   rangeCount)`, quotient/remainder, front workers +1 (S:179-198);
4. worker lifecycle: raw `std::jthread` only when >1; per-worker
   `FrequencyProjector`, `Adapter::Kernel`, `Adapter::PerRayScratch`,
   `frequencyStates` scratch, `RangeWorkerResult` (S:184-192);
5. the per-ray **projection loop** — source pattern amplitude, Lloyd-mirror
   projected amplitude, `projector.project` per frequency (S:200-222);
   family-independent because the single-frequency reference loop
   (`single_frequency_solver.cpp:281-300`) is family-independent;
6. exception protocol: per-worker `exception_ptr`, all joined before rethrow
   of the first (S:249-266);
7. timing join: `project/influence` seconds = max over workers; statistics
   accumulated across workers (S:268-277);
8. cache contract: `sourceCache` shared const via `paths()`, no worker copy;
   fingerprint pre/post under `--verify-cache`;
9. output ownership: `FusedAccumulationResult` /
   `FusedIntensityAccumulationResult` (per sink policy); `solveStreaming`
   owns materialize → scale → consumer per ascending frequency index
   (mode-complete chain, §6.2).

The executor calls exactly four adapter hooks per sink policy (§4); the
intensity sink calls the intensity twins of the accumulation and scale
hooks. Nothing else varies per family or per mode inside the executor.

### 3.3 Entry-point evolution (backward compatible)

- `FusedRayReuseSolver::accumulateFrequencies` and `solveStreaming` keep their
  exact signatures (`fused_ray_reuse_solver.hpp:69-82`). The
  `CartesianCervenySettings` parameter is retained verbatim; consumed only by
  the two Cerveny kernels, ignored by geometric families (their
  single-frequency constructors take no settings — verified at
  `single_frequency_solver.cpp:247-275`). No new settings carrier, no knobs.
- Internal change only: the CC-specific body moves into
  `accumulateFrequenciesImpl<CartesianCervenyFusedAdapter>`; public methods
  dispatch on `beamFamily()`/`cervenyCoordinateSystem()`.
- `supportsFusedRayReuse` remains the single fused-eligibility predicate
  shared by CLI and solver; widened per family task (§9), never bypassed.
- **Added alongside (re-frozen; existing signatures and
  `FusedAccumulationResult` stay untouched for the whole batch):**
  `FusedRayReuseSolver::accumulateFrequenciesIntensity` — same parameter
  list as `accumulateFrequencies`, returns the new
  `FusedIntensityAccumulationResult` (same fields and meanings,
  `rawIntensityWorkspace` typed `FusedIntensityWorkspace`) — the Level-B
  seam for raw double-payload parity; plus
  `FusedIntensityWorkspace::materializeIntensityFrequency` (§6). A
  variant-typed result was rejected: it would edit existing tests.
- `solveStreaming` keeps its exact signature and selects the sink once per
  run from the run mode (Coherent → existing path, textually unchanged;
  I/S → `accumulateFrequenciesIntensity` + the §6.2 scale chain). The
  consumer type is unchanged: in every mode the consumer receives a
  `FrequencyWorkspace` in pressure representation — legacy continuity
  verified at `single_frequency_solver.cpp:356-369`, where I/S intensity is
  converted by `scale{Geometric,Cartesian}IntensityToPressure` before
  delivery.
- IGR-2 tests must stay green without edit through A02 (Levels A-D re-run on
  the migrated CC path prove equivalence); the coherent public surface is
  not edited by any later task either (A02b adds alongside only).

### 3.4 Statistics envelope and timing

`CartesianCervenyStatistics` stays the fused influence-statistics envelope for
this batch (embedded in the solver-layer `SingleFrequencyTimings`, consumed by
the PRT writer). Each family fills the counters its traversal naturally
produces (`rayAccumulations`, `segmentCandidates`, `eligibleSegments`,
`receiverRange/DepthEvaluations`); CC-specific counters (images/window/taper,
IGR-1 splits) and sub-phase seconds remain CC-populated, documented as
zero/absent for other families in A08. No new statistics type; renaming is a
non-goal. Per-ray family prep (Cerveny epsilon) is timed inside the influence
window, exactly where the epsilon loop sits today (S:223-240). Intensity runs
fill the same envelope with the same counter and timing semantics — no
mode-specific counters or timing categories are added; the run mode itself is
reported by the existing PRT run-mode line, which fused runs must reproduce
legacy-exactly (A08).

### 3.5 Cache contract (unchanged)

Frozen `RayPathCache` const-shared; fingerprint FNV-1a before==after under
`--verify-cache` is Level A for every family; no worker-local cache copies; no
frequency-local state ever written back (AGENTS.md §9).

## 4. Kernel adapter interface (frozen)

One adapter struct per family, defined in a src-internal header
(`src/solver/fused_influence_adapters.hpp`, not installed). Shape (narrow,
compile-time, five members):

```cpp
struct <Family>FusedAdapter {
  using Kernel = <Family>Influence;
  struct PerRayScratch {};                 // Cerveny: vector<complex<double>> epsilons
  struct PerRayContext {};                 // Cerveny: widthMode, sourceC0, gradDepth,
                                           //   launchStep, loopRange, epsMultiplier
  static Kernel makeKernel(const SimulationCase&, CartesianCervenySettings);
  static void prepareScratch(PerRayScratch&, std::size_t frequencyCount);
  static void preparePerRay(const PerRayContext&, PerRayScratch&,
                            const RayPath&, std::span<const double> frequencies);
  static bool accumulateFused(const Kernel&, const PerRayScratch&,
      FusedPressureWorkspace&, std::span<const double> frequencies,
      const RayPath&, std::span<const RayFrequencyState> frequencyStates,
      std::size_t rangeBegin, std::size_t rangeEnd,
      CartesianCervenyStatistics* statistics);
  static void scaleFrequency(FrequencyWorkspace&, const ReceiverGrid&,
      double launchAngleStep, double sourceSoundSpeed, SourceGeometry);
  // Intensity twins — defined only by families with legal I/S modes
  // (CC, RC, Hat both coords, GeoGaussian; NOT SimpleGaussian):
  static bool accumulateFusedIntensity(const Kernel&, const PerRayScratch&,
      FusedIntensityWorkspace&, /* same trailing parameter list as above,
      FusedIntensityWorkspace in place of FusedPressureWorkspace */);
  static FrequencyWorkspace scaleIntensityFrequency(
      const IntensityWorkspace&, const ReceiverGrid&, double launchAngleStep,
      double sourceSoundSpeed, SourceGeometry);
};
```

Frozen rules:

- `makeKernel` arguments mirror the single-frequency dispatch **verbatim**
  (`single_frequency_solver.cpp:242-275`): CC — environment, receivers,
  settings, widthMode, sourceGeometry; RCC — plus `runMode()` and
  `fieldComponent()` (the ctor validates the entry kind: coherent entry
  requires Coherent, intensity entry requires I/S,
  `ray_centered_cerveny_influence.cpp:256-277`; V/H coherent branches are
  part of the RC pressure path, `ray_centered_cerveny_influence.cpp:417-435`);
  Hat — receivers, coordinate system, sourceGeometry; GeoGauss — receivers,
  sourceGeometry; SimpleGauss — receivers, `integrator().stepLength`,
  sourceGeometry.
- `preparePerRay` for Cerveny families is the **code-motioned** epsilon loop
  (S:223-231) computing `pickBeamEpsilon` per (ray, frequency) in frequency
  order; for geometric families an empty inline function (compiled away).
  `PerRayContext` is exactly the loop-invariant input set of that loop.
- `accumulateFused` and `accumulateFusedIntensity` are plain inline
  forwarding calls to the kernel's private fused entries (§5) with
  family-exact parameter lists (the intensity twin takes
  `FusedIntensityWorkspace&`); adapters hold no data and no logic beyond
  construction/forwarding.
- `scaleFrequency`/`scaleIntensityFrequency` reproduce the legacy
  post-scale selector (`single_frequency_solver.cpp:356-381`) **exactly per
  family × mode** (verified: the selector is family-based, not
  coordinate-based): CervenyGaussian (CC **and** RC) — coherent →
  `scaleCoherentCartesianPressure`, I/S → `scaleCartesianIntensityToPressure`;
  GeometricHat (both coords), GeometricGaussian, SimpleGaussian — coherent →
  `scaleCoherentGeometricPressure`; Hat/GeoGauss I/S →
  `scaleGeometricIntensityToPressure`. The intensity hooks return the
  converted `FrequencyWorkspace` by value (pressure representation).
- SimpleGaussian defines only the coherent pair of hooks — its legal product
  matrix is coherent-only (§9); the intensity sink is never instantiated
  with its adapter.
- Each kernel class befriends its adapter struct (one forward-declared friend
  line per family header). `accumulateFused` lexically contains the call to
  the kernel's private fused entry, so the adapter needs friendship — CC
  included: CC adds one `friend struct CartesianCervenyFusedAdapter;` line
  (existing friends retained; the friend set is extended, not unchanged).

## 5. Beam kernel boundary (frozen)

Each family implements one private fused accumulation entry **per supported
payload**: "accumulate one ray's contribution for all frequency lanes into
receiver cells [rangeBegin, rangeEnd) of the fused workspace" — a coherent
entry (complex payload) for every family, plus an **intensity twin** (real
payload into `FusedIntensityWorkspace`) for every family with legal I/S modes
(CC, RC, Hat both coords, GeoGaussian). Parameter lists are family-exact (no
dead parameters); each intensity twin's list is its coherent sibling's list
with the workspace type substituted:

- CC (reference): the coherent entry is **unchanged** — existing
  `accumulateFusedPrevalidated(workspace, frequencies, path, states, epsilons,
  rangeBegin, rangeEnd, statistics*)` — `cartesian_cerveny_influence.hpp:162-168`.
  The CC **intensity twin is NEW construction (A02b), not code motion**:
  no fused intensity kernel exists today (the fused CC path is coherent-only;
  legacy CC intensity lives in the single-frequency
  `accumulateIntensityPrevalidated`/shared `accumulateImpl` branches,
  `cartesian_cerveny_influence.cpp:947-973`). It carries Level B gates
  against legacy `reuse` raw intensity like any new kernel — unlike A02's
  pure-motion CC coherent migration.
- RC Cerveny: same shape including the `epsilons` span (epsilon channel
  exists), both entries.
- GeometricHat (per coordinate, internal once-per-ray selection between its
  Cartesian and RC traversal kernels, mirroring the legacy
  `accumulateField`/`accumulateRayCenteredField` split),
  GeometricGaussian: same shape without the epsilons span, both entries.
- SimpleGaussian: coherent entry only (legal matrix coherent-only, §9).

Kernels own: union active-prefix derivation and per-frequency active masks from
`frequencyStates` (the CC-proven pattern, `cartesian_cerveny_influence.cpp:1304-1358`),
all traversal, all science formulas, their frequency-independent/frequency-local
split (§8), partition-aware receiver-run intersection, and their statistics.
Family-private: everything else. No kernel reads executor state; no executor
code appears in kernels.

## 6. Sink boundary and run-mode coverage (frozen)

### 6.1 TL coherent pressure sink

`solveStreaming` per frequency index: `materializeFrequency` (bitwise lane
copy, `fused_pressure_workspace.cpp:45-78`) → `Adapter::scaleFrequency` →
`consumer(fi, workspaces, timings)`. This generalizes today's unconditional
`scaleCoherentCartesianPressure` call (S:361-363); for CC the call is textually
identical.

### 6.2 Run-mode coverage decision — mode-complete C/I/S (user-resolved)

**User decision (verbatim, 2026-09-03; resolves design OPEN #1):**

> "OPEN #1 resolved by user: IGR-3A shall cover all currently legal TL
> Influence accumulation modes, not coherent-only. C/I/S are in fused scope
> for Cerveny, Geometric Hat, and Geometric Gaussian where already
> supported; Simple Gaussian remains coherent-only. This is an
> execution-path extension only and must not expand the scientific
> beam×run-mode support matrix."

Frozen sink mechanism (grounded in the verified legacy chain,
`single_frequency_solver.cpp:216-381`):

- **Parallel real-payload workspace — not a woven-in payload.** A new
  `FusedIntensityWorkspace` (`std::vector<double>` payload, same
  `[range][depth][frequency]` layout `((r*D)+d)*F+f`, same per-run ownership
  and disjoint-worker write discipline, `materializeIntensityFrequency`
  bitwise lane copy into a legacy `IntensityWorkspace`) is selected once per
  run by run mode: Coherent → `FusedPressureWorkspace`; I/S →
  `FusedIntensityWorkspace`. Exactly one raw workspace exists per run.
  Rejected: extending `FusedPressureWorkspace` with a real lane, a union, or
  a variant payload (edits IGR-2 tests / burdens the coherent hot loop with
  no legacy-faithful gain).
- **I/S sink chain** (reproduces `single_frequency_solver.cpp:356-381`
  exactly): per frequency index, `materializeIntensityFrequency` (bitwise
  double-lane copy) → `Adapter::scaleIntensityFrequency` → family-exact
  `scaleCartesianIntensityToPressure` (Cerveny CC and RC) or
  `scaleGeometricIntensityToPressure` (Hat both coords, GeoGaussian),
  returning a `FrequencyWorkspace` → `consumer(fi, FrequencyWorkspace&&,
  timings)`.
- **Consumer API continuity (verified):** legacy delivers a
  pressure-representation `FrequencyWorkspace` in every mode — the I/S
  branch converts intensity to pressure before returning
  (`single_frequency_solver.cpp:359-369`). The fused consumer therefore
  receives the same type in every mode; no consumer or writer change.
- **I/S and C share the traversal.** Every family's legacy accumulation is
  one shared traversal with a workspace-kind branch (CC
  `cartesian_cerveny_influence.cpp:947-973`; RC
  `ray_centered_cerveny_influence.cpp:449-460`; Hat `accumulateField`
  wrappers `geometric_hat_influence.cpp:387-401`, function :404-415, with
  intensity branches :532-549 and the RC variant :730-739; GeoGaussian
  `geometric_gaussian_influence.cpp:
  342-356`). Each fused family kernel reproduces that one traversal with its
  per-lane intensity branch — never a second traversal.
- **Gate semantics:** formerly rejected `fused`+I/S CLI/solver combinations
  becoming runnable IS the intended execution-path extension (behavior
  tested in A07); no legacy `nonreuse`/`reuse`/`parallel` behavior changes;
  fused eligibility remains a subset of the legal support matrix (§9).

## 7. Workspace and range ownership (frozen)

- One raw workspace per run — `FusedPressureWorkspace` (complex) or
  `FusedIntensityWorkspace` (double), selected by run mode (§6.2); cell lanes
  contiguous `((r*D)+d)*F+f`; workers write disjoint `[rangeBegin, rangeEnd)`
  cells only; never both workspaces in one run.
- Per-worker: projector, kernel, family scratch, `frequencyStates`, result
  counters. Shared const: source cache, simulation model. No atomics, no
  mutexes, no per-worker field copies, no dynamic scheduling.
- Per-cell accumulation order within a worker is the serial order (static
  contiguous partition preserves each cell's stream — IGR-2 accepted property).

## 8. Numerical encounter-order contracts (frozen per family)

Universal contract: ray order = frozen cache order; the sequence of additions
into any single (cell, frequency lane) is **identical to legacy reuse** for
that family. Fused execution may interleave which lane receives a
contribution, never reorder within a lane. Each kernel derives the union
active-prefix (max over frequencies' active prefixes) and per-frequency masks
from `frequencyStates`; prefix/mask gates reproduce the per-frequency loop
bound and left-endpoint conditions exactly (CC pattern,
`cartesian_cerveny_influence.cpp:1344-1358`).

**Intensity contracts (re-frozen, §6.2):** for every family, the intensity
twin is the same traversal as its coherent sibling; the sequence of real
additions into any single (cell, frequency lane) is **identical to legacy
reuse intensity accumulation** — real additions are order-sensitive, no
reassociation, no per-lane reordering. Union active-prefix and per-frequency
masks are identical to the coherent kernel of that family. Per-family
intensity formulas (verified in source):

- **CC:** per beam, the coherent image sum is formed first, then intensity =
  ABS(contribution)² computed as `std::abs` then multiply — `std::norm` is
  forbidden (different rounding/overflow path,
  `cartesian_cerveny_influence.cpp:960-972`).
- **RC:** intensity = taper · ABS(contribution)², the contribution including
  the V/H factors and kmah/surface signs
  (`ray_centered_cerveny_influence.cpp:446-459`). The per-frequency-lane
  persistent flip-parity contract below applies **unchanged** to the
  intensity twin: flips alter projectedRange → receiver coverage → which
  cells receive intensity increments.
- **GeometricHat (both coords):** attenuatedConstant = amplitudeConstant ·
  exp((ω·delay).imag()); intensity = attenuatedConstant² · hatWeight — the
  hat weight is applied exactly once; this is NOT ABS(coherent)²
  (`geometric_hat_influence.cpp:532-549`, RC variant :730-739).
- **GeometricGaussian:** attenuatedConstant as above; intensity = √(2π) ·
  attenuatedConstant² · gaussianWeight (`geometric_gaussian_influence.cpp:
  342-356`).

**Incoherent vs semicoherent — verified distinction, frozen:** no family
kernel branches on run mode anywhere (RC's ctor `runMode_` is entry-kind
validation only). I and S execute bit-identical accumulation code; the ONLY
difference in the legacy chain is the Lloyd-mirror projected source
amplitude applied for S in the projection layer
(`single_frequency_solver.cpp:288-293`; factor
base·√2·|sin((ω/c₀)·z_s·sinθ)| per (ray, frequency), `usesLloydMirror`
`simulation_case.cpp:170`). The fused projection loop already carries this
conditional per frequency (S:207-212); it becomes live for semicoherent
runs. The delay-attenuation factors above apply for BOTH I and S — they are
not an I-vs-S distinction. Both modes are reproduced family-exactly and
gated separately (Level B/D per mode).

- **CC (reference, unchanged):** segment→range→depth→image, innermost
  frequency; images `1..imageCount`.
- **RC Cerveny:** depth→image(1..imageCount)→segment→ascending receiver run
  between projected indices. Frequency-independent per ray: normals
  `n=(t_z,−t_x)` from `t=c·slowness`, projection weights, source ratio.
  **Persistent normal-flip parity is per-frequency-lane** (design-review
  correction): legacy `imageNormals` is initialized once before the depth
  loop, so flips persist across depths AND images
  (`ray_centered_cerveny_influence.cpp:317-319`); evolution order is
  (depth, image, step). The step-loop bound `rightIndex < activePointCount`
  (line 335) derives from the per-frequency active prefix (lines 299-305,
  projector amplitude threshold 0.005F) — frequency-LOCAL; each non-True-image
  accepted step negates the whole array's range component (348-352), with the
  geometric `|n.depth|<ε` test (340-343) as the only pre-flip gate. Flip
  parity at (depth d, image i, step s) for frequency f therefore equals
  `(d·(I−1)+i−1)·g(P_f)+g(s) mod 2` (g = geometric gate passes; P_f = that
  frequency's active prefix): parity differs across depths and across
  frequencies whenever the g(P_f) parities differ (any imageCount ≥ 2), and
  it enters projectedRange via `endpointNormal.range` (363-364) — receiver
  coverage and interpolation weights, i.e. output bits. The fused kernel
  MUST reproduce **per-lane legacy-exact flip evolution**: each frequency
  lane maintains its own persistent flip state, evolved in the legacy
  (depth, image, step) order and bounded by that lane's own active prefix.
  The parity is a cheap one-bit-per-lane quantity; computing it as a
  closed-form parity vs an evolved per-lane state is a kernel implementation
  choice — the semantic requirement is per-lane legacy-exact evolution.
  Frequency-local: q/γ/kmah from the epsilon channel; ω; `radiusMax=30c0/f`;
  window test; taper; τ/reflectionPhase/amplitude; V/H factors. Level B
  proves it.
- **GeometricHat, Cartesian:** segment→range window (monotone advancing
  cursor, direction from slowness.range sign)→depths ascending. Hat width
  `|q/q0|` is frequency-independent → receiver window, beamRadius, hatWeight
  shared. Frequency-local: delay, amplitude (`scaledAmplitudes` per f), phase
  (reflectionPhase + caustic π/2), active masks. Cursor receiver runs
  intersected with `[rangeBegin,rangeEnd)`.
- **GeometricHat, Ray-Centered:** depth-major depth→segment→receiver runs
  (ascending forward / descending reversed), via a **new fused traversal
  replicating `forEachRayCenteredEvaluation`**
  (`geometric_hat_influence.cpp:184-328`) over the union prefix; the legacy
  helper itself is untouched (E shares it — §10). Per-depth persistent
  caustic phase and previous-Q state are frequency-independent (dynamicQ),
  evolved once; frequency-local: scaledAmplitudes, delay, `phaseAtReceiver`
  (per-f reflectionPhase), active masks (loop bound `activeCount(state)` is
  frequency-local → union prefix).
- **GeometricGaussian:** segment→receiver-range window→depths. Width σ1 is
  frequency-local (`wavelengthSigma=π·c/f`, `nearFieldSigma=0.2·f·Re(τ_right)`)
  → **receiver eligibility windows and depth prefilters are evaluated per
  frequency exactly as legacy; eligibility is NOT lifted to
  frequency-independent geometry** (frozen). Shared per segment:
  tangent/normal, q interpolation inputs, geometricSigma, q0, caustic
  accumulation, source ratio. Per-cell per-frequency order = segment then
  depth ascending, identical to legacy.
- **SimpleGaussian:** segment→monotone range cursor→shared depth rows
  (rectilinear semantics retained; no irregular support added). Width
  frequency-independent (`gaussianA` from Δθ). Frequency-local: ω·delay
  phase, amplitude, active prefix/masks. Coherent only.

## 9. Family-by-family fused eligibility (frozen)

Common gate (all families): TL mode, run mode legal for the family
(SimpleGaussian requires Coherent — its product gate,
`single_frequency_solver.cpp:224-227` — is legal-matrix enforcement, not a
fused restriction), exactly 1 source, ≥2 frequencies, regular receiver grid,
≥2 equally-spaced ranges (existing checks, S:50-88 — the `NotCoherent` arm
at S:53-55 becomes family-legality and its message is replaced in the family
tasks/A07). Fused stays regular-grid-only for every family — including Hat-C
and GeoGaussian, which support irregular grids in legacy mode; the fused
predicate simply does not offer fused there (no support-matrix change).

| Family (token)        | Coords      | Product run modes | IGR-3A fused | Epsilon channel | Family-specific gate notes |
|-----------------------|-------------|-------------------|--------------|-----------------|----------------------------|
| CervenyGaussian (C)   | Cartesian   | C/I/S             | C+I+S (reference: C=A02, I/S=A02b) | yes | unchanged |
| CervenyGaussian (R)   | RayCentered | C/I/S             | C+I+S        | yes | ctor uniform-range validation retained |
| GeometricHat (G/g)    | Cartesian + RayCentered | C/I/S | C+I+S, both coords | no | RC variant ctor rejects irregular (retained) |
| GeometricGaussian (B) | Cartesian   | C/I/S             | C+I+S        | no | — |
| SimpleGaussian (CS)   | n/a         | C only (product)  | C only — its ONLY legal mode | no | point source + positive step via existing ctor validation |

Fused run-mode coverage equals the family's legal product run modes —
never more: **fused eligibility is always a subset of the legal
beam×run-mode support matrix** (user decision: execution-path extension
only). Formerly rejected `fused`+I/S CLI/solver combinations becoming
runnable IS the intended extension (behavior-change tests in A07); no
legacy mode behavior changes anywhere.

Gate enum/messages generalize family/coordinate/mode-legality failures;
observable error strings are updated with their component tests in the same
task (A02b, A03-A07).

## 10. Regression boundary (frozen)

- **R:** `ray_trace_product.cpp` uses only `GeometryTracer`+`RayWriter`; no
  IGR-3A edit may touch the tracer or ray writer; `.ray` byte identity gates
  stay.
- **E:** `GeometricHatInfluence`/`GeometricGaussianInfluence` public surfaces
  (`accumulate`, `accumulateIntensity`, `accumulateArrivals`,
  `collectEigenrayHits`) and shared helpers (`forEachRayCenteredEvaluation`,
  `prefixBounceCounts`, anonymous traversal/eligibility helpers) are
  behavior-frozen; fused entries/traversals are **additive** new code. No
  shared-helper refactor unless mechanical and proven by E gates: eigenray
  hit-identity tests, `validate_i8_eigenrays.py`,
  `multi_source_writer_test.cpp` `.ray` byte identity.
- **Legacy execution paths** (`nonreuse`/`reuse`/`parallel`) unchanged; gates:
  existing ctest suite, model_matrix byte-identity gate, `make
  f2cpp-regression`, standard-case three-party products. The six existing
  I/S standard cases (§11 Level E) enter three-party product regression;
  their legacy products are the frozen reference.
- **Cache:** §3.5; Level A per family.

## 11. Construction sequence, gates, performance protocol

Sequence: design-review PASS → A01 interface freeze → A02 CC coherent
migration (bit-identity + perf proof, pure motion) → A02b CC fused I/S (new
construction) → A03 RC Cerveny → A04 GeoHat (both coords) → A05
GeoGaussian → A06 SimpleGaussian → A07 CLI/routing/support-matrix → A08
PRT/stats/docs → A09 full regression + benchmark → Batch Acceptance → Final
Review. One family per task; each family task passes Levels A-E for **every
fused mode of that family — I and S gated separately, both bitwise** —
before review; Level F evidence is consolidated in A09 (plus the A02 CC perf
proof).

Acceptance gates (defined once, referenced by the worklist):

- **Level A — cache contract:** `--verify-cache` fingerprint before==after.
- **Level B — raw bitwise parity, per mode:** coherent — fused raw workspace
  materialized per frequency bit-identical (`memcmp`, complex payload) to
  legacy `reuse` raw for that family; I/S — fused raw intensity lane per
  frequency bit-identical (`memcmp`, double payload) to the legacy `reuse`
  raw `IntensityWorkspace` for that family and mode. I and S are gated
  separately (they differ by the Lloyd-mirror amplitude, §8).
- **Level C — scaled/product parity:** scaled workspaces identical (I/S via
  the §6.2 intensity→pressure chain); SHD product bytes/hash identical to
  legacy `reuse` output.
- **Level D — partition invariance, per mode:** fused outputs identical
  across `--fused-range-workers 1/2/4/8` (serial fused == workers=1), for
  each fused mode of the family.
- **Level E — product/oracle regression:** family-targeted standard-case
  three-party products — coherent set as before, plus the six existing I/S
  standard cases (`incoherent_direct`, `semicoherent_direct`,
  `geometric_hat_incoherent`, `geometric_hat_semicoherent`,
  `geometric_gaussian_incoherent`, `geometric_gaussian_semicoherent`; their
  fused-eligible profile is `broadband_smoke` 2F — the `single` 1F profile
  is below the fused ≥2F bound and stays legacy-only). No RC-Cerveny or
  Hat-RC I/S standard case exists; those family tasks additionally evidence
  targeted fused-vs-legacy-`reuse` SHD identity for the missing coordinate×
  mode combinations and record the gap. E-boundary gates (§10) for geometric
  families; component + solver tests updated with any observable behavior
  change.
- **Level F — performance:** protocol below.

Gate mapping to the user's gate list: Level C folds user gates C+D (scaled
bitwise + SHD byte/SHA-256 identity); Level D = user gate E (workers 1/2/4/8
bitwise identity); Levels E+F cover user gate F (legacy
single-frequency/nonreuse/reuse/parallel unchanged) plus the performance
protocol — nothing from the user's gate list is dropped.

**STOP rule (user-frozen):** if any family's floating-order structure proves
unable to reach the bitwise gate, construction STOPS — architect +
advanced-worker perform an order re-audit. Downgrading to tolerance-based
acceptance is forbidden.

Performance protocol (A09; A02 subset for CC): five families (six kernel
configurations counting both hat coordinates) coherent **plus the six I/S
benchmark configurations** (CC I/S via `incoherent_direct`/
`semicoherent_direct`, Hat-Cartesian I/S via `geometric_hat_incoherent`/
`geometric_hat_semicoherent`, GeoGaussian I/S via
`geometric_gaussian_incoherent`/`geometric_gaussian_semicoherent`) × workers
{1,2,4,8}; one warmup + ≥3 measured runs; median/min/max/MAD of wall, Trace,
Project, Influence, Scale seconds and peak RSS (`ru_maxrss`); git HEAD +
binary hash recorded; `require_identical_sample_hashes` across worker counts.
The six I/S benchmark cases currently lack a 16F-class broadband profile
(`single` 1F and `broadband_smoke` 2F only) — a `broadband_regression`
16F-class profile per case is added once as shared-test infra in A09 before
the benchmark run (50–500 Hz ×16 for the two `*_direct` cases, matching
`munk_cerveny_cc`; 1000–2500 Hz ×16 for the four 1000-Hz-class cases);
substitution only with recorded justification. A02 additionally
proves CC wall/phase times match the pre-refactor baseline binary (built from
HEAD `38137a4`) within noise — performance magnitude preserved, not just
function.

## 12. IGR-3B frozen boundary (preliminary — no construction in IGR-3A)

- **Broadband arrival lane:** logical layout `[range][depth][frequency]` —
  per-cell per-frequency arrival slots; AddArr semantics operate within one
  (range, depth, f) cell exactly as the legacy per-frequency
  `ArrivalWorkspace` (`arrival_workspace.hpp:31-32`).
- **AddArr primitive extraction:** one shared implementation of the
  candidate/merge/cusp-guard/weakest-replacement/capacity semantics and
  counters, used by both legacy `ArrivalWorkspace` and the fused lanes;
  legacy `A/a` output byte-identical (gates:
  `validate_i8_arrival_accumulator.py` probe, `validate_i8_arrivals.py`
  ULP≤8 ordered, broadband one-file-per-freq, no `.tmp` leftovers).
- **Ownership:** per-worker lane ownership under the same static contiguous
  range partition — one worker owns `[rangeBegin,rangeEnd)` × all depths ×
  all frequencies; no atomics.
- **Writer lifecycle (direction frozen, names not):** source-streamed
  per-frequency writer — one writer per frequency, header first,
  `appendSource(sourceIndex, frequencyView)` in source order, finalize,
  transactional `.tmp`→rename publication preserved, per-frequency file
  naming `<root>_f%03d_<token>Hz.arr` preserved.
- **frequencyView:** zero/minimal-copy view of one frequency's lane slice.
- **Eligibility:** `B`/`G`/`g` × `A/a` exactly (matches the existing legal
  arrival beam set); a fused-arrival gate separate from the TL gate.
- **Non-goals preserved:** no new beam families, no formula changes, no
  Origin/F2CPP edits, no support-matrix changes.

Open questions for the IGR-3B design (recorded, NOT decided here): lane
metadata sizing (per-cell capacity representation); pooled vs per-cell
storage allocation; RSS model and peak-memory budget; per-worker statistics
merge under partitioning; publication ordering under streaming. IGR-3B starts
only after IGR-3A is ACCEPTED and committed.

## 13. Open questions for the design reviewer

1. **Fused I/S intensity — RESOLVED by user decision (2026-09-03,
   verbatim):**

   > "OPEN #1 resolved by user: IGR-3A shall cover all currently legal TL
   > Influence accumulation modes, not coherent-only. C/I/S are in fused
   > scope for Cerveny, Geometric Hat, and Geometric Gaussian where already
   > supported; Simple Gaussian remains coherent-only. This is an
   > execution-path extension only and must not expand the scientific
   > beam×run-mode support matrix."

   §6.2 (sink mechanism), §3 (sink policies + public surface), §4/§5 (dual
   kernel/adapter entries), §8 (intensity + I-vs-S contracts), §9 (family
   table), §11 (per-mode gates, Level F extension), and the IGR-3A worklist
   are re-frozen accordingly.
2. **Benchmark case set per family** (Level F): **resolved — frozen now, not
   deferred to A09.** Six existing 16F-class broadband cases in
   `test/standard_cases/cases/`: `munk_cerveny_cc` (CC reference),
   `ray_centered_component_pressure` (RC Cerveny), `geometric_hat_cartesian`
   (Hat Cartesian), `geometric_hat_ray_centered` (Hat Ray-Centered),
   `geometric_gaussian_cartesian` (Geometric Gaussian),
   `simple_gaussian_cartesian` (Simple Gaussian). Substitution only with
   recorded justification (worklist §4).
3. **Deprecation messaging**: whether fused availability warnings for I/S
   modes should name `reuse` explicitly as the alternate (A07 detail, no
   architectural impact).

## 14. Non-goals (preserved from scope authority)

Frequency interpolation; BARR; GPU; fast-math/reassociation/SIMD; dynamic
scheduling/work stealing/atomics; frequency-parallel redesign; new beam
families; formula/physics changes; support-matrix changes; persistent
influence geometry cache; Origin/F2CPP production edits; multi-source fused;
irregular-receiver fused; IGR-3B construction.
