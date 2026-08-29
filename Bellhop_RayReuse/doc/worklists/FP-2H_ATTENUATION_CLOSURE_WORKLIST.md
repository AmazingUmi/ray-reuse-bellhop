# FP-2H Attenuation Closure Worklist

Batch: `FP-2H`
Phase: `ACCEPTED / CLOSED`
Design state: `CONSTRUCT COMPLETE / ACCEPTED`
Implementation state: `CONSTRUCT COMPLETE`
Predecessors: `FP-2F ACCEPTED`, `FP-2G ACCEPTED`

This Worklist is the execution authority for FP-2H after coordinator FREEZE.
Changing scientific semantics, ownership, cache behavior, oracle thresholds, or
scope requires reopening DESIGN with an architect.

## 1. Goal

Close RayReuse attenuation parity for:

- ATT-01: `N`, `F`, `M`, `W`, `Q`, and `L` attenuation units.
- ATT-02: protect current-frequency/current-sound-speed `W` semantics.
- ATT-03: protect existing Thorp (`T`) behavior against migration regressions.
- ATT-04: implement parameterized Francois–Garrison (`F`) volume attenuation.
- ATT-05: implement biological (`B`) volume attenuation with overlapping layers.
- Water-column projection through all five frequency SSP backends.
- Acoustic and elastic half-space attenuation during boundary projection.
- `nonreuse`, `reuse`, and `parallel` broadband execution parity.
- Frozen-cache immutability and fingerprint compatibility.

Parity declarations require executable Origin, F2CPP, and RayReuse product
evidence. Parser acceptance or unit tests alone cannot close an ATT item.

## 2. Scope

### 2.1 In scope

1. Add F2CPP-compatible volume-attenuation parameter ownership to the RayReuse
   `Environment`.
2. Port Origin/F2CPP attenuation formulas, compile constraints, and operation ordering.
3. Extend ENV parsing and PRT reporting for `T`, `F`, and `B`.
4. Thread volume attenuation through:
   - C-linear frequency SSP;
   - N2-linear frequency SSP;
   - PCHIP frequency SSP;
   - cubic-spline frequency SSP;
   - quadrilateral frequency SSP;
   - `FrequencySspEvaluator`;
   - `FrequencyProjector`;
   - acoustic/fluid/elastic boundary projection.
5. Preserve the existing RayReuse raw attenuation model tag and its existing
   fingerprint representation.
6. Add RayReuse eligibility and three-party validation to the existing ATT-01,
   ATT-04, and ATT-05 standard cases.
7. Revalidate ATT-02 and ATT-03 without changing their scientific semantics.
8. Publish evidence-limited support/status documentation.

### 2.2 Explicitly out of scope

- Lowercase `m` power-law ENV syntax or new power-law parameters.
- New attenuation models beyond None, Thorp, Francois–Garrison, and biological.
- Changes to Origin or F2CPP source, tests, cases, or build products.
- Changes to `RayPath`, `RayPathCache`, `ReflectionEvent`, or ray-state layouts.
- Changes to `RayPathCache::contentFingerprint()` input order, byte encoding,
  FNV-1a algorithm, or hash constants.
- Persisting complex speed, attenuation, reflection coefficients, travel-time
  phase, Arrival, or Eigenray state in frozen geometry.
- A global “current frequency” or shared mutable attenuation state.
- Persistent cross-environment cache keys or cache serialization.
- Attenuation-specific performance caches.
- New per-version ENV templates where the shared `origin.env.in` is accepted.
- Broad product-parity claims for SSP/boundary/product combinations not covered
  by the acceptance matrix below.
- Unrelated parser, geometry, writer, CLI, or documentation refactors.

## 3. Frozen architecture decisions

### 3.1 Environment-level ownership

Add the F2CPP data model to
`Bellhop_RayReuse/include/rayreuse/model/environment.hpp`:

- `FrancoisGarrisonParameters`, value-owned by `VolumeAttenuation`.
- `BiologicalAttenuationLayer`.
- `BiologicalAttenuationLayers`.
- `SharedBiologicalAttenuationLayers =
  std::shared_ptr<const BiologicalAttenuationLayers>`.
- `VolumeAttenuationParameters`, using a variant of:
  - `std::monostate`;
  - `FrancoisGarrisonParameters`;
  - immutable shared biological layers.
- `VolumeAttenuation`.
- `Environment::volumeAttenuation()`.

The `Environment` constructor receives a final defaulted
`VolumeAttenuation = {}` argument so existing source callers remain valid.

Biological parsing builds a private vector and publishes it only through a
`shared_ptr<const ...>`. No mutable alias may escape the parser.

### 3.2 Legacy raw tag and exactly-once resolution

RayReuse already stores:

`RawAttenuation::volumeModel`

and fingerprints that field in `RayPathCache::contentFingerprint()`. It remains in
its existing field position and continues to act as:

1. a backward-compatible conversion tag for legacy overloads; and
2. a frozen cache / material compatibility tag.

> **保留 legacy raw tag 的原因**：保持既有 frozen `RayPathCache` /
> `contentFingerprint()` compatibility（该 tag 已参与现有哈希序列），不能在
> 本批无证据删除或修改字段位置。

It does not own Francois–Garrison or biological parameters.

**Frozen compatibility resolution matrix (绝对禁止 double attenuation)**:

| Raw tag (`RawAttenuation.volumeModel`) | Environment model (`Environment::volumeAttenuation()`) | Expected behavior |
|---|---|---|
| `None` | `None` | base attenuation only |
| `Thorp` | `None` | base + Thorp |
| `None` | `Thorp` | base + Thorp |
| `Thorp` | `Thorp` | base + Thorp exactly once |
| `Thorp` | `FrancoisGarrison` | reject (`ValidationError`) |
| `Thorp` | `Biological` | reject (`ValidationError`) |
| `FrancoisGarrison` | `None` | reject missing parameters (`ValidationError`) |
| `Biological` | `None` | reject missing parameters (`ValidationError`) |
| `FrancoisGarrison` | `FrancoisGarrison` | base + FG exactly once |
| `Biological` | `Biological` | base + Biological exactly once |

Resolution rules:

