# FP-2I Line Source Closure Worklist

Batch: `FP-2I`
Phase: `ACCEPTED / CLOSED`
Design state: `CONSTRUCT COMPLETE / ACCEPTED`
Implementation state: `CONSTRUCT COMPLETE`
Predecessors: `FP-2F ACCEPTED`, `FP-2G ACCEPTED`, `FP-2H ACCEPTED`

This Worklist is the execution authority for FP-2I after coordinator FREEZE.
Changing scientific semantics, ownership, cache behavior, oracle thresholds, or
scope requires reopening DESIGN with an architect.

## 1. Goal

Close RayReuse parity for:

- `SRC-02`: Line source geometry dispatch (`'X'` in ENV run type 4th character; point source remains blank or `'R'`).
- `PRD-08`: Line source product scaling (Cartesian beam scale with `-4.0*sqrt(pi)` line prefix applied range-independently to TL coherent complex pressure and incoherent/semicoherent intensity, and `4.0*sqrt(pi)` applied to ASCII/Binary Arrival amplitude).
- `nonreuse`, `reuse`, and `parallel` broadband execution parity for line source runs.
- Frozen-cache immutability and fingerprint compatibility.

Parity declarations require executable Origin, F2CPP, and RayReuse product
evidence. Parser acceptance or unit tests alone cannot close a parity item.

## 2. Scope

### 2.1 In scope

1. Add `SourceGeometry` enum (`Point`, `Line`) and `SimulationCase` integration.
2. Extend ENV parsing and PRT reporting for RunType 4th position `'X'`.
3. Support Line Source in Cartesian Cerveny, Ray-centered Cerveny, Geometric Hat, and Geometric Gaussian Influence backends.
4. Enforce Simple Gaussian (`CS`) rejection of Line Source (`ValidationError`).
5. Update `PressureScaling` to apply line-source factor `(-4.0F * sqrt(3.14159265F)) * beamScale` across all receiver ranges.
6. Update `ArrivalWriter` to apply line-source factor `4.0F * sqrt(pi)` to arrival amplitudes.
7. Wire `SourceGeometry` through `SingleFrequencySolver`, `BroadbandNonreuseSolver`, `SerialReuseSolver`, `ParallelSolver`, `ArrivalSolver`, `EigenraySolver`.
8. Enable RayReuse in `source_geometry_line` and `arrival_line_directional_multisource` standard cases.
9. Verify three-party oracle, broadband multi-mode byte identity, and frozen-cache invariance.
10. Repository-wide documentation audit and final parity closeout.

### 2.2 Explicitly out of scope

- 3D, Bellhop3D, or N×2D.
- Beam shift or experimental features.
- Ray-centered geometric Gaussian (not supported by F2CPP/Origin).
- Changes to Origin or F2CPP source, tests, cases, or build products.
- Changes to `RayPath`, `RayPathCache`, `ReflectionEvent`, or ray-state layouts.
- Changes to `RayPathCache::contentFingerprint()` input order, byte encoding, FNV-1a algorithm, or hash constants.
- Persisting frequency-local amplitude scaling in frozen cache.

## 3. Frozen architecture decisions

### 3.1 Model and Core Types

In `Bellhop_RayReuse/include/rayreuse/model/simulation_case.hpp` and `src/model/simulation_case.cpp`:
- `enum class SourceGeometry { Point, Line };`
- `SimulationCase` owns `SourceGeometry sourceGeometry_ = SourceGeometry::Point`.
- Getter: `[[nodiscard]] SourceGeometry sourceGeometry() const noexcept;`.
- Constructor receives `SourceGeometry sourceGeometry = SourceGeometry::Point` (default argument at end of parameter list).

### 3.2 Parser and PRT Reporting

In `Bellhop_RayReuse/src/io/environment_parser.cpp`:
- RunType 4th character:
  - `'X'` -> `SourceGeometry::Line`
  - `' '`, `'R'`, or absent -> `SourceGeometry::Point`
  - Any other character -> `ValidationError`
- Simple Gaussian (`CS`):
  - If `sourceGeometry == SourceGeometry::Line`, throw `ValidationError("simple Gaussian influence requires a point source")`.
- PRT generation:
  - If `sourceGeometry == SourceGeometry::Line`: write `"Line source (Cartesian coordinates)\n"`.
  - Else: write `"Point source (cylindrical coordinates)\n"`.

