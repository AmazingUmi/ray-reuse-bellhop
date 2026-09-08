# IGR-3A — Remaining TL Beam Family Adaptation — WORKLIST

> **Status:** ACCEPTED / CLOSED (2026-09-04).
> A01-A09 all DONE; every checkpoint reviewed (recorded substitutions:
> A01/A02/A09 final-reviewer-as-reviewer fallbacks; A05/A06 fresh-instance
> re-verifications after environment failures). A09 independent checkpoint
> PASS (final-reviewer substitution). Batch Acceptance gates executed
> (A09 evidence). Final Review: first round verified ALL technical,
> evidence, protocol, cache, scope, and hygiene checks PASSED and returned
> CHANGES_REQUIRED on a single LOW doc-level finding (stale construction-
> status headers), remediated by the coordinator; closing re-review
> (2026-09-04, fresh final-reviewer instance substituting the
> non-resumable first-round reviewer — substitution recorded) verified the
> finding CLOSED and returned **ACCEPTED**. Batch IGR-3A is CLOSED;
> IGR-3B design may start only per the scope authority's sequencing rule.
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
Status: DONE
Reviewer: PASS (2026-09-04; independent line-diff of the intensity twin vs
the coherent twin, Level B reference-validity audit, independent rebuild +
44/44 ctest + self-run reuse-vs-fused SHD byte identity on both Level E
cases)

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

- New construction (design §5/§8): private fused intensity entry
  `CartesianCervenyInfluence::accumulateFusedIntensityPrevalidated` +
  `accumulateFusedIntensityImpl<CollectStatistics, ImageCount>` (header
  forward-declares `FusedIntensityWorkspace`; the anonymous-namespace fused
  entry validator became a template over the workspace kind — same checks,
  same messages). Programmatic body diff vs the coherent twin: only the
  function name, the cell span type, and the store block differ; traversal,
  union prefix, per-frequency masks, range clamp, interpolation, image loop
  (incl. unconditional zero-adds on window/taper rejection), encounter
  order, and every statistics counter are byte-identical. Store per eligible
  (ray, cell, lane): contribution = corrected * imageSum (identical
  arithmetic), intensity = ABS(contribution)^2 via abs-then-multiply
  (std::norm forbidden), legacy validation messages verbatim, read-add-
  assign directly on the FusedIntensityWorkspace lane (not routed through
  IntensityWorkspace::add). Dispatch mirrors the coherent dispatch
  (statistics x imageCount 1/2/3).
- Adapter: both A01 throws replaced by forwarding
  (`accumulateFusedIntensity` -> kernel entry; `scaleIntensityFrequency` ->
  `scaleCartesianIntensityToPressure`, FrequencyWorkspace by value).