- Legacy conversion overloads continue to resolve the raw tag.
- New overloads receive explicit `VolumeAttenuation` and evaluation depth.
- Raw tag `None` plus explicit model uses the explicit model.
- Explicit model `None` plus a non-None raw tag preserves legacy behavior.
- Matching non-None raw and explicit models apply volume loss exactly once.
- Conflicting non-None models throw `ValidationError`.
- A raw `F` or `B` tag without matching parameter ownership in `Environment` throws `ValidationError`.
- Parser-produced SSP points and acoustic materials stamp the selected model
  into the legacy tag while also storing its parameters in `Environment`.

The ordinal order of `VolumeAttenuationModel` remains:

`None`, `Thorp`, `FrancoisGarrison`, `Biological`.

### 3.3 Fingerprint contract

`RayPathCache::contentFingerprint()` remains the canonical little-endian
FNV-1a mutation detector for frozen ray content.

FP-2H shall not:

- add `Environment::volumeAttenuation()` payloads to the fingerprint;
- remove or reorder existing `RawAttenuation` fingerprint inputs;
- alter the fingerprint source implementation;
- use the fingerprint as a complete cross-environment compatibility key.

Two Francois–Garrison or biological parameter payloads may share a frozen
geometry fingerprint by design. Environment identity remains external to the
cache. A future persistent cross-environment cache key is separate scope.

### 3.4 Scientific semantics

Port F2CPP/Origin constants, literal widths, branches, and accumulation order.

Base-unit conversions remain:

- `N`: value in Np/m.
- `M`: value divided by `8.6858896`.
- `F`: `value * frequencyHz / (1000 * 8.6858896)`.
- `W`: current-frequency loss using the current conversion sound speed.
- `Q`: angular frequency divided by `2 * soundSpeed * Q`.
- `L`: loss parameter times angular frequency divided by sound speed.

Francois–Garrison numerical & compile contract:

- Clang compile option: `src/acoustics/attenuation.cpp` must be compiled with
  `-fno-builtin-pow` on AppleClang/Clang to prevent `std::pow(10.0, x)` from being
  lowered to `exp10(x)`, ensuring exact 0 ULP libm pow parity with gfortran/F2CPP.
- Temperature branch: `temperature < 20.0` uses the cold viscosity polynomial,
  `temperature >= 20.0` (including 20.0 C) uses the warm viscosity polynomial.
- Nested `std::fma` targets matching F2CPP:
  - Magnesium pressure: `P2 = std::fma(6.2e-9, meanDepthSquared, std::fma(-1.37e-4, meanDepth, 1.0));`
  - Viscosity pressure: `P3 = std::fma(4.9e-10, meanDepthSquared, std::fma(-3.83e-5, meanDepth, 1.0));`
  - Viscosity coefficient `A3`:
    `temperature < 20.0 ? std::fma(-1.5e-8, temperatureCubed, std::fma(9.11e-7, temperatureSquared, std::fma(-2.59e-5, temperature, 4.937e-4))) : std::fma(-6.5e-10, temperatureCubed, std::fma(1.45e-7, temperatureSquared, std::fma(-1.146e-5, temperature, 3.964e-4)));`

Other invariants:

- Imaginary sound speed is positive:
  `alphaNpPerM * cReal² / (2*pi*frequency)`.
- Base and volume losses are additive and are applied exactly once.
- Thorp preserves binary32-promoted constants, including `0.11F` and
  `8685.8896F`.
- Francois–Garrison uses its parameter `meanDepthMeters`; the biological
  evaluation depth is a separate runtime input.
- Francois–Garrison preserves F2CPP/Origin literal promotion, branch, `fma`,
  and operation ordering.
- Biological layer endpoints are inclusive.
- Overlapping biological layers are valid and additive.
- Each matching biological layer is converted from dB/km to Np/m before it is
  accumulated, preserving Origin last-bit behavior.
- Zero biological layers are valid; more than 200 are rejected.
- Biological resonance frequency and quality factor are positive.
- Biological attenuation coefficients are non-negative.
- Francois–Garrison temperature exceeds `-273 C`; salinity and mean depth are
  non-negative; all parameters are finite.

### 3.5 SSP projection ordering

For every frequency SSP backend:

1. retain real, frequency-independent SSP geometry;
2. convert attenuation at each tabulated SSP node using that node’s depth;
3. form the node’s complex sound speed;
4. perform the backend’s existing complex interpolation/evaluation.

Biological loss must not be reevaluated after interpolation at arbitrary query
depths.

For quadrilateral SSP, attenuation conversion uses the reference ENV node real
sound speed, not a range-dependent value from the Q matrix. The Q matrix
continues to provide the range-dependent real field.

Existing lossless and Thorp operation order must remain stable.

### 3.6 Boundary projection

`evaluateFluidHalfSpaceAcoustics`,
`evaluateAcousticHalfSpaceAcoustics`, and the boundary dispatcher receive:

- explicit `VolumeAttenuation`;
- attenuation evaluation depth;
- current frequency.

The model applies to acoustic material compressional and, where present,
shear attenuation exactly as in F2CPP.

Depth is:

- the boundary/material evaluation depth for ordinary materials;
- `BoundaryModel::materialAttenuationDepthAtSegment()` for long materials;
- the legacy `1.0e20` value where the existing long-material contract uses it.

Grain-size conversion continues to use an empty volume model. Volume
attenuation must not be added to the derived grain-size loss.

No complex material or reflection result is written to `ReflectionEvent` or
`RayPath`.

### 3.7 Frequency-local and concurrency contract

- Geometry tracing remains real and frequency-independent.
- `FrequencyProjector::project()` remains const.
- `Environment` volume model and parameter data are immutable while solver and projection run.
- Frequency workers do not share mutable frequency-local state; each frequency owns its complex SSP, travel-time, amplitude, phase, reflection, Arrival, and Eigenray state.
- Sequential low/high/low projection is deterministic.
- Parallel projection reads only immutable environment and frozen geometry, with deterministic ordered product publishing.
- `nonreuse`, `reuse`, and `parallel` execution modes produce byte-identical SHD products.
- No frequency-local result is written into `RayPathCache`.

## 4. Ordered tasks and dependencies

Execution order is:

```text
H00
→ H01/R
→ H02/R
→ H03/R
→ H04/R
→ H05/R
→ H06/R
→ H07
→ H08
→ H09
→ Batch Acceptance
→ H10
→ Final Review
```