### 3.3 Influence Kernels

- Cartesian Cerveny (`CC/IC/SC`):
  - `ratio = (sourceGeometry == SourceGeometry::Line) ? 1.0 : std::sqrt(std::abs(std::cos(launchAngle)));`
- Ray-centered Cerveny (`CR/IR/SR`):
  - `ratio = (sourceGeometry == SourceGeometry::Line) ? 1.0 : std::sqrt(std::abs(std::cos(launchAngle)));`
- Geometric Hat (`CG/IG/SG`, `Cg/Ig/Sg`):
  - `ratio = (sourceGeometry == SourceGeometry::Line) ? 1.0 : std::sqrt(std::abs(std::cos(launchAngle)));`
- Geometric Gaussian (`CB/IB/SB`):
  - `sourceRatio = (sourceGeometry == SourceGeometry::Point) ? std::sqrt(std::abs(std::cos(launchAngle))) / std::sqrt(2.0 * std::numbers::pi) : 1.0 / std::sqrt(2.0 * std::numbers::pi);`
- Simple Gaussian (`CS`):
  - Explicitly requires `SourceGeometry::Point`.

### 3.4 Pressure Scaling (TL SHD)

In `Bellhop_RayReuse/src/field/pressure_scaling.cpp`:
- `constexpr float kLegacyPi = 3.14159265F;`
- `const float linePrefix = -4.0F * std::sqrt(kLegacyPi);`
- For `SourceGeometry::Line`:
  `factor = static_cast<double>(linePrefix) * beamScale;` (applied to all ranges including range == 0).
- For `SourceGeometry::Point`:
  `factor = (range == 0.0) ? 0.0 : beamScale / std::sqrt(std::abs(range));`

### 3.5 Arrival Writer (ARR A/a)

In `Bellhop_RayReuse/src/io/arrival_writer.cpp`:
- `float sourceScale(SourceGeometry geometry, double range)`:
  - `SourceGeometry::Line`: `static_cast<float>(4.0 * std::sqrt(std::numbers::pi))`
  - `SourceGeometry::Point`: `range == 0.0 ? 1.0e5F : checkedFloat(1.0 / std::sqrt(range), "ARR point-source scale")`
- Each arrival record's amplitude is multiplied by `sourceScale(simulation_.sourceGeometry(), range)`.

### 3.6 Frozen Cache and Concurrency Contract

- Ray geometry tracing is purely geometric and frequency-independent; ray paths and reflection events are identical for point and line sources.
- `RayPathCache` schema and `contentFingerprint()` are unmodified.
- Line source scaling is frequency-local and applied during field/arrival evaluation.
- `nonreuse`, `reuse`, and `parallel` broadband modes produce byte-identical output products.

## 4. Ordered Tasks

```text
I00 [STANDARD] Freeze pre-construction baseline
I01 [ADVANCED] Model: SourceGeometry enum and SimulationCase integration
I02 [STANDARD] Parser: RunType fourth-position 'X' and PRT reporting
I03 [ADVANCED] Influence: Point vs Line ratio in Cerveny, GeoHat, GeoGaussian
I04 [ADVANCED] Pressure Scaling: Line source spreading factor
I05 [STANDARD] Arrival Writer: Line source amplitude scaling
I06 [ADVANCED] Solvers & Broadband Pipeline: Wire SourceGeometry & verify cache invariance
I07 [STANDARD] Standard Cases & Oracles: Enable source_geometry_line and arrival_line_directional_multisource
I08 [STANDARD] Broadband Multi-mode & Cache Invariance Evidence Matrix
Batch Acceptance
I09 [SIMPLE] Documentation Closeout & Batch Report
Final Review
```

---

### I00 [STANDARD] Freeze pre-construction baseline

Status: `DONE`
Reviewer: `N/A (coordinator)`

Evidence:
- Pre-FP-2I Git revision: `099a2b1 feat(rayreuse): complete FP-2H attenuation closure`
- CTest: 41/41 PASSED
- Pytest: 187/187 PASSED
- Make unit: 172/172 PASSED
- Working tree clean.

---

### I01 [ADVANCED] Model: SourceGeometry enum and SimulationCase integration

Status: `DONE`
Reviewer: `PASS — I01-R`

