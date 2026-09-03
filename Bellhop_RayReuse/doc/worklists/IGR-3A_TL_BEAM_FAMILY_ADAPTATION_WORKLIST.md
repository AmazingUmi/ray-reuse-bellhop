# IGR-3A — Remaining TL Beam Family Adaptation — WORKLIST

> **Status:** PRE-CONSTRUCTION (construction authority only after independent
> design-review PASS on the design document)
> **Branch:** `feat/igr-influence-geometry-reuse`
> **Baseline:** HEAD `38137a4`, clean tree
> **Date frozen:** 2026-09-03
> **Re-frozen:** 2026-09-03 — design OPEN #1 resolved by user decision
> (design §13 item 1): mode-complete C/I/S fused coverage. Objective, A01,
> A02b (new), A03-A05, A07-A09, and §3/§4 updated; A02/A06 scope unchanged
> (wording clarified: no intensity wiring in A02; A06 coherent-only is the
> legal matrix).
> **Authority:** [`IGR-3_SCOPE_AND_ARCHITECTURE_DECISION.md`](IGR-3_SCOPE_AND_ARCHITECTURE_DECISION.md)
> (user-frozen scope) and
> [`DESIGN_IGR3_UNIFIED_INFLUENCE_ARCHITECTURE.md`](DESIGN_IGR3_UNIFIED_INFLUENCE_ARCHITECTURE.md)
> (frozen architecture). Construction order is strictly serial; each task's
> reviewer PASS (Levels A-E where applicable) gates the next task.

## 1. Batch objective

Adapt the four remaining TL beam families (Ray-Centered Cerveny, Geometric Hat
Cartesian + Ray-Centered, Geometric Gaussian, Simple Gaussian) onto the
unified fused executor with static range parallelism, covering **all legal TL
accumulation modes per family** (user-resolved design §6.2/§13 item 1):
coherent for all five families plus incoherent/semi-coherent for Cerveny
(both coords), Geometric Hat (both coords), and Geometric Gaussian;
Simple Gaussian stays coherent-only (its legal product matrix). Cartesian
Cerveny remains the bit-identical reference (A02 coherent motion; A02b new
I/S kernel). No IGR-3B construction in this batch.

## 2. Tasks

### A01 [ADVANCED] Freeze unified executor interface
Status: DONE
Reviewer: PASS (2026-09-03; independent rebuild + 43/43 ctest + design
§3/§4/§6.2 conformance; reviewer-Flash unavailable in environment, checkpoint
performed by final-reviewer as read-only fallback)

Acceptance:

- Introduce `accumulateFrequenciesImpl<Adapter, Sink>` as a private static
  member template of `FusedRayReuseSolver` (design §3.1) and the src-internal
  `src/solver/fused_influence_adapters.hpp` with the adapter shape of design
  §4 — including the intensity twins (`accumulateFusedIntensity`,
  `scaleIntensityFrequency`; SimpleGaussian adapter coherent pair only);
  per-kernel friend declarations added.
- Interface freeze includes the mode-complete sink surface (design §3.3,
  §6.2): sink policy pair `CoherentFusedSink`/`IntensityFusedSink`,
  `FusedIntensityWorkspace` (double payload, same `[R][D][F]` layout,
  `materializeIntensityFrequency`), `FusedIntensityAccumulationResult`, and
  `accumulateFrequenciesIntensity` (existing coherent signatures and
  `FusedAccumulationResult` untouched).
- Dispatch behavior unchanged in this task (CC still routed through the
  existing body; template + `CartesianCervenyFusedAdapter` + sink policies
  compile, unused).
- No gate, message, CLI, or numerical change. `git diff --check` clean.
- Reviewer confirms interface conformance to design §3-§6 (checkpoint review).

Evidence:

- Files added: `src/solver/fused_influence_adapters.hpp` (CC adapter with
  the five §4 hooks + intensity twins; `CoherentFusedSink` /
  `IntensityFusedSink` policy structs owning workspace construction,
  accumulation-hook selection, and result assembly),
  `include/rayreuse/field/fused_intensity_workspace.hpp` +
  `src/field/fused_intensity_workspace.cpp` (double payload,
  `((r*D)+d)*F` cell offset, overflow-checked allocation, `cell()` spans,
  `materializeIntensityFrequency` bitwise lane copy into the legacy
  depth-major `IntensityWorkspace` layout `d*R+r`, written via a new
  `friend class FusedIntensityWorkspace` in `frequency_workspace.hpp` so the
  copy is not routed through `add()`'s 0.0+x path).
- Files modified (additive only, +135/-0 in tracked files):
  `fused_ray_reuse_solver.hpp` (`FusedIntensityAccumulationResult`,
  public `accumulateFrequenciesIntensity`, private template declaration),
  `fused_ray_reuse_solver.cpp` (template definition: validation +
  unreachable target-shape hook sequence + explicit "not constructed until
  A02" throw; intensity entry with instantiation seams),
  `cartesian_cerveny_influence.hpp` (+`friend struct
  CartesianCervenyFusedAdapter;`, existing friends retained), `CMakeLists.txt`
  (+1 source). CC intensity hooks throw
  `ValidationError("Cartesian Cerveny fused intensity kernel is not
  implemented (IGR-3A A02b)")`; the NotCoherent scope gate is untouched.
- Compile check without dispatch change: `accumulateFrequenciesIntensity`
  calls `<CC, IntensityFusedSink>` (runtime path only reaches the unchanged
  scope validation) and takes the address of `<CC, CoherentFusedSink>` as a
  discarded value; `nm` on the object file shows the intensity
  specialization emitted, and a `-femit-all-decls` probe confirms both
  specializations instantiated (the coherent one is dead-stripped at -O2).
- Build/tests: `cmake --preset release && cmake --build --preset release -j`
  exit 0 (RAYREUSE_WARNINGS_AS_ERRORS=ON, zero warnings);
  `ctest --test-dir build/release` → 43/43 passed, all unedited
  (incl. `rayreuse.component.fused_solver`, `rayreuse.component.fused_cc_parity`,
  `rayreuse.unit.command_line`). `git diff --check` clean; tracked diff is
  purely additive; reference implementations untouched.
- Runtime rejection parity (throwaway probe, not committed): I/S request to
  `accumulateFrequenciesIntensity` throws the byte-identical existing
  message ("requires the coherent run mode; ..."); coherent request into the
  intensity entry throws "fused unified executor is not constructed until
  IGR-3A A02" (no silent unaccumulated result); existing coherent
  `accumulateFrequencies` still runs.
- `bash scripts/check_format.sh` is NOT green — but it is not green at clean
  HEAD `38137a4` either in this environment: a `git worktree` probe of HEAD
  shows 293 violations under every locally available formatter
  (Apple clang-format 21; pip clang-format 14/15/16/17/18/19/20/23), all in
  untouched files (e.g. `fused_cc_parity_test.cpp`, 91). The committed style
  was produced by a formatter binary not present here; the gate presumably
  needs the house `CLANG_FORMAT` pinned by the user. My new code mirrors the
  committed sibling style (verified: `fused_intensity_workspace.hpp` has
  zero disagreements under clang-format 20.1.8, and the .cpp shows only the
  same signature-wrapping disagreements as the untouched pressure twin);
  my-tree total is 352 vs HEAD 293 under 20.1.8, delta confined to new
  files. Finding routed to coordinator: format-gate environment drift.

### A02 [ADVANCED] Cartesian Cerveny onto the unified executor
Status: DONE
Reviewer: PASS (2026-09-03; independent line-by-line motion equivalence vs
`git show 38137a4` body, single-implementation grep, I/S unreachability at
every entry, self-run ctest 43/43, perf-evidence consistency; reviewer-Flash
unavailable in environment, checkpoint performed by final-reviewer as
read-only fallback)

Acceptance:

- Code motion only: worker loop body, partition math, join/rethrow, timing
  join move verbatim into `accumulateFrequenciesImpl`; CC-specific pieces
  become `CartesianCervenyFusedAdapter::makeKernel` (ctor verbatim),
  `preparePerRay` (epsilon loop verbatim, S:223-231), `accumulateFused`
  (forwarding call to the unchanged `accumulateFusedPrevalidated`),
  `scaleFrequency` (same `scaleCoherentCartesianPressure` call).
- Delete the CC-specialized executor body; public signatures and the scope
  gate unchanged; `CC fused kernel entry untouched`.
- No intensity wiring in A02 — the intensity sink stays unused and the CC
  intensity kernel lands in A02b; A02's diff touches only the coherent path.
  IGR-2 tests stay green unedited.
- Levels A-D re-run on migrated CC: fingerprint, raw memcmp, scaled/SHD
  identity, workers 1/2/4/8 identity — all IGR-2 fused tests green unedited.
- Performance magnitude proof: benchmark fused CC (16F munk case, workers
  1/2/4/8) vs baseline binary built from HEAD `38137a4`; wall/Trace/Project/
  Influence/Scale/RSS within noise (documented threshold, e.g. ≤5% wall).

Evidence:

- Motion (src-internal, all in `fused_ray_reuse_solver.cpp` + a header
  comment update): the A01 target-shape skeleton in
  `accumulateFrequenciesImpl<Adapter, Sink>` was replaced with the motioned
  CC executor body; `accumulateFrequencies` and
  `accumulateFrequenciesIntensity` are now thin dispatchers to
  `<CartesianCervenyFusedAdapter, CoherentFusedSink>` /
  `<CartesianCervenyFusedAdapter, IntensityFusedSink>` (the A01
  address-of-instantiation seam removed — production dispatch now
  instantiates both); `solveStreaming`'s scale phase calls
  `CartesianCervenyFusedAdapter::scaleFrequency` (same arguments, order
  unchanged; design §6.1 "for CC the call is textually identical"). Two now
  unused includes dropped from the .cpp. Programmatic marker diff of the
  HEAD `38137a4` executor body vs the template body: every executable line
  verbatim and in order; only the five frozen adapter/sink substitution
  sites, one adapted workspace comment, and the added `PerRayContext`
  construction differ.
- Single implementation (grep, `src/solver/fused_ray_reuse_solver.cpp`):
  `rangeCount / activeWorkerCount` ×1, `std::vector<std::jthread>` ×1,
  `for (const std::exception_ptr& workerError` ×1, `runWorker` lambda ×1;
  `accumulateFusedPrevalidated` referenced only by the adapter forward and
  its own definition (`cartesian_cerveny_influence.cpp:1133`, untouched).
- Build/tests: `cmake --preset release && cmake --build --preset release -j`
  exit 0 (RAYREUSE_WARNINGS_AS_ERRORS=ON); `ctest --test-dir build/release`
  → 43/43 passed, all unedited (incl. `rayreuse.component.fused_cc_parity`
  Levels A/B/C + workers 1/2/4/8, `rayreuse.component.fused_solver`,
  `rayreuse.unit.command_line`).
- SHD bitwise identity, munk_cerveny_cc `broadband_regression` (16F
  50–500 Hz; coordinator-run benchmark sessions with the harness
  cross-config check `require_identical_sample_hashes` +
  `common_shd_sha256`, covering reuse + fused w1/w2/w4/w8 for BOTH the
  candidate and the 38137a4 baseline binaries, PLUS 16 interleaved direct
  binary runs): ALL
  sha256 `f01ee48119549a82e79798322bf5227d8fc95054be82de955de5ccadef057c2c`.
- Performance (coordinator-run, authoritative record:
  [`IGR3A_A02_PERF.md`](IGR3A_A02_PERF.md); Apple M4, 10 cores, 24 GiB;
  `benchmark_rayreuse.py --modes reuse,fused --fused-range-workers 1,2,4,8
  --repeats 3 --warmups 1` per binary, sequential sessions, PLUS an
  interleaved A/B session-bias control for w1/w8): baseline exe sha256
  `5b802a8cc75819d5bc0434f6c14510df4fc4c30d41fe3e865eb29145d81a2ebb`
  (git worktree at `38137a4`, retained at
  `/private/tmp/igr3a_a02_baseline` for A09 reuse), candidate exe sha256
  `900b09f026fd1a9baf9eb9a4015de464debd2ab7b16292d97147b0fd45c41034`.
  Separated-session median wall deltas: w1 +0.14%, w2 +0.21%, w4 −0.13%,
  w8 +1.41%, reuse +0.06%; RSS ≤ +0.13%. Interleaved A/B (stronger
  protocol): w1 +0.10%, w8 +1.24%, ranges fully overlapping at both.
  Worst median wall delta +1.41% vs the documented ≤5% threshold — no
  measurable overhead. Gate PASS. (An earlier worker-run fused-only
  comparison reporting ≤+0.31% was superseded: its baseline artifact was
  overwritten by the coordinator's full-protocol reruns.)
- `git diff --check` clean; no test files touched; reference
  implementations untouched.

### A02b [ADVANCED] Cartesian Cerveny fused incoherent/semi-coherent
Status: TODO
Reviewer: N/A

Acceptance:

- **NEW construction (not code motion; design §5):** private fused
  intensity entry + adapter intensity hook adding real payload lanes into
  `FusedIntensityWorkspace`. Per lane: the coherent image sum is formed
  first, then intensity = ABS(contribution)² via abs-then-multiply
  (`std::norm` forbidden, design §8); traversal, union prefix, per-frequency
  masks, and encounter order identical to the fused coherent kernel.
- Public surface added alongside only (design §3.3):
  `accumulateFrequenciesIntensity` → `FusedIntensityAccumulationResult`;
  `solveStreaming` per-run mode dispatch; consumer receives
  `FrequencyWorkspace` (pressure representation) in every mode.
- Gate: CC fused eligibility extended to I/S (`supportsFusedRayReuse`;
  family-legality per design §9 — SG non-coherent remains product-rejected);
  messages + component tests updated.
- Levels A-E for BOTH modes, gated separately: A fingerprint; B per-frequency
  double-payload memcmp of fused raw intensity vs legacy `reuse` raw
  `IntensityWorkspace` (I and S each); C scaled/SHD identity via
  `scaleCartesianIntensityToPressure` then parity with reuse; D workers
  1/2/4/8 per mode; E `incoherent_direct`/`semicoherent_direct` standard
  cases (`broadband_smoke` 2F profile) three-party + targeted
  fused-vs-reuse runs.
- Own reviewer checkpoint (distinct from A02's pure-motion review: this task
  carries Level B evidence obligations of a new kernel).

Evidence:
- (placeholders)

### A03 [ADVANCED] Ray-Centered Cerveny fused
Status: TODO
Reviewer: N/A

Acceptance:

- Kernel: private fused entries (epsilons span, design §5) — coherent AND
  intensity twins — reproducing the depth→image→segment→receiver order;
  frequency-independent normals; **per-frequency-lane persistent flip
  parity** — each frequency lane evolves its own flip state in the legacy
  (depth, image, step) order, bounded by that lane's own active prefix
  (design §8; applies unchanged to the intensity twin: flips alter receiver
  coverage and thus which cells receive intensity increments); per-f
  q/gamma/kmah/ω/radiusMax/window/taper/V-H factors; intensity =
  taper · ABS(contribution)² (design §8).
- Adapter + dispatch wiring (implicit instantiation via the public entries,
  design §3.1, both sinks); gate widened for CervenyGaussian + RayCentered
  to C+I+S (family legality, design §9); messages + component tests
  updated.
- Levels A-E for every fused mode (C, I, S gated separately, all bitwise;
  Level B memcmp vs reuse raw per frequency — complex payload for C, double
  payload for I/S; D across workers 1/2/4/8 per mode). Level B cases MUST
  include frequencies with DIFFERING active prefixes so flip-parity
  divergence across lanes is exercised — the A03-specific risk unique to RC
  (the CC fused kernel's per-frequency active masks do not cover it) — for
  the coherent AND intensity payload.
- No RC-Cerveny I/S standard case exists: Level E for I/S = targeted
  fused-vs-legacy-`reuse` SHD identity runs (recorded evidence) + the gap
  noted (design §11 Level E); coherent Level E uses
  `ray_centered_component_pressure`.

Evidence:
- (placeholders)

### A04 [ADVANCED] Geometric Hat fused, both coordinate systems
Status: TODO
Reviewer: N/A

Acceptance:

- Cartesian kernel: segment→range-cursor→depth fused traversal; shared
  freq-independent window/beamRadius/hatWeight; per-f delay/amplitude/phase;
  cursor runs intersected with [rangeBegin,rangeEnd).
- RC kernel: new fused traversal replicating `forEachRayCenteredEvaluation`
  order over union prefix; per-depth caustic phase/previousQ evolved once;
  legacy helper untouched (design §8, §10).
- Both kernels carry coherent AND intensity twins (design §5): intensity =
  (amplitudeConstant · exp((ω·delay).imag()))² · hatWeight, hat weight
  applied exactly once, NOT ABS(coherent)² (design §8; verified
  `geometric_hat_influence.cpp:532-549`, RC :726-733).
- Gate widened for GeometricHat both coords to C+I+S; Levels A-E for every
  fused mode (C, I, S gated separately, all bitwise) incl. E-boundary gates
  (eigenray hit identity, `validate_i8_eigenrays.py`, `.ray` byte identity
  via multi-source test). Level E I/S: `geometric_hat_incoherent`/
  `geometric_hat_semicoherent` cases (`broadband_smoke` 2F); no Hat-RC I/S
  standard case exists — targeted fused-vs-reuse SHD identity for Hat-RC
  I/S with the gap recorded (design §11 Level E).

Evidence:
- (placeholders)

### A05 [ADVANCED] Geometric Gaussian fused
Status: TODO
Reviewer: N/A

Acceptance:

- Kernel: per-segment shared geometry; per-frequency σ1 windows/prefilters
  evaluated exactly as legacy — eligibility NOT lifted (design §8).
- Coherent AND intensity twins (design §5): intensity = √(2π) ·
  (amplitudeConstant · exp((ω·delay).imag()))² · gaussianWeight (design §8;
  verified `geometric_gaussian_influence.cpp:342-356`).
- Gate widened to C+I+S; Levels A-E for every fused mode (C, I, S gated
  separately, all bitwise) incl. E-boundary gates (shared surfaces with E).
  Level E I/S: `geometric_gaussian_incoherent` /
  `geometric_gaussian_semicoherent` cases (`broadband_smoke` 2F).

Evidence:
- (placeholders)

### A06 [ADVANCED] Simple Gaussian fused
Status: TODO
Reviewer: N/A

Acceptance:

- Kernel: segment→range cursor→depth rows; frequency-independent width;
  per-f phase/amplitude/masks; coherent only — SimpleGaussian's ONLY legal
  product mode (design §9), not a fused restriction; existing ctor
  point-source and step validations surface unchanged; no irregular support
  added.
- Gate widened; Levels A-E.

Evidence:
- (placeholders)

### A07 [STANDARD] CLI, routing, and support matrix
Status: TODO
Reviewer: N/A

Acceptance:

- `supportsFusedRayReuse` remains the single predicate driving CLI
  compatibility warnings and solver validation; fused rejected for non-TL
  products unchanged (`main.cpp` routing).
- **Behavior change (intended, tested):** formerly rejected `fused`+I/S
  combinations are now accepted and run end-to-end for CervenyGaussian
  (both coords), GeometricHat (both coords), GeometricGaussian —
  component/solver tests cover accept-and-run per family × mode; the
  solver's coherent-mode rejection message
  (`fused_ray_reuse_solver.cpp:99-103`) is replaced by family-legality
  messaging; SimpleGaussian non-coherent keeps the existing product
  message (`single_frequency_solver.cpp:224-227` path unchanged).
- Usage/warning text reflects the fused family×mode domain (design §9);
  error strings generalized with component tests updated.
- No legacy `nonreuse`/`reuse`/`parallel` behavior change; docs state fused
  eligibility per family × mode and that fused eligibility is a subset of
  the legal support matrix (design §6.2, §9).

Evidence:
- (placeholders)

### A08 [STANDARD] PRT, statistics, docs
Status: TODO
Reviewer: N/A

Acceptance:

- Statistics envelope applicability documented per family (design §3.4);
  PRT output correct for all families and all fused modes — the PRT run-mode
  lines ("Incoherent TL calculation", "Semi-coherent TL calculation") are
  reproduced legacy-exactly for fused runs (mode reporting, design §3.4);
  no overclaim in docs.
- IGR-3A documentation updated to evidence level (design + results).

Evidence:
- (placeholders)

### A09 [ADVANCED] Full regression + benchmark (batch acceptance prep)
Status: TODO
Reviewer: N/A

Acceptance:

- Clean-tree build in `Bellhop_RayReuse/build/igr3a-clean`; full `ctest`,
  `uv run pytest`, `make -C test/standard_cases test-unit`, model_matrix
  gate, `make f2cpp-regression`, `git diff --check`. Full regression
  includes the six I/S standard cases (three-party products).
- Shared-test infra (design §11 Level F): add the `broadband_regression`
  16F-class profile to the six I/S benchmark cases (50–500 Hz ×16 for
  `incoherent_direct`/`semicoherent_direct`, matching `munk_cerveny_cc`;
  1000–2500 Hz ×16 for the four 1000-Hz-class cases) with oracle
  regeneration, BEFORE the benchmark run; substitution only with recorded
  justification.
- Level F performance protocol (below) executed and recorded — coherent +
  I/S sets.
- Coordinator spot-checks key gates personally.

Evidence:
- (placeholders)

## 3. Batch acceptance

All tasks DONE + reviewer PASS; Levels A-F evidenced for all five families
across their fused mode sets (C everywhere; C+I+S for Cerveny both coords,
Hat both coords, GeoGaussian — I and S gated separately, both bitwise);
frozen-cache contract intact (Level A everywhere, every mode); R/E regression
boundary clean; no open HIGH/BLOCKER; docs match evidence; Git scope clean
and limited to `Bellhop_RayReuse/` + shared tests/docs. Then final-reviewer
ACCEPTED → commit → IGR-3B design may start.

STOP rule (user-frozen, design §11): if any family's floating-order
structure proves unable to reach the bitwise gate, construction STOPS —
architect + advanced-worker perform an order re-audit. Downgrading to
tolerance-based acceptance is forbidden.

## 4. Performance protocol (Level F)

- Five families (six kernel configurations: CC, RC Cerveny, Hat-C, Hat-RC,
  GeoGaussian, SimpleGaussian) coherent × `--fused-range-workers 1/2/4/8`,
  PLUS the six I/S benchmark configurations: CC I/S
  (`incoherent_direct`, `semicoherent_direct`), Hat-Cartesian I/S
  (`geometric_hat_incoherent`, `geometric_hat_semicoherent`), GeoGaussian
  I/S (`geometric_gaussian_incoherent`, `geometric_gaussian_semicoherent`).
- Frozen benchmark case inventory (design §13 item 2 + design §11 Level F
  extension; substitution only with recorded justification) — coherent: six
  16F-class broadband variants from `test/standard_cases/cases/`:
  `munk_cerveny_cc` (CC reference), `ray_centered_component_pressure`
  (RC Cerveny), `geometric_hat_cartesian` (Hat Cartesian),
  `geometric_hat_ray_centered` (Hat Ray-Centered),
  `geometric_gaussian_cartesian` (Geometric Gaussian),
  `simple_gaussian_cartesian` (Simple Gaussian); I/S: the six cases above,
  each gaining a `broadband_regression` 16F-class profile in A09 (shared
  test infra, values frozen in design §11); 1 warmup + ≥3 measured runs.
- Report median/min/max/MAD for wall, Trace, Project, Influence, Scale
  seconds and `ru_maxrss`; record git HEAD + binary sha256;
  `require_identical_sample_hashes` across worker counts (Level D re-check).
- CC additionally carries the A02 perf-magnitude proof vs the `38137a4`
  baseline binary.