Dependencies rule:

- `Batch Acceptance` requires tasks **H00–H09 DONE** (and all reviewer checkpoints H01-R～H06-R `PASS`).
- `H10` depends on `H09 + successful Batch Acceptance` (H10 performs final documentation/matrix/status closure).
- `Final Review` occurs after `H10`.
- No circular dependency between `H10` and `Batch Acceptance`.

### H00 [STANDARD] [GENERAL] Freeze pre-construction regressions

Status: `DONE`
Reviewer: `N/A`
Depends on: none

Work:

- Build or identify a clean pre-FP-2H RayReuse executable.
- Record Git revision, executable SHA-256, compiler, and build options.
- Capture current Thorp unit anchors and:
  - `constant_speed_thorp/single`;
  - `constant_speed_thorp/broadband_smoke`;
  - `constant_speed_thorp/broadband_regression`.
- Capture current `attenuation_unit_w` 5 kHz and 4/5 kHz behavior.
- Record full SHD hashes, PRT markers, trace counts, and cache fingerprints.
- Preserve existing frozen SSP/cache anchors, including `munk_spline`
  fingerprint `1526667602348633172`.
- Store generated products outside the repository; summarize hashes in the
  eventual FP-2H work report.

Acceptance:

- Evidence is captured before H01 production edits.
- The baseline executable and source revision are unambiguous.
- No generated SHD, PRT, ENV, build, or temporary files enter the batch diff.

Evidence:
- Git revision: `c40a4ee01ef8a790a5d6d1814b4b24795ea0f083`
- Pre-build SHA-256: `8dc2c8c24b2977d45af6a33d95987be3a42364687fe829a486c890eb05e3ca9c` (AppleClang 21.0.0, Release, `-DRAYREUSE_WARNINGS_AS_ERRORS=ON`)
- Thorp single 5000Hz SHD SHA: `27450009cbc6861ffc8f89e127432c09c852ca34af47e8a057e7d218db3f48ea`
- Thorp broadband_smoke 1000/5000Hz SHD SHA: `1ddd8171315750ddf754136191bc9a7aa3e5b0747cc0b43286acabc74305a7f3`
- Thorp broadband_regression 16-freq SHD SHA: `c8ef3fad90e32753991b021eb804f4f26046c04783ab50a602a2983f26dcbcd2`
- Thorp reuse `--verify-cache` fingerprint: `12163770556679950120` (before==after)
- Munk spline broadband_smoke SHD SHA: `74028065178ff80d43755ef2ba70ba5ba3e4947574a37a4154a7ecc52eef1596`, fingerprint: `1526667602348633172` (before==after)
- W single 5000Hz SHD SHA: `1263c208ff5ed1395c5f28991cb045c1a1b63fddc0055c2092f077645f373951`
- W broadband_smoke 4000/5000Hz SHD SHA: `891306fec4e1c936bcdd059c3c522a45effba5770a685aa1504332fe7a2369a4`, fingerprint: `10638750469126791633` (before==after)
- Unit tests: `rayreuse.unit.attenuation` PASSED.
- Products stored in `/tmp/fp2h_pre_products` and `/tmp/fp2h_pre_verify`. Git working tree clean.

### H01 [ADVANCED] [ADVANCED] Add immutable volume-attenuation ownership

Status: `DONE`
Reviewer: `PASS — H01-R`
Depends on: H00

Likely files:

- `Bellhop_RayReuse/include/rayreuse/model/environment.hpp`
- `Bellhop_RayReuse/src/model/environment.cpp`
- `Bellhop_RayReuse/tests/unit/core_types_test.cpp`

Work:

- Add the data model from §3.1.
- Add a defaulted final `Environment` constructor argument.
- Add a const accessor.
- Preserve all existing `RawAttenuation` fields and enum ordinals.
- Validate model/parameter variant consistency at the earliest stable seam
  without adding mutable state.
- Verify copied environments share biological layers immutably.
- Preserve existing programmatic environments through default construction.

Acceptance:

- Existing callers compile without bulk constructor rewrites.
- None and Thorp carry `monostate`.
- Francois–Garrison carries its value payload.
- Biological carries a non-null immutable shared layer vector.
- Default environments remain lossless unless a legacy raw tag requests loss.
- No `RayPath`, cache, reflection-event, or solver-state schema changes.

Evidence:

- Targeted core-type/model tests: `rayreuse.unit.core_types` PASSED.
- All 10 unit tests compiled with warnings-as-errors (`-Werror`) and passed.
- Reviewer H01-R checked ownership, lifetime, default API, and protected schemas: PASS.

### H02 [ADVANCED] [ADVANCED] Port ENV parser and PRT reporting

Status: `DONE`
Reviewer: `PASS — H02-R`
Depends on: H01-R `PASS`

Likely files:

- `Bellhop_RayReuse/src/io/environment_parser.cpp`
- `Bellhop_RayReuse/app/main.cpp`
- `Bellhop_RayReuse/tests/component/environment_parser_test.cpp`

Work:

- Accept six-character padded top/SSP options with exact F2CPP/Origin grammar:
  ```text
  topOptions[0] = SSP interpolation: C / P / N / S / Q
  topOptions[1] = surface: V / R / A / G / F
  topOptions[2] = attenuation unit: N / F / M / W / Q / L
  topOptions[3] = volume attenuation: blank / T / F / B
  topOptions[4] = topography: blank / ~ / *
  topOptions[5] = blank only
  ```
- Continue accepting `N/F/M/W/Q/L`; do not add lowercase `m` ENV support.
- Parse Francois–Garrison’s four values in Origin/F2CPP order:
  temperature (Celsius), salinity (PSU), pH, and mean depth (meters).
- Parse biological layer count and five values per layer in Origin/F2CPP order:
  minimum depth (m), maximum depth (m), resonance frequency (Hz), quality factor, attenuation coefficient (dB/km).
- Allow 0–200 biological layers and overlapping intervals.
- Reject malformed counts, parameter variants, invalid ranges, non-finite
  values, invalid quality factors, and excess layers (>200).
- Populate both `Environment::volumeAttenuation()` and the legacy raw tags.
- Replace PRT inference from the first SSP node with environment-level model
  reporting.