- Gate: `NotCoherent` arm renamed `RunModeIllegalForFamily` (family-legality
  message, design §9), dormant until A06 (CC TL modes C/I/S all pass);
  family/coords checks precede it. main.cpp fused TL gate drops the
  coherent requirement ("--execution-mode fused requires Cartesian Cerveny
  TL") so fused I/S runs end-to-end (design §6.2 intended extension);
  reuse-deprecation warning now fires for I/S CC via the unchanged
  supportsFusedRayReuse predicate. solveStreaming reads the run mode once
  per run; coherent chain preserved (same calls/arguments/order); I/S chain
  = accumulateFrequenciesIntensity -> materializeIntensityFrequency ->
  scaleIntensityFrequency -> consumer (pressure representation in every
  mode); timing/fingerprint/statistics semantics unchanged (design §3.4).
- Level B raw reference note: solveFrequencyFromSourceCache(Raw) returns the
  converted pressure workspace for I/S (conversion is workspace
  construction, single_frequency_solver.cpp:356-369), so the raw gate uses
  a legacy reference built from the exact solver loop via the public
  `CartesianCervenyInfluence::accumulateIntensity` kernel, plus the
  converted end-to-end raw seam — both memcmp'd bitwise. Reviewer audited
  the reference against single_frequency_solver.cpp:281-354 (same projector,
  Lloyd chain, epsilon channel, ray order; reference and fused sides share
  no kernel code).
- New component test `rayreuse.component.fused_cc_intensity_parity`
  (fused_cc_intensity_parity_test.cpp): Levels B/C/D/A for I and S gated
  separately — fixtures A (3-image), A-parallel 16F x workers 1/2/4/8, A2
  (2-image), B divergent prefixes (guard: 96/300 rays diverge, 96 truncated
  — no vacuous pass), C (WKB). All PASS: raw intensity span memcmp,
  converted seam memcmp, scaled-workspace memcmp vs SerialRayReuseSolver,
  per-worker-count gating against the same serial reference, fingerprint
  before==after==serial reuse.
- Build/tests: `cmake --preset release && cmake --build --preset release -j`
  exit 0 (RAYREUSE_WARNINGS_AS_ERRORS=ON, zero warnings); extra -fsyntax-only
  compile of the kernel TU without NDEBUG (debug-validation blocks) clean;
  `ctest --test-dir build/release` → 44/44 passed (43 existing — incl.
  unedited fused_cc_parity and fused_solver with the intended CC+I/S gate
  acceptance replacing the old rejection — + 1 new). `git diff --check`
  clean; reference implementations untouched.
- Level E (broadband_smoke 50,250 Hz; candidate exe sha256 63faaa6a7688
  50bbb49985ae2aca26c94a2d41cdd1a4f1a4f842dc7018583851): harness `test`
  PASSED for origin, f2cpp, rayreuse-nonreuse, rayreuse-reuse
  (--rayreuse-execution-mode reuse), rayreuse-fused (fused; PRT "execution
  mode = broadband fused reuse" validated). Origin-oracle compare per
  frequency: max TL diff 1.52587891e-05 dB (tolerance 0.001 dB), identical
  metrics for f2cpp and rayreuse nonreuse/reuse/fused.
- Level E byte identity (direct binary runs, --verify-cache; independently
  reproduced by reviewer): incoherent_direct reuse == fused w1 == fused w8
  SHD sha256 dadb0dbf0b3190b33784891fb10142d58a7aab4e2b0e9c30918eb8217daed
  351; semicoherent_direct all three 8bc6260e762f792305b432a4f34e4e45db194a
  4ce19ecb1d772cb955a6237d44 (cmp byte-identical); cache fingerprint
  11632125087325642441 stable before==after and equal across reuse/fused
  runs.
- Format gate: baseline-broken environment (A01 finding stands); no
  formatter run over any file; new code mirrors committed sibling style.
  Local-formatter (Apple clang-format 21) disagreement delta vs HEAD:
  main.cpp +0, solver hpp +0, adapters +5, solver cpp +8, CC hpp +19, CC
  cpp +99, solver test -3; new test 164 vs committed coherent twin 172 —
  same signature-wrapping drift class as the committed baseline.

### A03 [ADVANCED] Ray-Centered Cerveny fused
Status: DONE
Reviewer: PASS (2026-09-04; independent flip-parity audit vs legacy
byte-identical accumulateImpl, receiver-run anchor semantics audit, V/H
conjugation check, independent rebuild + 45/45 ctest + self-run CR/IR
reuse-vs-fused SHD byte identity; 2 findings: worklist-update [coordinator,
done] and stale main.cpp usage text [routed to A07])

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

- New construction (design §5/§8): private fused entries
  `RayCenteredCervenyInfluence::accumulateFusedPrevalidated` +
  `accumulateFusedIntensityPrevalidated` over ONE shared traversal
  `accumulateFusedImpl<IntensityPayload, Workspace>` (workspace-kind branch
  at the store, mirroring the legacy single-traversal accumulateImpl;
  legacy body proven byte-identical to HEAD — diff purely additive +523/-0).
  Legacy depth -> image -> step -> ascending receiver-run skeleton preserved;
  step loop bound = union of per-frequency active prefixes with a per-lane
  prefix gate; per-lane q/gamma/kmah via epsilon in a flat
  per-(point, frequency) layout; normal n=(t_z,-t_x) computed once over the
  union prefix (frequency-independent); per-lane omega, radiusMax=30c0/f,
  beam-window test, taper, tau/reflectionPhase/amplitude, V/H factor
  branches (Fortran-conjugated DOT_PRODUCT for V, handwritten H) reproduced
  verbatim.
- Per-lane persistent flip parity (design §8, the A03-specific risk): each
  lane carries normalSign, reset per ray, flipped exactly where the legacy
  non-True-image accepted step flips (near-horizontal |n.depth|<eps gate
  BEFORE the flip; gate reads the never-flipped depth component so it is
  frequency-independent), evolved in legacy (depth, image, step) order
  bounded by that lane's own prefix; endpointNormal.range carries the parity
  into projectedRange -> receiver coverage -> interpolation weights.
  Construction bug found by the Level B gates and fixed: the shared normal
  was first computed only over lane 0's prefix, so the gate read zeroed
  normals in [P_0, unionPrefix) and skipped steps for longer lanes (S and
  divergent-prefix fixtures diverged ~1e-8 relative); fix = compute the
  normal over the union prefix. No tolerance gates introduced.
- Intensity store (design §8): magnitude = ABS(contribution), power =
  magnitude*magnitude, increment = taper*power (taper OUTSIDE the square,
  unlike CC); legacy IntensityWorkspace::add validation messages verbatim,
  read-add-assign directly on the FusedIntensityWorkspace lane. Entry-kind
  run-mode checks mirror the public entries ("ray-centered complex pressure
  requires coherent TL mode" / "ray-centered intensity requires incoherent
  or semi-coherent mode"). Statistics pointer accepted for adapter-shape
  uniformity; no counters collected (legacy RC kernel produces none and
  takes no statistics pointer, single_frequency_solver.cpp:332-333/347-348;
  design §3.4 zero-counter envelope; --profile-influence stays
  CLI-rejected for RC).
- Adapter `RayCenteredCervenyFusedAdapter` (fused_influence_adapters.hpp):
  makeKernel verbatim from single_frequency_solver.cpp:269-272 (+ runMode()
  + fieldComponent()); epsilon scratch/prepare identical to CC;
  accumulateFused/Intensity forward to the kernel entries; scaleFrequency ->
  scaleCoherentCartesianPressure, scaleIntensityFrequency ->
  scaleCartesianIntensityToPressure (family-based selector, verified).
  Dispatch: accumulateFrequencies / accumulateFrequenciesIntensity and the
  solveStreaming scale phase select CC vs RC on cervenyCoordinateSystem()
  (closed ctor-validated enum). Gate: NotCartesian scope arm removed — both
  Cerveny coordinates accepted for C/I/S (design §9); all other rejection
  messages byte-identical; main.cpp fused TL gate message now "requires
  Cerveny Gaussian TL".
- New component test `rayreuse.component.fused_rc_parity` (labels
  "component;reuse"): Levels B/C/D/A for C (complex memcmp), I and S gated
  separately (double memcmp) — fixtures A (3-image), A-parallel 16F x
  workers 1/2/4/8, A2 (2-image), B divergent prefixes @8 workers, C (WKB),
  D (Vertical), E (Horizontal). Fixture B runtime guard (C/I/S each):
  rays=300, divergent-prefix rays=96, cutoff-truncated rays=96,
  flip-parity divergent rays=47 — divergence non-vacuous for coherent AND
  intensity payloads (reviewer independently reproduced the guard counts).
  I/S Level B reference = public RayCenteredCervenyInfluence kernel loop +
  converted Raw seam, both memcmp'd (reviewer audited against
  single_frequency_solver.cpp:281-350; no shared code with the fused side).
  supportsFusedRayReuse RC C/I/S acceptance also asserted in
  fused_ray_reuse_solver_test (RC end-to-end streaming + fingerprint check
  added; existing CC rejections byte-identical).
- Build/tests: cmake --build --preset release exit 0
  (RAYREUSE_WARNINGS_AS_ERRORS=ON, zero warnings); ctest --test-dir
  build/release -> 45/45 passed. git diff --check clean; reference
  implementations untouched.
- Level E coherent (ray_centered_component_pressure, broadband_smoke 1000,
  2000 Hz, candidate exe sha256 2d78460ea3624a97e2dd9a1c19213b7020d5feb768
  916692cb7b86144a1e4f63): harness test PASSED for origin, f2cpp,
  rayreuse-nonreuse, rayreuse-reuse, rayreuse-fused (PRT "execution mode =
  broadband fused reuse" validated; reuse-deprecation warning now fires for
  RC — intended). Origin-oracle compare per frequency: max TL diff
  6.10351562e-05 dB (tolerance 0.001 dB).
- Level E byte identity (direct binary runs, --verify-cache; artifacts
  /tmp/igr3a_a03_coherent.jObb/; reviewer re-ran independently): coherent
  reuse == fused w1 == fused w8 SHD sha256 2b827187a4fbaba51f6910b2366972cc
  6dd27ff1ca287f47ecd3f8553f4e2d27; IR ce121f941ff8db9c46554293e5de94feba1
  72df50cdbbc6db29978c28b60696; SR 95c18c716025ff21018a12eff8ef8a9384b18647
  d08033642e7d605bfff3023e; supplementary 3-image IR 48d46dd2dc9531dffa051d
  1798af97d68d29684659a883925a220f0ec699c623. Cache fingerprint
  10925417565703232468 stable before==after and equal across reuse/fused
  runs; PRT run-mode lines ("Incoherent/Semi-coherent TL calculation")
  correct; w8 runs effective workers 8.
- Level E gap recorded (design §11): no RC-Cerveny I/S standard case
  exists; I/S evidenced by targeted fused-vs-legacy-reuse SHD byte identity
  on envs derived from ray_centered_component_pressure with RunType changed
  to 'IR'/'SR' (token mapping verified in the env parser canonicalRunType),
  >=2 frequencies (1000, 2000 Hz), plus a 3-image IR variant exercising
  flips end-to-end.
- Format gate: baseline-broken environment (A01 finding stands); no
  formatter run over any file; new code mirrors committed sibling style.
  Apple clang-format 21 whole-file disagreement counts: RC cpp 96, RC hpp
  23, adapters 121, solver cpp 64, main.cpp 28, solver test 166, new test
  238 — same signature-wrapping drift class as the committed CC twins.

### A04 [ADVANCED] Geometric Hat fused, both coordinate systems
Status: DONE
Reviewer: PASS (2026-09-04; independent numerical-order audit of both fused
traversals vs legacy, launchAngleStep frozen-interface adaptation scrutiny,
legacy additive-freeze audit, independent rebuild + 46/46 ctest + self-run
Hat-RC 'Sg' byte identity; non-blocking observations: worklist update
[coordinator, done], S-mode divergent-prefix guard probes with amplitude 1.0
— approximation noted, runtime S byte identity confirms real divergence)

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

- New construction (design §5/§8): private fused entries
  GeometricHatInfluence::accumulateFusedPrevalidated +
  accumulateFusedIntensityPrevalidated (no epsilons span) over
  accumulateFusedImpl<IntensityPayload, Workspace> — entry validation
  (legacy validateField conditions, fused-prefixed diagnostics; legacy
  whole-workspace payload rescan not restored, CC A02 precedent) + the
  once-per-ray coordinate routing of the legacy accumulateField split.
  Cartesian traversal: segment -> monotone range cursor -> ascending depth
  rows; initial receiver index (find_if + slowness.range sign), cursor
  movement, segment geometry/tangent/normal, degenerate-length skip before
  the caustic update, caustic phase accumulation, interpolationWeight/
  normalOffset/q/beamRadius(|q/q0|)/hatWeight/amplitude base are
  frequency-independent, computed once and shared; per-lane delay,
  amplitudeConstant (legacy association (sourceRatio*sqrt(c/|q|))*amp),
  phaseAtReceiver (reflectionPhase + phase + receiver caustic pi/2), and
  the per-lane segment mask rightIndex < activeCount(state_f); cursor runs
  intersected with [rangeBegin,rangeEnd) with cursor anchors unclamped.
  RC traversal: depth -> segment -> receiver runs (ascending forward /
  descending reversed, legacy order preserved); per-depth persistent
  caustic phase, previousQ, and receiver anchors evolved ONCE over the
  union prefix (pure path functions — each lane's legacy evolution is
  identical at every step index its own prefix reaches); normals and
  scaledBase (sourceRatio*sqrt(c)) shared over the union prefix; per-lane
  (scaledBase[right]*amp)/sqrt(|q|) association, delay, reflectionPhase,
  masks; runs clamped to the partition via 1-based [max(prev+1/r+1,
  rangeBegin+1), min(cur/prev, rangeEnd)] with an empty-intersection guard
  on the descending branch.
- Intensity twins (design §8): attenuatedConstant = amplitudeConstant *
  exp((omega*delay).imag()); power = attenuatedConstant^2; increment =
  power * hatWeight (hat weight applied exactly once, NOT ABS(coherent)^2);
  legacy validation messages verbatim + the IntensityWorkspace::add
  accumulated-finite check, read-add-assign directly on the fused lane.
- launchAngleStep resolution (frozen adapter shape carries no spacing; Hat
  has no epsilon channel): makeKernel constructs with the verbatim
  single_frequency_solver.cpp:247-250 args, then installs
  launchFanPlan().launchAngleStep via private friend setter
  setFusedLaunchAngleStep (kernel-run state, mirrors RC runMode_
  precedent; public per-frequency entries unchanged; reviewer audited:
  default 0.0 never read by legacy paths, unset fused path rejected by
  validateFusedHatInput spacing>0, kernel lifetime per-worker-per-run).
- Adapter GeometricHatFusedAdapter: empty inline prepareScratch/
  preparePerRay (no epsilon channel, design §4); forwarding hooks;
  scaleFrequency -> scaleCoherentGeometricPressure,
  scaleIntensityFrequency -> scaleGeometricIntensityToPressure
  (family-based selector, identical for both Hat coordinates). Dispatch:
  public entries + solveStreaming scale phase select Hat by family before
  the Cerveny coordinate split. Gate: family in {CervenyGaussian,
  GeometricHat} (arm renamed UnsupportedBeamFamily; message "fused
  ray-reuse solver requires the Cerveny Gaussian or geometric hat beam
  family"; every other rejection byte-identical); main.cpp CLI gate
  "--execution-mode fused requires Cerveny Gaussian or geometric hat TL".
  Statistics pointer accepted, no counters (design §3.4 non-CC envelope).
- New component test rayreuse.component.fused_hat_parity (labels
  "component;reuse"): Levels B/C/D/A for C, I, S gated separately x BOTH
  coordinates — fixture A (Munk hat), A-parallel 16F x workers 1/2/4/8,
  fixture B divergent prefixes @8 workers with runtime guard (300 rays,
  96 divergent-prefix, 96 cutoff-truncated — every one of the six
  mode x coordinate combos, no vacuous pass). Level B references: coherent
  = solveFrequencyFromSourceCache(Raw); I/S = in-test legacy loop via
  public accumulateIntensity (exact solver loop, no shared kernel code
  with the fused side) + converted seam via
  scaleGeometricIntensityToPressure memcmp'd against the Raw delivery.
  Level C vs SerialRayReuseSolver::solve per mode x coordinate; Level A
  fingerprint before==after==serial reuse.
- Build/tests: cmake --build --preset release exit 0
  (RAYREUSE_WARNINGS_AS_ERRORS=ON, zero warnings); ctest --test-dir
  build/release -> 46/46 passed (45 existing + 1 new).
- E-boundary gates (design §10): rayreuse.component.arrival_eigenray_solver,
  multi_source_product, multi_source_writer PASS; make test-unit (uv)
  177/177 OK incl. validate_i8_eigenrays; harness single-profile PASS x10
  (geometric_hat_cartesian/incoherent/semicoherent/ray_centered,
  eigenray_geometric_hat(+_ray_centered), arrival_geometric_hat_ascii/
  binary/ray_centered(_binary)) — legacy paths and the frozen
  forEachRayCenteredEvaluation untouched (reviewer verified diff
  additive-only, zero deleted lines).
- Level E three-party harness (broadband_smoke): geometric_hat_incoherent
  + geometric_hat_semicoherent (1000,2000 Hz) and
  geometric_hat_ray_centered (100,200 Hz) PASSED for origin, f2cpp,
  rayreuse nonreuse/reuse/fused (PRT "execution mode = broadband fused
  reuse"; reuse-deprecation warning now fires for Hat — intended).
- Level E byte identity (direct binary runs, --verify-cache; candidate exe
  sha256 8c1f6375adb5fa72e18457ef69274723638b88f4c96704ba05d8bacbffcd1b31;
  artifacts /tmp/igr3a_a04/direct/; reviewer independently reproduced the
  'Sg' identity): reuse == fused w1 == fused w8 (cmp IDENTICAL) — Hat-C I
  09c2a80507a465fe1b215491af3394d1f46d5af1589f27f449de0c939d73ef28; Hat-C
  S ab92d113875994b7ceda2515cb668314629f43ff63b01c63bc589af77f841593;
  Hat-RC C fef67deae6f74627d385782a2464adb456d8aec75f0be411dab1e4836a71c
  6eb; derived Hat-C C (geometric_hat_cartesian env + --frequencies-hz
  100,200 — the case has no >=2F profile, gap recorded) 528af70c7c274dd9d4
  364623a046bc1f478e99255256cfcf0e774cb4a4848bce; derived Hat-RC I ('Ig')
  010b4a180c28bffefb4b4ff6707eb5668a9ec713a58a9956eacc417bad7dd819;
  derived Hat-RC S ('Sg') 101e490e52fb599127833aab5868cd8ae1d1ff1ae151c58
  b0f0209d6138498a7 (no Hat-RC I/S standard case exists — gap recorded,
  design §11). Cache fingerprints stable before==after: Hat-C cases
  10925417565703232468, RC-geometry cases 11321016705018875701; w8
  effective workers 8 (Hat-C) / 3 (RC, clamped to the 3 receiver ranges);
  PRT run-mode lines correct.
- git diff --check clean; reference implementations untouched; no
  generated products in tree.
- Format gate: baseline-broken environment (A01 finding stands); no
  formatter run over any file (clang-format absent from the current PATH —
  per-file disagreement counts not re-measurable locally); new code
  mirrors committed sibling style (A03 twins).

### A05 [ADVANCED] Geometric Gaussian fused
Status: DONE
Reviewer: PASS (2026-09-04; technical scope fully verified PASS — per-
expression numerical audit vs legacy, sigma-branch fixture audit with
independently reproduced 1566/559 guard counts, legacy additive-freeze,
independent rebuild + 47/47 ctest + fresh C/I/S byte-identity runs. One
CHANGES_REQUIRED finding, worklist-record-keeping only, remediated by
coordinator and re-verified PASS by a fresh reviewer instance — the
original reviewer agent could not be resumed due to an environment
model-provider failure; substitution recorded)

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

- New construction (design §5/§8): private fused entries
  GeometricGaussianInfluence::accumulateFusedPrevalidated +
  accumulateFusedIntensityPrevalidated (no epsilons span) over
  accumulateFusedImpl<IntensityPayload, Workspace> — entry validation
  (legacy validateField per-ray conditions, fused-prefixed diagnostics,
  A04 Hat precedent) + ONE shared Cartesian traversal with a per-lane
  payload branch at the store. Legacy skeleton preserved: segment ->
  monotone receiver cursor -> ascending depth; degenerate-length skip,
  tangent/normal, caustic phase accumulation, q-term, q0, source ratio,
  amplitude association (sourceRatio*sqrt(c/(q0*sigma1)))*amplitude all
  legacy-verbatim (reviewer: per-expression identical incl. strictness of
  prefilter/window comparisons, both acoustic phase-update points, cursor
  advance, requireFinite order). Width sigma1 is frequency-local exactly
  as legacy (frozen design §8): per-lane wavelengthSigma/nearFieldSigma
  evaluated per segment; the shared min(nf,wl) feeds BOTH the
  segment-sigma depth prefilter and the receiver sigma1 at the legacy
  evaluation points — per-lane depth prefilters and eligibility windows
  NOT lifted. Per-lane prefix gate (rightIndex < that lane's
  activeCount), delay, gaussianWeight, phaseAtReceiver; cursor runs
  intersected with [rangeBegin,rangeEnd), anchors unclamped. Intensity
  store: sqrt(2 pi) * attenuatedConstant^2 * gaussianWeight with legacy
  validation messages verbatim + the add-path accumulated-finite check,
  read-add-assign directly on the fused lane. Statistics pointer accepted,
  no counters (legacy Gaussian accumulate takes no statistics pointer,
  design §3.4 non-CC envelope). Kernel diff purely additive (+444/-0 vs
  HEAD; legacy public surfaces and accumulateArrivals/
  collectEigenrayHits byte-untouched, design §10 — reviewer verified).
- launchAngleStep resolution (A04 Hat precedent): makeKernel constructs
  with verbatim single_frequency_solver.cpp:251-253 args, then installs
  launchFanPlan().launchAngleStep via private friend setter
  setFusedLaunchAngleStep; public per-frequency entries unchanged.
- Adapter GeometricGaussianFusedAdapter (fused_influence_adapters.hpp):
  empty inline prepareScratch/preparePerRay; forwarding hooks;
  scaleFrequency -> scaleCoherentGeometricPressure,
  scaleIntensityFrequency -> scaleGeometricIntensityToPressure
  (family-based selector). Dispatch: public entries + solveStreaming
  scale phase (coherent and I/S chains) select Gaussian by family.
  Gate: family in {CervenyGaussian, GeometricHat, GeometricGaussian}
  (message "...Cerveny Gaussian, geometric hat, or geometric Gaussian
  beam family"; every other rejection byte-identical); main.cpp CLI gate
  "--execution-mode fused requires Cerveny Gaussian, geometric hat, or
  geometric Gaussian TL". Scope test: Gaussian C/I/S acceptance + fused
  streaming/fingerprint wiring added; unsupported-family rejection now
  SimpleGaussian (widens in A06).
- New component test rayreuse.component.fused_geometric_gaussian_parity
  (labels "component;reuse"): Levels B/C/D/A for C, I, S gated
  separately — fixture A (Munk gaussian 2F), A-parallel 16F x workers
  1/2/4/8, fixture B divergent prefixes @8 workers with runtime guard
  (rays=300, divergent-prefix=96, cutoff-truncated=96 per mode),
  fixture C sigma-branch {50,1000} Hz @8 workers with runtime guard:
  window-divergent rays=1566, branch-divergent rays=559 per mode —
  per-lane sigma1 eligibility windows and width branches genuinely
  diverge; Level B bitwise parity asserted ON that fixture incl. the I/S
  converted seam (no vacuous pass; reviewer reproduced the guard counts
  exactly). Level B references: coherent =
  solveFrequencyFromSourceCache(Raw); I/S = in-test legacy loop via
  public accumulateIntensity (exact solver loop, no shared kernel code
  with the fused side) + converted seam via
  scaleGeometricIntensityToPressure memcmp'd against the Raw delivery.
  Level C vs SerialRayReuseSolver::solve per mode; Level A fingerprint
  before==after==serial reuse.
- Build/tests: cmake --build --preset release exit 0
  (RAYREUSE_WARNINGS_AS_ERRORS=ON, zero warnings); ctest --test-dir
  build/release -> 47/47 passed (46 existing + 1 new).
- E-boundary gates (design §10): rayreuse.component.arrival_eigenray_
  solver, multi_source_product, multi_source_writer PASS (in 47/47);
  make test-unit (uv) 177/177 OK incl. validate_i8_eigenrays; harness
  single-profile PASS for eigenray_geometric_gaussian and
  arrival_geometric_gaussian_irregular — legacy paths untouched.
- Level E three-party harness (broadband_smoke 1000,2000 Hz):
  geometric_gaussian_cartesian + geometric_gaussian_incoherent +
  geometric_gaussian_semicoherent PASSED for origin, f2cpp, rayreuse
  nonreuse/reuse/fused (PRT "execution mode = broadband fused reuse"
  harness-validated; reuse-deprecation warning now fires for Gaussian —
  intended).
- Level E byte identity (direct binary runs, --frequencies-hz 1000,2000
  --verify-cache; candidate exe sha256 4e97163f46b6bf8677b4615ee849f958
  d8439f45df2ffa4f6709e033dd389556; artifacts /tmp/igr3a_a05/direct/;
  reviewer re-ran fresh and reproduced all six configurations
  bit-identical): reuse == fused w1 == fused w8 (cmp IDENTICAL) — C
  15e9739422f2b1aba38f67f1118edea8e4d95d97e8bf817b4ce0daea216fd7dd; I
  43085029261694680a5fae7a677f1763e468174a1ae579ca7d340dbfdf6d1094; S
  64b33350130c21bf389cd56fa171a5e3f47d8d7b3b6ee8e6369937d8d9e38b3c.
  Cache fingerprint 10925417565703232468 stable before==after and equal
  across all nine runs; PRT run-mode lines correct; w8 effective
  workers 8.
- git diff --check clean; reference implementations untouched; no
  generated products in tree.
- Format gate: baseline-broken environment (A01 finding stands); no
  formatter run over any file; new code mirrors committed sibling style
  (zero >80-column lines in the new/edited A05 files).

### A06 [ADVANCED] Simple Gaussian fused
Status: DONE
Reviewer: PASS (2026-09-04; after remediation of record-keeping findings —
see below. Technical scope independently verified PASS: mechanical text
diff of fused vs legacy loop bodies, shared previousQ/causticPhase
pure-path-function audit (all three update points path-only; each lane's
legacy loop a causal prefix of the union traversal), load-bearing
intensity-entry rejection trace, independent rebuild + 48/48 ctest +
fresh reuse==fused byte-identity runs. CHANGES_REQUIRED findings were
worklist-record-keeping only: A06 block not yet written, probe wording
overclaim, diff-stat misreport — remediated by coordinator with corrected
wording, re-verified PASS by a fresh reviewer instance — the original
reviewer agent could not be resumed due to an environment model-provider
failure; substitution recorded)

Acceptance:

- Kernel: segment→range cursor→depth rows; frequency-independent width;
  per-f phase/amplitude/masks; coherent only — SimpleGaussian's ONLY legal
  product mode (design §9), not a fused restriction; existing ctor
  point-source and step validations surface unchanged; no irregular support
  added.
- Gate widened; Levels A-E.

Evidence:

- New construction (design §5/§8, coherent ONLY — the family's single legal
  product mode, §9): private fused entry
  SimpleGaussianInfluence::accumulateFusedPrevalidated (no epsilons span,
  no intensity twin) + friend SimpleGaussianFusedAdapter + private
  setFusedLaunchAngleStep fused-run state (A04/A05 precedent — legacy
  gaussianA takes the per-call launch-angle spacing
  [single_frequency_solver.cpp:304], distinct from the ctor's
  configuredStepLengthMeters that feeds legacyArcLength only).
- Legacy traversal reproduced verbatim (reviewer: mechanical text diff of
  the two loop bodies — every expression verbatim; only the loop bound
  pointCount->unionPrefix, per-lane locals moved inside the lane loop,
  partition gate around stores, fused cell target, and dropped
  diagnostics differ): segment loop over the union active prefix
  (per-lane activeCount scan, first inactive point retained) -> monotone
  range cursor (byte-identical advance incl. the in-while
  1000*floatingSpacing(leftRange) degenerate test; entry caustic check
  runs for degenerate segments as legacy) -> shared depth rows; no
  irregular support. previousQ/causticPhase update at exactly three
  legacy points (segment entry, per matched range in-while, while-tail)
  with operands exclusively path geometry -> ONE shared state over the
  union traversal is legacy-exact per lane (each lane's loop is a causal
  prefix; reviewer audited all three update points); per-match caustic
  update + previousQ hand-off run on EVERY matched range (anchors
  unclamped), stores partition-gated [rangeBegin,rangeEnd). Per-lane:
  prefix bound, tau, right amplitude, reflection phase, omega, delay,
  phase, contribution (association and the binary32 0.98F beta literal
  verbatim), complex read-add-assign with legacy validation messages.
  validateFusedSimpleGaussianInput (A04/A05 precedent; no
  whole-workspace payload rescan). Ctor point-source/step validations
  surface unchanged via makeKernel; kernel diff purely additive (cpp
  +289/-0, hpp +36/-0 — reviewer-corrected stat; an earlier +290/-1
  report was a miscount). Two immaterial validation-order notes on
  invalid data only (requireFinite checks precede the lane contribution
  in fused vs after in legacy; pre-depth checks partition-gated so they
  run once in the owning worker) — no effect on valid-data bits, Level B
  bitwise proves it, A04/A05 precedent.
- Adapter SimpleGaussianFusedAdapter: verbatim makeKernel + setter
  install; empty inline scratch hooks; coherent forwarding hook;
  scaleFrequency -> scaleCoherentGeometricPressure. NO intensity twins:
  structurally compile-time-enforced (IntensityFusedSink::accumulate
  requires Adapter::accumulateFusedIntensity, which the adapter lacks;
  absence verified in-tree at fused_influence_adapters.hpp:505-585);
  the adapter-level non-instantiation probe was a throwaway /tmp compile
  check, NOT committed (in-tree compile-time evidence is the pre-existing
  kernel-level static_assert at
  tests/component/simple_gaussian_influence_test.cpp:35); runtime
  reachable enforcement is the intensity-entry family rejection below.
- Gate: family arm + main.cpp CLI gate widened to SimpleGaussian
  (messages now list simple Gaussian; product gate at
  single_frequency_solver.cpp:224-227 byte-untouched).
  RunModeIllegalForFamily LIVE as defense-in-depth: SimulationCase
  construction rejects SG+I/S upstream (simulation_case.cpp:404-409,
  pre-existing), so the REACHABLE enforcement is the new family
  rejection at the top of accumulateFrequenciesIntensity
  (fused_ray_reuse_solver.cpp:390-394, family-legality message) thrown
  before the family dispatch — reviewer traced that without it an SG
  (coherent-only) case would fall through the dispatch to the Cerveny
  coordinate check and silently instantiate
  CartesianCervenyFusedAdapter+IntensityFusedSink; the rejection is
  load-bearing. Scope test: SG coherent acceptance + streaming/
  fingerprint wiring + SG+I/S constructibility rejection +
  intensity-entry rejection; Cerveny/Hat/GeoGauss I/S acceptance
  unchanged.
- New component test rayreuse.component.fused_simple_gaussian_parity
  (labels "component;reuse"): Levels B/C/D/A coherent — fixture A (Munk
  SG 2F), A-parallel 16F x workers 1/2/4/8, fixture B divergent prefixes
  @8 workers with runtime guard (rays=300, divergent-prefix=96,
  cutoff-truncated=96), fixture A caustic guard (rays=500,
  caustic-phase-active=500 — no vacuous pass; reviewer reproduced all
  guard counts). Level B reference =
  solveFrequencyFromSourceCache(Raw); Level C vs SerialRayReuseSolver;
  Level A fingerprint before==after==serial reuse.
- Build/tests: uv run cmake --build --preset release exit 0
  (RAYREUSE_WARNINGS_AS_ERRORS=ON, zero warnings); kernel TU -Werror
  -fsyntax-only without NDEBUG clean; ctest --test-dir build/release ->
  48/48 passed (47 existing + 1 new).
- E-boundary: multi_source_product, multi_source_writer,
  arrival_eigenray_solver PASS (in 48/48); make test-unit (uv) 177/177
  OK.
- Level E (broadband_smoke 1000,2000 Hz, native 2F — no derived-env
  gap): simple_gaussian_cartesian three-party harness PASSED for origin,
  f2cpp, rayreuse nonreuse/reuse/fused (SG reuse-deprecation warning
  fires — intended; reviewer re-ran the rayreuse leg). No I/S Level E
  (illegal mode for the family).
- Level E byte identity (direct runs, --frequencies-hz 1000,2000
  --verify-cache; candidate exe sha256 23fefef189a82e9962a04b4d882c0fb1
  bfd874b87cef0dd0b0b2644a217daf21; reviewer's fresh runs reproduced
  reuse == fused serial == fused w1 == fused w8): SHD sha256
  5cc8f28180a1c6eaaec5b26c052cbda0e20b50a49f4ee9569007fe0eafa2436d;
  cache fingerprint 10925417565703232468 stable before==after across all
  runs (equal to A05's legitimately — identical launch geometry,
  family-independent trace); PRT "execution mode = broadband fused
  reuse"; w8 effective workers 8.
- git diff --check clean; reference implementations untouched; no
  generated products in tree; format gate: baseline-broken environment
  (A01 finding stands), no formatter run, new code mirrors committed
  sibling style (zero >80-column lines in new/edited A06 files).

### A07 [STANDARD] CLI, routing, and support matrix
Status: DONE
Reviewer: N/A (STANDARD, reviewer-on-need per AGENTS.md §5; coordinator
spot-check performed personally: release build clean, ctest 48/48, --help
fused paragraph verified against the implemented domain, support-matrix
doc section verified accurate with no acceptance overclaim, diff confined
to Bellhop_RayReuse/)

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

- Usage text (main.cpp printUsage) rewritten to the fused family×mode
  domain: multi-frequency TL, regular receiver grids, Cerveny Gaussian +
  geometric hat (both coordinate systems), geometric Gaussian, simple
  Gaussian; coherent for all supported families, I/S where legal; fused
  eligibility stated as a subset of each family's legal support matrix;
  legacy deprecation sentence kept. String sweep confirmed all other
  fused user-visible strings (TL gate comment/messages main.cpp:325-352,
  R/1-freq/A-a-E routing rejections, PRT log,
  warnIfReplaceableLegacyMode warnings) already family/mode-accurate;
  warning form kept per design §13 item 3. (Resolves the stale-usage
  findings routed from the A03/A05/A06 reviews.)
- supportsFusedRayReuse remains the single predicate driving CLI warnings
  and solver validation; non-TL CLI rejections byte-unchanged.
- Accept-and-run audit: all 13 combos (CC C/I/S, RC C/I/S, Hat-C C/I/S,
  Hat-RC C/I/S, GeoGaussian C/I/S, SG C) covered by predicate assertions
  in fused_ray_reuse_solver_test + end-to-end solveStreaming (coherent
  wiring runs in the scope test; CC bitwise streaming; I/S Level C
  solveStreaming in fused_cc_intensity/fused_rc/fused_hat (both coords)/
  fused_geometric_gaussian/fused_simple_gaussian parity tests). No
  accept-and-run gap found.
- Rejection audit: SG non-coherent (constructibility message +
  intensity-entry family-legality message), multi-source, 1-frequency,
  irregular grid, one-range, unequally spaced ranges all still rejected
  with unchanged messages; one gap closed — new solver-level non-TL
  reject (RayTrace) asserts "fused ray-reuse solver requires a
  transmission-loss run mode" through the existing
  fused-prefix/distinctness checks.
- Support-matrix docs: REFERENCE_FEATURE_SUPPORT_MATRIX.md gains "fused
  execution-mode 支持域" (common gate + family×run-type×run-mode table +
  subset principle, incl. Hat-C/GeoGaussian irregular-legacy contrast
  and SG product-law note); GUIDE_USAGE.md fused bullet/deprecation/
  支持域 paragraph updated; README.md stale IGR-2-only paragraph and
  CC-pressure-layout bullet fixed. No acceptance overclaim ("当前实现",
  not "已验收"); IGR-3B remains not-current. A08 evidence-level doc pass
  follows.
- Release build zero warnings (warnings-as-errors ON); full ctest 48/48
  green (reuse 10, parallel 2, component 38, unit 10); --help smoke
  quoted (coordinator re-verified personally); ./bellhop_rayreuse_fused_
  ray_reuse_solver_tests PASS; git diff --check clean; no >80-col lines;
  no formatter run (A01 finding stands). No src/include production edit
  in A07 (main.cpp is the CLI app surface); legacy nonreuse/reuse/
  parallel paths untouched. Pre-existing note recorded: CLI-level non-TL
  routing strings have no automated assertion (outside CTest beyond the
  rayreuse.cli.* smoke tests); left as-is per minimal-scope discipline.

### A08 [STANDARD] PRT, statistics, docs
Status: DONE
Reviewer: N/A (STANDARD, reviewer-on-need per AGENTS.md §5; coordinator
spot-check performed personally: ctest 48/48 after the docs diff,
support-matrix statistics section + header status changes verified,
docs-only diff confirmed)

Acceptance:

- Statistics envelope applicability documented per family (design §3.4);
  PRT output correct for all families and all fused modes — the PRT run-mode
  lines ("Incoherent TL calculation", "Semi-coherent TL calculation") are
  reproduced legacy-exactly for fused runs (mode reporting, design §3.4);
  no overclaim in docs.
- IGR-3A documentation updated to evidence level (design + results).

Evidence:

- PRT verification matrix (16 kernel-config runs = the 13 family x mode
  combos, Hat counted once per mode across both coords; fresh runs with the
  A07 release binary, artifacts /tmp/igr3a_a08/cases/): every combo's fused
  PRT run-mode line is legacy-exact ("Coherent TL calculation" /
  "Incoherent TL calculation" / "Semi-coherent TL calculation", matching
  case.toml prt_markers) and every fused PRT carries "execution mode =
  broadband fused reuse". Coverage: CC C/I/S (munk_cerveny_cc 50,250 Hz;
  incoherent_direct / semicoherent_direct), RC C/I/S
  (ray_centered_component_pressure env + derived IR/SR, 1000,2000 Hz),
  Hat-C and Hat-RC C/I/S (standard envs + derived Ig/Sg; 1000,2000 /
  100,200 Hz), GeoGaussian C/I/S (standard envs, 1000,2000 Hz), SG-C
  (simple_gaussian_cartesian, 1000,2000 Hz).
- PRT diff surface: normalized reuse-vs-fused diff (masking only the fused
  executor block — execution-mode line, range parallel, requested/effective
  range worker count, wall-seconds label — and numeric seconds values) is
  EMPTY for all 16 runs; raw diff shows exactly that block + timing values.
  Config summary (incl. run-mode/family lines), Trace passes, ray counts,
  and cache fingerprints before/after byte-identical between reuse and
  fused; SHD reuse==fused cmp IDENTICAL all 16; --verify-cache
  before==after stable all 16 (incidental Level A re-check).
- Statistics envelope (source + runtime): --profile-influence CLI-accepted
  only for CervenyGaussian Cartesian TL, execution-mode-independent
  (main.cpp:357-365; RC fused/reuse exit 1 with the documented message); CC
  fused fills the full counter set incl. sub-phase seconds in coherent AND
  incoherent runs except validatedRayPoints/validatedWorkspaceValues (zero
  in fused; increment sites only in the legacy public per-frequency
  entries). RC/Hat/GeoGaussian/SimpleGaussian fused entries accept-and-
  uncount (static_cast<void>(statistics); executor passes nullptr when
  collectStatistics off).
- Docs (docs-only diff): REFERENCE_FEATURE_SUPPORT_MATRIX.md fused section
  gains "fused PRT 模式报告与 Influence 统计 envelope（IGR-3A A08）" (PRT
  diff-surface statement, --profile-influence applicability, per-family
  counter envelope incl. the two zero CC counters); README.md profiling
  section states the CC-only scope + matrix link; DESIGN_IGR3 and worklist
  headers updated PRE-CONSTRUCTION -> construction-in-progress (A01-A08
  constructed, A09 + Batch Acceptance + Final Review pending, nothing
  ACCEPTED). Design §9 table re-checked vs implemented fused domain —
  matches, no frozen-text correction needed.
- Validation: ctest build/release 48/48 passed after the docs diff; git
  diff --check clean; no production source edit; reference implementations
  untouched; artifacts throwaway in /tmp/igr3a_a08/ (not committed).
- Observation (record-keeping, no action): A07's evidence says "all 13
  combos" enumerating Hat-C/Hat-RC separately (= 16 kernel-config x mode
  pairs); the 13-count follows the design §9 row convention (Hat = one
  family row across both coords).

### A09 [ADVANCED] Full regression + benchmark (batch acceptance prep)
Status: DONE
Reviewer: PASS (2026-09-04; independent checkpoint performed by the
final-reviewer as read-only fallback — the reviewer agent type was
unavailable in the environment (model-provider failure on dispatch, two
attempts); substitution per the A01/A02 precedent and the user's
checkpoint rule. Verdict "A09 CHECKPOINT: PASS" with all items
independently verified: 11 profile files match frozen values byte-level;
harness companion-staging fix minimal (1 import + 1 call mirroring the
two existing sites); spot-runs personally executed (ctest 48/48 clean
build, test-unit 177 OK, fused 16F three-party PASSED, pytest 192,
binary sha256 re-hashed and matching the record); benchmark JSON
programmatically re-derived for hash identity, 12-config inventory
confirmed with zero substitution; munk exceedance decision assessed
within coordinator authority — no mandated gate weakened, global 1e-3
tolerance untouched, no case-local override added)

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

- Shared-test infra (design §11 Level F): `broadband_regression` 16F
  profile added to 11 cases (frozen I/S values: *_direct 50-500 Hz x16,
  the four 1000-Hz-class I/S cases 1000-2500 Hz x16; documented coherent
  choices: RC/geometric-gaussian/simple-gaussian 1000-2500 Hz x16, both
  hat boundary cases 100-250 Hz x16 — start = case single freq, x2.5 span
  mirrors the frozen 1000->2500 rule, covers smoke span; closes the A04
  hat-Cartesian >=2F gap). munk_cerveny_cc's pre-existing 50-500 x16 used
  unchanged. Oracle regeneration + three-party validation on every new
  profile BEFORE the benchmark run.
- Phase-1 three-party on all new profiles (clean binary, origin oracle,
  tolerance 1e-3 dB, rayreuse gated on all 16 slices x nonreuse/reuse/
  fused, f2cpp fmax-gated per D-02): all PASS; worst rayreuse-vs-origin
  TL diff 1.755e-04 dB (simple_gaussian @2000 Hz); six I/S worst
  7.63e-06 dB; three-mode SHD byte-identical per case. f2cpp non-fmax
  diagnostics ~2.2 dB on the two *_direct cases only (documented D-02 fan
  replan, non-gating).
- munk_cerveny_cc 16F strict origin compare exceeds tolerance at 14/16
  non-fmax slices (worst 1.118e-02 dB @200 Hz; fmax 3.05e-04 passes; f2cpp
  identical values; smoke 7.9e-04 / single 2.5e-04 controls pass) —
  pre-existing Origin<->C++ binary32 near-caustic class (munk_spline 5e-3
  / q_control 2e-2 precedents), outputs byte-identical to the 38137a4
  baseline (f01ee481...). Coordinator decision (2026-09-04): munk 16F
  remains benchmark/hash-gated (its role since A02); no tolerance override
  added; mandated A09 three-party set = the six I/S cases, all PASS; the
  exceedance is recorded, not tuned. Documented path if strict 16F origin
  compare is ever required: case-local tolerances.toml per the q_control
  precedent.
- Clean build: build/igr3a-clean (release cache vars, warnings-as-errors
  ON, zero warnings); exe sha256 5aeebe780c750bc4b8b0a98121d7adf27b618530
  f8a15e4cc662028631c050c0 == build/release rebuild of the same tree.
- Phase-2 gates: ctest 48/48 (100%); uv run pytest 192 passed; make
  test-unit 177 OK; model_matrix broadband_smoke all-eligible 49/50 PASS
  at default tolerances (all 12 batch cases PASS, cross-mode
  byte-identical; q_range_independent_control passes with its established
  0.02 dB tolerances_i5_q_control override — MODEL MATRIX PASSED) +
  single x6 snapshot cases MODEL MATRIX PASSED; make f2cpp-regression:
  F2CPP ctest 37/37 + case set PASSED rc=0; git diff --check clean.
  (Full gate-script defaults unrunnable: compact snapshots cover 6/60
  cases — pre-existing, recorded; matrix executed as the two recorded
  sub-invocations.)
- Level F benchmark (IGR3A_A09_PERF.md; Apple M4 10 cores, 1 warmup + 3
  measured, sequential session; JSON build/benchmarks/igr3a_a09_level_f.
  json): 12 configs (6 coherent + 6 I/S) x reuse + fused w1/2/4/8, every
  config on its broadband_regression 16F profile.
  require_identical_sample_hashes + cross-configuration identity enforced
  PASS on all 12 (munk reproduces the A02 baseline hash f01ee481...; hat
  pair effective workers clamp 8->3). Median wall: munk 96.27/84.31/45.16/
  34.35/23.66 s (reuse/w1/w2/w4/w8; fused w1 -12.5% vs reuse, w8 4.07x);
  CC I/S direct 0.38->0.30 s at w4; 11 micro cases fixed-cost dominated.
  Session note: munk w4 median +24% vs the A02 record with identical bits
  (machine-state variance, not a solver change); w1/w2/reuse match A02
  within 0.2%.
- Harness infra fix (recorded): benchmark_rayreuse.py _run_isolated_sample
  now stages companion .ati/.bty via stage_companion_files (first attempt
  aborted on the hat boundary cases — companion staging had only ever
  been exercised on companion-free munk; results of the aborted attempt
  fully discarded); harness unittests 177/177 OK after the change.
- Coordinator spot-checks (personally run 2026-09-04): ctest on
  build/igr3a-clean 48/48; git diff --check clean; fresh fused three-party
  validation of geometric_hat_incoherent broadband_regression 16F PASSED;
  benchmark JSON + IGR3A_A09_PERF.md present; untracked files enumerated
  (five new parity tests + the A09 perf doc — all intended for commit).

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

## 5. Final Review record (2026-09-04)

- First round (final-reviewer instance A): performed the A09 independent
  checkpoint as reviewer-substitution ("A09 CHECKPOINT: PASS" — 11 profile
  files byte-verified, minimal harness fix, personally executed ctest
  48/48 / test-unit 177 / pytest 192 / fused 16F three-party / binary
  re-hash, benchmark JSON hash-identity re-derivation, munk exceedance
  decision assessed within coordinator authority) and the batch Final
  Review: all technical, evidence, protocol, cache, scope, and hygiene
  checks PASSED; verdict CHANGES_REQUIRED on a single LOW doc-level
  finding (stale construction-status headers in the worklist and design
  doc contradicting the authoritative per-task records).
- Remediation (coordinator): both headers updated to the live state; the
  A09 Reviewer line recorded the substitution verdict. No other content
  touched.
- Closing re-review (fresh final-reviewer instance B substituting the
  non-resumable first-round reviewer — environment model-provider failure;
  substitution recorded): verified the finding CLOSED, remediation scoped
  (design-doc diff a single header hunk; 33-file batch payload otherwise
  identical; git diff --check clean). Verdict: **ACCEPTED**. IGR-3A
  CLOSED. IGR-3B construction not started.