Evidence:
- Added `enum class SourceGeometry { Point, Line };` to `simulation_case.hpp`.
- Added `sourceGeometry_` member, getter, and constructor integration.
- Unit tests in `core_types_test.cpp` verify Point/Line preservation, Simple Gaussian rejection, invalid enum rejection.

---

### I02 [STANDARD] Parser: RunType fourth-position 'X' and PRT reporting

Status: `DONE`
Reviewer: `PASS — I02-R`

Evidence:
- `environment_parser.cpp` parses `'X'` as `SourceGeometry::Line`, blank and `'R'` as `Point`.
- Simple Gaussian (`'CS X'`) throws `ValidationError`.
- `main.cpp` writes `"Line source (Cartesian coordinates)"` for line source.
- `environment_parser_test.cpp` test suite passes.

---

### I03 [ADVANCED] Influence: Point vs Line ratio in Cerveny, GeoHat, GeoGaussian

Status: `DONE`
Reviewer: `PASS — I03-R`

Evidence:
- Cartesian Cerveny, Ray-centered Cerveny, Geometric Hat, Geometric Gaussian receive `SourceGeometry`.
- Line source ratio = 1.0 (or `1/sqrt(2*pi)` for Geometric Gaussian) applied.
- Simple Gaussian validates `SourceGeometry::Point`.
- Component tests pass.

---

### I04 [ADVANCED] Pressure Scaling: Line source spreading factor

Status: `DONE`
Reviewer: `PASS — I04-R`

Evidence:
- `pressure_scaling.cpp` implements `linePrefix = -4.0F * sqrt(3.14159265F)`.
- Applied across all ranges including range 0 for coherent pressure and intensity-to-pressure.
- `pressure_scaling_test.cpp` passes with exact bit/tolerance anchors.

---

### I05 [STANDARD] Arrival Writer: Line source amplitude scaling

Status: `DONE`
Reviewer: `PASS — I05-R`

Evidence:
- `arrival_writer.cpp` implements `sourceScale(SourceGeometry, range)` with factor `4.0 * sqrt(pi)` for line source.
- ASCII and binary writers tested and pass.

---

### I06 [ADVANCED] Solvers & Broadband Pipeline: Wire SourceGeometry & verify cache invariance

Status: `DONE`
Reviewer: `PASS — I06-R`

Evidence:
- `single_frequency_solver.cpp`, `arrival_solver.cpp`, `eigenray_solver.cpp` wired.
- `--verify-cache` before == after fingerprint verified on line source cases.
- Trajectory tracing and cache schema untouched.

---

### I07 [STANDARD] Standard Cases & Oracles: Enable source_geometry_line and arrival_line_directional_multisource

Status: `DONE`
Reviewer: `N/A (coordinator)`

Evidence:
- `source_geometry_line` SHD: F2CPP↔RayReuse 0 diff (0 ULP), Origin↔RayReuse within 0.001 dB tolerance.
- `arrival_line_directional_multisource` ARR: F2CPP↔RayReuse 0 ULP diff, `validate_i8_arrivals.py` 9/9 PASS.

---

### I08 [STANDARD] Broadband Multi-mode & Cache Invariance Evidence Matrix

Status: `DONE`
Reviewer: `N/A (coordinator)`

Evidence:
- `source_geometry_line` broadband smoke (50, 100 Hz): SHD hash `544d2b7dbc8e87fcd618a5d7ef3ffe88eee1c6c40bf797e17dc721eff4d6741d` byte-identical across nonreuse, reuse, parallel.
- `arrival_line_directional_multisource` broadband smoke (500, 1000 Hz): ARR hashes `f923d3be699db7f4465f16be3f256afed94f1b3e5995d1e0dddc334b314704d7` and `0899cf03da4107ca4c6ff415111cc795e81e9aceafa23db0ec3c6b497b49d2f3` byte-identical across nonreuse, reuse, parallel.
- Trace passes: 2/1/1 for single source, 4/2/2 for dual source.
- `--verify-cache` before == after:
  - `source_geometry_line`: `11632125087325642441`
  - `arrival_line_directional_multisource`: source 0 `4994599520005947392`, source 1 `5953004235750977769`.

---

### I09 [SIMPLE] Documentation Closeout & Batch Report

Status: `DONE`
Reviewer: `N/A (coordinator)`

Evidence:
- Batch report `FP-2I_LINE_SOURCE_CLOSURE_BATCH_REPORT.md` written and validated.
- FP-2I status: `ACCEPTED / CLOSED`.