- Preserve required markers:
  - `THORP volume attenuation added`
  - `Francois-Garrison volume attenuation added`
  - `Biological attenaution`
  - `Number of Bio Layers =`

Acceptance:

- Existing 3–5 character options remain accepted.
- The sixth padded position must obey the F2CPP/Origin grammar.
- Shared `origin.env.in` templates parse directly in RayReuse.
- Malformed or conflicting inputs fail with deterministic parser context.
- The historical `attenaution` spelling remains because it is an oracle marker.
- No new `f2cpp.env.in` or `rayreuse.env.in` files are introduced unless a
  reviewed parser blocker proves the shared template unusable.

Evidence:

- Focused positive and negative parser tests, including 0, 200, and 201 layers.
- Tests proving overlapping layers are accepted.
- PRT marker checks with newline termination.
- Legacy tags stamped on SSP and acoustic materials (compressional and shear).
- `rayreuse.component.environment_parser` PASSED.
- Reviewer H02-R re-validation: PASS.

### H03 [ADVANCED] [ADVANCED] Port attenuation kernels and compatibility resolution

Status: `DONE`
Reviewer: `PASS — H03-R`
Depends on: H02-R `PASS`

Likely files:

- `Bellhop_RayReuse/CMakeLists.txt`
- `Bellhop_RayReuse/include/rayreuse/acoustics/attenuation.hpp`
- `Bellhop_RayReuse/src/acoustics/attenuation.cpp`
- `Bellhop_RayReuse/tests/unit/attenuation_test.cpp`

Work:

- Add compile option `-fno-builtin-pow` for `src/acoustics/attenuation.cpp` on AppleClang/Clang in `CMakeLists.txt`.
- Add explicit conversion overloads accepting environment volume attenuation
  and evaluation depth.
- Preserve delegating legacy overloads.
- Implement the exactly-once/conflict rules in §3.2.
- Port Francois–Garrison (with `temperature < 20.0` branch and nested `std::fma` expressions for P2, P3, A3) and biological kernels from F2CPP.
- Preserve N/F/M/W/Q/L and Thorp calculations exactly.
- Validate all scientific inputs before returning a result.
- Preserve positive-imaginary-speed convention and excessive-loss rejection.

Acceptance:

- Formula anchors match F2CPP within the same last-bit/declared precision
  expected by its unit tests.
- ATT-01 equivalent 5 kHz inputs produce bit-identical conversion results.
- `W` changes correctly with frequency and conversion sound speed.
- Thorp pre-construction anchors remain exact.
- FG anchors cover both sides of the 20 C viscosity branch.
- Biological anchors cover:
  - outside any layer;
  - inclusive lower and upper boundaries;
  - overlapping layers;
  - resonance;
  - zero layers;
  - 200 layers;
  - rejection of 201 layers.
- Matching raw/global model loss is applied once.
- Conflicting raw/global models are rejected.
- Legacy raw Thorp callers remain behaviorally unchanged.

Evidence:

- `rayreuse.unit.attenuation` PASSED.
- Clang/AppleClang `-fno-builtin-pow` compile option applied to `src/acoustics/attenuation.cpp`.
- Direct comparison to F2CPP unit anchors and Origin formula constants.
- Reviewer H03-R: PASS.

### H04 [ADVANCED] [ADVANCED] Wire all frequency SSP backends

Status: `DONE`
Reviewer: `PASS — H04-R`
Depends on: H03-R `PASS`

Likely files:

- `Bellhop_RayReuse/include/rayreuse/model/c_linear_frequency_ssp.hpp`
- `Bellhop_RayReuse/src/model/c_linear_frequency_ssp.cpp`
- `Bellhop_RayReuse/include/rayreuse/model/n2_linear_frequency_ssp.hpp`
- `Bellhop_RayReuse/src/model/n2_linear_frequency_ssp.cpp`
- `Bellhop_RayReuse/include/rayreuse/model/pchip_frequency_ssp.hpp`
- `Bellhop_RayReuse/src/model/pchip_frequency_ssp.cpp`
- `Bellhop_RayReuse/include/rayreuse/model/cubic_spline_frequency_ssp.hpp`
- `Bellhop_RayReuse/src/model/cubic_spline_frequency_ssp.cpp`
- `Bellhop_RayReuse/include/rayreuse/model/quadrilateral_frequency_ssp.hpp`
- `Bellhop_RayReuse/src/model/quadrilateral_frequency_ssp.cpp`
- `Bellhop_RayReuse/include/rayreuse/model/sound_speed_evaluator.hpp`
- `Bellhop_RayReuse/src/model/sound_speed_evaluator.cpp`
- corresponding five SSP component tests
- `Bellhop_RayReuse/tests/component/sound_speed_evaluator_test.cpp`

Work:

- Add explicit volume-model constructor/factory paths.
- Keep existing APIs through delegating overloads/defaults where practical.
- Convert attenuation node-first at each node depth.
- Preserve each backend’s existing interpolation and derivative semantics.
- Preserve quadrilateral reference-node sound-speed behavior for attenuation.
- Keep geometry SSP evaluators real and frequency-independent.

Acceptance:

- C, N, P, S, and Q backends all exercise None, Thorp, FG, and biological
  volume paths through focused tests.
- Biological depth tests distinguish nodes inside, outside, and on layer
  endpoints.
- No backend applies biological loss at post-interpolation query depth.
- Q attenuation does not use a Q-matrix speed for conversion.
- Lossless and Thorp backend anchors remain unchanged.
- Low/high/low repeated evaluation is deterministic.
- No frequency-local object is stored in shared geometry.

Evidence:

- `rayreuse.component.c_linear_ssp` PASSED.
- `rayreuse.component.n2_linear_ssp` PASSED.
- `rayreuse.component.pchip_ssp` PASSED.
- `rayreuse.component.cubic_spline_ssp` PASSED.
- `rayreuse.component.quadrilateral_ssp` PASSED.
- `rayreuse.component.sound_speed_evaluator` PASSED.
- Reviewer H04-R: PASS.

### H05 [ADVANCED] [ADVANCED] Wire projector and boundary attenuation

Status: `DONE`
Reviewer: `PASS — H05-R`
Depends on: H04-R `PASS`

Likely files:

- `Bellhop_RayReuse/include/rayreuse/acoustics/boundary_acoustics.hpp`
- `Bellhop_RayReuse/src/acoustics/boundary_acoustics.cpp`
- `Bellhop_RayReuse/src/field/frequency_projector.cpp`
- `Bellhop_RayReuse/tests/unit/boundary_acoustics_test.cpp`
- `Bellhop_RayReuse/tests/component/frequency_projector_test.cpp`

Work:

- Pass environment volume attenuation into the frequency SSP evaluator.
- Add explicit volume/depth overloads to boundary acoustics.
- Preserve legacy overloads through default empty-volume delegation.
- Apply volume attenuation to acoustic compressional and shear material
  conversion.
- Use frozen event/material evaluation depth, including long-material depth.
- Keep grain-size conversion on an empty global volume model.
- Ensure the projector only reads `RayPath` and environment state.

Acceptance:

- Water-column FG/biological effects appear in projected complex travel time.
- Acoustic and elastic boundary coefficients respond to frequency and
  biological evaluation depth.
- Compressional and shear paths are both covered.
- Long material depth is honored.
- Grain-size results remain unchanged by the environment volume model.
- Reprojecting the same path at another frequency does not retain prior
  complex speed or coefficient state.
- Component tests would fail if only the water-column path were wired.

Evidence:

- `rayreuse.unit.boundary_acoustics` PASSED.
- `rayreuse.component.frequency_projector` PASSED.
- Targeted flat/long/elastic material component tests PASSED.
- Reviewer H05-R: PASS.

### H06 [ADVANCED] [ADVANCED] Prove frozen-cache and concurrency invariants

Status: `DONE`
Reviewer: `PASS — H06-R`
Depends on: H05-R `PASS`

Likely files:

- existing cache/projector/solver tests only
- no planned change to cache implementation or ray schemas

Protected files:

- `Bellhop_RayReuse/src/cache/ray_path_cache.cpp`
- `Bellhop_RayReuse/include/rayreuse/cache/ray_path_cache.hpp`
- `Bellhop_RayReuse/include/rayreuse/ray/ray_path.hpp`
- frequency-independent ray-state schemas

Work:

- Add tests that fingerprint a cache before and after sequential projection.
- Repeat with reuse and parallel projection.
- Verify low/high/low deterministic projection.
- Verify changing FG/biological payload values does not mutate geometry.
- Verify model payloads are not fingerprint inputs.
- Preserve existing raw-tag hash behavior for cached acoustic materials.
- Recheck representative frozen fingerprints and geometry traces.

Acceptance:

- Protected cache/fingerprint implementation files have zero diff.
- `contentFingerprint()` before equals after for every tested projection.
- Existing fingerprint algorithm/order is unchanged by source inspection.
- Different parameter payloads can project differently while sharing frozen
  geometry.
- No claim of equal fingerprints is made where a cached raw model tag itself
  legitimately differs.
- Environment volume attenuation state is immutable during solver and projection execution.
- Frequency workers do not share mutable frequency-local state.
- Repeated parallel execution is deterministic and bit-identical.
- Existing `munk_spline` fingerprint remains `1526667602348633172`.
- Existing representative C/P/N/S/Q geometry and SHD baselines remain exact.

Evidence:

- `rayreuse.reuse.serial_solver` PASSED.
- `rayreuse.parallel.solver` PASSED.
- `rayreuse.component.frequency_projector` PASSED.
- `git diff --exit-code` over protected cache/schema paths PASSED (zero diff).
- `munk_spline` fingerprint `1526667602348633172` verified in C++ test.
- Reviewer H06-R re-validation: PASS.

### H07 [STANDARD] [GENERAL] Close ATT-01 and ATT-02 product evidence

Status: `DONE`
Reviewer: `OPTIONAL (coordinator validated)`
Depends on: H06-R `PASS`

Likely files:

- six `test/standard_cases/cases/attenuation_unit_*/case.toml`
- `test/standard_cases/codes/validate_i4_attenuation_units.py`
- focused Python tests only if new reusable validator logic requires them

Work:

- Add `rayreuse` to all six case compatibility lists.
- Extend the validator with optional RayReuse executable support.
- Require distinct resolved paths and content hashes for all executables.
- Require fresh successful manifests with exact case/profile/frequency identity.
- Validate both:
  - `single`: 5000 Hz;
  - `broadband_smoke`: 4000 and 5000 Hz.
- Use frequency index `index` for a RayReuse broadband SHD and index `0` for
  per-frequency Origin/F2CPP SHDs.
- Perform Origin↔F2CPP, Origin↔RayReuse, and F2CPP↔RayReuse comparisons using
  existing tolerances without relaxation.
- Require bit-identical 5 kHz decoded pressure across N/F/M/W/Q/L independently
  for Origin, F2CPP, and RayReuse.
- Record executable, rendered-input, field, and aggregate SHA-256 values.

Acceptance:

- All six RayReuse cases execute through the standard-case runner.
- Equivalent 5 kHz unit fields are bit-identical within each implementation.
- The 4 kHz slice protects current-frequency `W` semantics.
- Existing PRT attenuation-unit markers remain exact.
- Single-profile shared ENV bytes are identical across implementations.
- Broadband execution is validated semantically despite RayReuse using one
  multi-frequency invocation.
- No tolerance file changes.

Evidence:

- `test/standard_cases/codes/validate_i4_attenuation_units.py` PASSED with Origin, F2CPP, and RayReuse across all 54 pairwise comparisons (42 gating comparisons PASS, 12 non-gating documented).
- Rendered environment consistency verified across implementations.
- Cross-unit 5 kHz fields across N, F, M, W, Q, L are bit-identical for Origin, F2CPP, and RayReuse.
- 4 kHz linear-in-frequency scaling for W, F, Q, L and frequency-independence for N, M verified.
- Pairwise comparisons Origin↔F2CPP, Origin↔RayReuse, and F2CPP↔RayReuse all pass with max TL diff = 0.0 dB (5000 Hz) and 2.28e-5 dB (4000 Hz Origin↔RayReuse).
- All standard cases Python tests PASSED.

### H08 [STANDARD] [GENERAL] Close ATT-03, ATT-04, and ATT-05 product evidence

Status: `DONE`
Reviewer: `OPTIONAL (coordinator validated)`
Depends on: H07

Likely files:

- `test/standard_cases/cases/volume_attenuation_francois_garrison/case.toml`
- `test/standard_cases/cases/volume_attenuation_biological/case.toml`
- `test/standard_cases/codes/validate_i4_volume_attenuation.py`

The existing shared case templates remain:

- `volume_attenuation_francois_garrison/origin.env.in`
- `volume_attenuation_biological/origin.env.in`

Work:

- Add `rayreuse` to the FG and biological compatibility lists.
- Extend the volume validator to RayReuse and include Thorp regression evidence.
- Preserve the existing no-loss control:
  `constant_speed_no_attenuation_5khz`.
- Preserve `MINIMUM_NOOP_DIFFERENCE = 1.0e-6`.
- Validate:
  - Thorp single: 5000 Hz;
  - Thorp smoke: 1000/5000 Hz;
  - Thorp regression: 16 frequencies from 1–10 kHz;
  - FG single: 5000 Hz;
  - FG smoke: 5000/10000 Hz;
  - biological single: 5000 Hz;
  - biological smoke: 2500/5000 Hz.
- Use RayReuse frequency index correctly for shared broadband SHDs.
- Verify all executable identities, freshness, manifests, frequencies, inputs,
  pairwise pressure/TL comparisons, and aggregate hashes.
- Verify FG and biological single fields differ from the lossless control.
- Preserve and validate the overlapping biological oracle.
- Compare Thorp output and hashes to H00 pre-construction evidence.

Acceptance:

- All gating comparison pairs (Origin↔RayReuse across all frequencies, and all pairs at profile fmax) pass existing tolerances.
- Non-gating comparisons (F2CPP comparisons at f < fmax) document the single-frequency D-02 launch-fan divergence.
- RayReuse FG and biological paths are proven non-no-op.
- Biological validation does not reject overlapping layers.
- Thorp unit and product evidence remains unchanged by the model migration (SHA-256 equals H00 baseline).
- Required model PRT markers are present.
- No per-version ENV templates are added without an approved blocker.
- No tolerance changes.

Evidence:

- `test/standard_cases/codes/validate_i4_volume_attenuation.py` PASSED with all 3 executables across all 75 comparison pairs (39 gating comparisons PASS, 36 non-gating documented).
- Rendered environment consistency verified across implementations.
- Thorp output hashes match H00 pre-construction baselines bit-for-bit:
  - single: `27450009cbc6861ffc8f89e127432c09c852ca34af47e8a057e7d218db3f48ea`
  - smoke: `1ddd8171315750ddf754136191bc9a7aa3e5b0747cc0b43286acabc74305a7f3`
  - regression: `c8ef3fad90e32753991b021eb804f4f26046c04783ab50a602a2983f26dcbcd2`
- All non-no-op guards passed (`max_pressure_absolute_vs_lossless` > `1.0e-6`).
- All 187 pytest tests and 172 standard cases test-unit tests PASSED.

### H09 [STANDARD] [GENERAL] Execute mode, trace, and cache evidence matrix

Status: `DONE`
Reviewer: `OPTIONAL`
Depends on: H08

Work:

- Use one isolated results root for three-party nonreuse oracle comparison.
- Use separate isolated results roots for RayReuse `reuse` and `parallel`.
- Run every FP-2H broadband profile in all three RayReuse modes.
- Compare complete RayReuse SHD files byte-for-byte across modes.
- Record expected trace-pass markers:
  - every two-frequency profile: `2 / 1 / 1`;
  - Thorp 16-frequency regression: `16 / 1 / 1`;
  - order is `nonreuse / reuse / parallel`.
- Run `--verify-cache` on representative W, Thorp, FG, and biological
  broadband cases in reuse and parallel modes.
- Require exact before/after cache fingerprint equality.
- Record aggregate hashes for each mode.
- Keep all generated outputs outside the repository.

Acceptance:

- Per-case broadband SHD bytes are identical across RayReuse modes.
- Trace-pass counts prove reuse/parallel trace geometry once.
- Cache verification is enabled and before equals after.
- Repeated parallel runs are byte-identical.
- No stale or cross-mode manifest is accepted as evidence.
- Evidence includes every ATT-01 unit plus T, FG, and biological models.

Evidence (executed with `/tmp/fp2h-pre-build/bellhop_rayreuse`; roots under `/tmp/fp2h_modes`):

- All 10 broadband profiles passed in nonreuse, reuse, and parallel modes; every pairwise SHD `cmp` passed. SHA-256 (nonreuse = reuse = parallel):
  - `attenuation_unit_n/broadband_smoke`: `6c02651c34a1a50d651df18d4cea72bc1f6b2fc637c7543b6ac5badb543ce165`
  - `attenuation_unit_f/broadband_smoke`: `4cf98e60c99976b91874bfa8f9759dbada71ffa6b3cd3e451cfd7523add06b6d`
  - `attenuation_unit_m/broadband_smoke`: `9baac3160633ce61efbd576678e3e4322d0c5723c548f8a4d1b5a20db6e31a47`
  - `attenuation_unit_w/broadband_smoke`: `891306fec4e1c936bcdd059c3c522a45effba5770a685aa1504332fe7a2369a4`
  - `attenuation_unit_q/broadband_smoke`: `5ef677035fd8d5797af12f25306b4dcfa18588299f96437da51a62463933b3b8`
  - `attenuation_unit_l/broadband_smoke`: `3b05a8b91cb9c058f8a17d099c01ed08c64ab62914618a1a433f59bac03c9d82`
  - `volume_attenuation_francois_garrison/broadband_smoke`: `2837277287c8ccd3784d02f4ce55bc49b9e09ec68d8c937564a33dbeb473642f`
  - `volume_attenuation_biological/broadband_smoke`: `70459926cb04cd2b59867984b0d74823ce85fe0e4289589f4f226f4837e2c05f`
  - `constant_speed_thorp/broadband_smoke`: `1ddd8171315750ddf754136191bc9a7aa3e5b0747cc0b43286acabc74305a7f3`
  - `constant_speed_thorp/broadband_regression`: `c8ef3fad90e32753991b021eb804f4f26046c04783ab50a602a2983f26dcbcd2`
- PRT trace passes (nonreuse / reuse / parallel): all two-frequency profiles `2 / 1 / 1`; Thorp 16-frequency regression `16 / 1 / 1`.
- `--verify-cache` reuse and parallel, before = after: W `10638750469126791633`; Thorp `12163770556679950120`; FG `4134998748544866669`; biological `514508787683948826`. Verification was enabled in every run.
- A complete second parallel matrix in `/tmp/fp2h_modes/parallel_repeat` was byte-identical to the first parallel matrix.
- No generated outputs were written under the repository.

### H10 [SIMPLE] [GENERAL] Publish evidence-bounded documentation

Status: `DONE`
Reviewer: `N/A (coordinator validated)`
Depends on: H09 + successful Batch Acceptance evidence

Likely files:

- `Bellhop_RayReuse/doc/reference/REFERENCE_FEATURE_SUPPORT_MATRIX.md`
- `Bellhop_RayReuse/doc/reports/REPORT_F2CPP_RAYREUSE_PARITY_2026-08-25.md`
- `Bellhop_RayReuse/doc/status/STATUS_FEATURE_PARITY_SEQUENCE_2026-08-29.md`
- `Bellhop_RayReuse/doc/workreports/FP-2H_ATTENUATION_CLOSURE_BATCH_REPORT.md`
- this Worklist

Work:

- Record exact commands, executable hashes, output hashes, validator reports,
  fingerprints, trace counts, and mode identity.
- Mark ATT-01/04/05 closed only after executable evidence passes.
- Describe ATT-02/03 as regression-protected, not newly implemented.
- Restrict support claims to the accepted case/product matrix.
- Record unsupported combinations and residual risks.
- Update Worklist state to `READY_FOR_FINAL_REVIEW` (final `ACCEPTED / CLOSED`
  transition is performed by coordinator after Final Review approval).

Acceptance:

- No claim exceeds executable evidence.
- Origin/F2CPP remain explicitly read-only references.
- Work report distinguishes targeted tests from full Batch Acceptance.
- Failed or not-run gates remain visible.
- Documentation does not claim universal attenuation parity.

Evidence:

- `REFERENCE_FEATURE_SUPPORT_MATRIX.md` updated with Attenuation row.
- `REPORT_F2CPP_RAYREUSE_PARITY_2026-08-25.md` updated with FP-2H oracle & parity table closures.
- `STATUS_FEATURE_PARITY_SEQUENCE_2026-08-29.md` updated with FP-2H status.
- `FP-2H_ATTENUATION_CLOSURE_BATCH_REPORT.md` published.
- Worklist transitioned to `READY_FOR_FINAL_REVIEW`.

## 5. Likely construction files

### 5.1 Actual DESIGN-turn change

Only this file is added during DESIGN:

- `Bellhop_RayReuse/doc/worklists/FP-2H_ATTENUATION_CLOSURE_WORKLIST.md`

No production, test, standard-case, status, or reference source file is changed
during this DESIGN turn.

### 5.2 Planned production files

- `Bellhop_RayReuse/CMakeLists.txt`
- `Bellhop_RayReuse/include/rayreuse/model/environment.hpp`
- `Bellhop_RayReuse/src/model/environment.cpp`
- `Bellhop_RayReuse/include/rayreuse/acoustics/attenuation.hpp`
- `Bellhop_RayReuse/src/acoustics/attenuation.cpp`
- all five `include/rayreuse/model/*_frequency_ssp.hpp`
- all five `src/model/*_frequency_ssp.cpp`
- `Bellhop_RayReuse/include/rayreuse/model/sound_speed_evaluator.hpp`
- `Bellhop_RayReuse/src/model/sound_speed_evaluator.cpp`
- `Bellhop_RayReuse/include/rayreuse/acoustics/boundary_acoustics.hpp`
- `Bellhop_RayReuse/src/acoustics/boundary_acoustics.cpp`
- `Bellhop_RayReuse/src/field/frequency_projector.cpp`
- `Bellhop_RayReuse/src/io/environment_parser.cpp`
- `Bellhop_RayReuse/app/main.cpp`

### 5.3 Planned test and shared-case files

- `Bellhop_RayReuse/tests/unit/attenuation_test.cpp`
- `Bellhop_RayReuse/tests/unit/core_types_test.cpp`
- `Bellhop_RayReuse/tests/unit/boundary_acoustics_test.cpp`
- `Bellhop_RayReuse/tests/component/environment_parser_test.cpp`
- `Bellhop_RayReuse/tests/component/frequency_projector_test.cpp`
- five SSP component test files
- `Bellhop_RayReuse/tests/component/sound_speed_evaluator_test.cpp`
- eight ATT-01/04/05 case TOMLs
- `test/standard_cases/codes/validate_i4_attenuation_units.py`
- `test/standard_cases/codes/validate_i4_volume_attenuation.py`

### 5.4 Protected reference and cache files

No edits are permitted to:

- `Bellhop_origin/**`
- `Bellhop_F2CPP/**`
- `Bellhop_RayReuse/src/cache/ray_path_cache.cpp`
- `Bellhop_RayReuse/include/rayreuse/cache/ray_path_cache.hpp`
- frozen `RayPath`, `ReflectionEvent`, and frequency-independent ray schemas

`test/standard_cases/codes/standard_cases.py` already supports all three RayReuse
execution modes and is not expected to require modification.

## 6. Acceptance matrix

| Gate | Required evidence |
|---|---|
| ATT-01 N/F/M/W/Q/L | Three-party single and 4/5 kHz smoke fields; 5 kHz cross-unit bit identity |
| ATT-02 W | Unit anchors plus 4 kHz Origin/F2CPP/RayReuse product comparison |
| ATT-03 Thorp | Unit anchors, pre/post hash equality, single, smoke, and 16-frequency regression |
| ATT-04 FG | Parser, kernel anchors, single 5 kHz, smoke 5/10 kHz, non-no-op control |
| ATT-05 biological | Parser, inclusive/overlap anchors, single 5 kHz, smoke 2.5/5 kHz, non-no-op control |
| Five SSP backends | None/T/FG/B node-first conversion and backend-specific component checks |
| Boundary seam | P/S, flat/long depth, biological depth response, grain exclusion |
| Cache contract | Protected implementation zero diff; before/after equality; frozen baselines |
| Mode parity | Broadband SHD byte identity across nonreuse/reuse/parallel |
| Reuse evidence | Two-frequency trace passes `2/1/1`; 16-frequency Thorp `16/1/1` |
| Oracle provenance | Distinct executable paths/hashes, fresh manifests, exact identities, aggregate SHA |
| Documentation | Claims limited to passed matrix; no implementation-only parity declarations |

Any failure enters remediation and revalidation. ADVANCED findings return to an
advanced worker and the original reviewer checkpoint.

## 7. Batch regression and validation

Run once after tasks H00–H09 and all checkpoint reviews pass (before H10 documentation closure), from an isolated clean build:

```bash
cmake -S Bellhop_RayReuse \
  -B Bellhop_RayReuse/build/fp2h-clean \
  -DCMAKE_BUILD_TYPE=Release \
  -DRAYREUSE_WARNINGS_AS_ERRORS=ON
cmake --build Bellhop_RayReuse/build/fp2h-clean --parallel

uv run ctest \
  --test-dir Bellhop_RayReuse/build/fp2h-clean \
  --output-on-failure

uv run pytest
uv run make -C test/standard_cases test-unit
```

Additional required gates:

1. Run ATT-01/02 and ATT-03/04/05 validators with distinct Origin, F2CPP, and
   clean RayReuse executables.
2. Run every listed RayReuse broadband profile under:
   - `nonreuse`;
   - `reuse`;
   - `parallel`.
3. Verify complete SHD byte identity across those modes.
4. Run representative W/T/FG/B broadband cases with `--verify-cache`.
5. Confirm expected trace counts and fingerprint equality.
6. Run representative frozen regressions for:
   - C, P, N, S, and Q SSP;
   - `munk_spline`;
   - multi-source;
   - irregular receiver;
   - boundary material projection.
7. Check scope and whitespace:

```bash
git diff --check
git diff --exit-code -- Bellhop_origin Bellhop_F2CPP
git diff --exit-code -- \
  Bellhop_RayReuse/src/cache/ray_path_cache.cpp \
  Bellhop_RayReuse/include/rayreuse/cache/ray_path_cache.hpp
```

Batch Acceptance is `PASS` only if:

- tasks H00–H09 are DONE;
- every ADVANCED reviewer checkpoint (H01-R～H06-R) is PASS;
- all listed tests/oracles pass;
- no tolerance is weakened;
- generated products are absent from Git;
- no HIGH/BLOCKER remains.

All commands in this section are `not-run` during DESIGN.

## 8. Evidence inventory

The final batch report must record:

- source revision and scoped Git status;
- compiler and build configuration;
- Origin, F2CPP, and RayReuse resolved executable paths;
- executable SHA-256 values;
- validator schema versions and JSON payloads;
- case/profile/frequency manifest identities;
- pairwise pressure/TL metrics;
- cross-unit bit-identity result;
- no-op guard differences;
- mode-specific SHD hashes;
- trace-pass counts;
- before/after cache fingerprints;
- frozen baseline values;
- targeted and batch test commands with exit status;
- reviewer PASS records;
- final-reviewer verdict.

Implementation or parser presence without these products is not oracle evidence.

## 9. Design findings

### HIGH

1. `Bellhop_RayReuse/include/rayreuse/model/environment.hpp`
   RayReuse cannot own Francois–Garrison parameters or biological layers.
   Existing raw tags are insufficient for safe lifetime and concurrency.

2. `Bellhop_RayReuse/src/acoustics/attenuation.cpp`
   FG and biological branches explicitly reject execution. The kernel is the
   primary ATT-04/05 production gap.

3. `Bellhop_RayReuse/src/model/*_frequency_ssp.cpp` and
   `src/model/sound_speed_evaluator.cpp`
   All five backends currently resolve only raw-attached model state and cannot
   perform parameterized, node-depth volume conversion.

4. `Bellhop_RayReuse/src/acoustics/boundary_acoustics.cpp` and
   `src/field/frequency_projector.cpp`
   Environment-level volume model and material evaluation depth do not reach
   boundary P/S conversion. Direct-field standard cases alone would miss this.

5. `Bellhop_RayReuse/src/cache/ray_path_cache.cpp`
   The existing hash sequence includes `RawAttenuation.volumeModel`. Removing
   or relocating the field during an F2CPP-style model migration would break
   cache compatibility and frozen baselines.

### MEDIUM

6. `Bellhop_RayReuse/src/io/environment_parser.cpp` and
   `Bellhop_RayReuse/app/main.cpp`
   Parser/PRT handling supports the legacy Thorp tag but not parameterized
   environment ownership and required FG/biological reporting.

7. `test/standard_cases/cases/attenuation_unit_*/case.toml`,
   `volume_attenuation_*/case.toml`, and both I4 validators
   Existing ATT-01/04/05 product evidence excludes RayReuse, and validators
   currently prove only Origin/F2CPP execution.

## 10. Residual risks and unresolved questions

### Residual risks

- Raw tags and environment-level models form a compatibility seam. Conflict and
  exactly-once tests are mandatory to prevent double attenuation.
- Fingerprints intentionally exclude FG/biological parameter payloads and are
  not full environment cache keys.
- Product oracles primarily exercise C-linear direct-field attenuation;
  backend and boundary generalization relies on component evidence.
- Boundary biological depth, especially legacy long-material depth, can
  produce large but valid effects and needs explicit anchors.
- Exact same-toolchain F2CPP parity depends on preserving floating literal
  promotion, `fma`, and accumulation order.
- Biological layer iteration is linear in layer count. Optimization is outside
  FP-2H unless profiling demonstrates a release blocker.
- The historical PRT spelling `attenaution` must remain until a separately
  versioned oracle migration.

### Unresolved architectural questions

None blocking. Persistent cross-environment cache identity and broader
product-oracle coverage are explicitly deferred rather than silently solved in
FP-2H.

## 11. Closure state

Current state:

- DESIGN audit: complete.
- Worklist: FP-2H ACCEPTED / CLOSED.
- Production implementation: complete.
- Targeted validation: passed.
- Batch Acceptance: passed.
- Checkpoint reviews: H01-R～H06-R all PASS.
- Final Review: ACCEPTED.
- FP-2H status: ACCEPTED / CLOSED.
