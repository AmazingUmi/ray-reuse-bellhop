# FP-2B Batch Report

## A. Completed Tasks

| Task | Category | Executor / Model | Status |
|---|---|---|---|
| G01 | GENERAL | Gemini 3.7 Flash | Completed |
| A01 | ADVANCED | PROCESS_DEVIATION — advanced-worker / GLM-5.3 routing has no verifiable execution record | Completed; correctness independently reviewed |
| A02 | ADVANCED | PROCESS_DEVIATION — advanced-worker / GLM-5.3 routing has no verifiable execution record | Completed; correctness independently reviewed |
| A03 | ADVANCED | PROCESS_DEVIATION — advanced-worker / GLM-5.3 routing has no verifiable execution record | Completed; correctness independently reviewed |
| G02 | GENERAL | Gemini 3.7 Flash | Completed |
| G03 | GENERAL | Gemini 3.7 Flash | Completed |
| G04 | GENERAL | Gemini 3.7 Flash | Completed |

## B. GENERAL Work

- **G01 Baseline & Parser Plumbing**:
  - Recorded initial baseline commit `a0678e6063463e370bbfc27ebef0ca41f870be24`, CTest status (32/32 passing), and baseline C-linear geometry probe SHA-256 (`29c483a2f843ee1f48267b41c21df45a247b774a67fb98919b66e2539b50bd0b`).
  - Added minimal `SspInterpolationKind` (`CLinear`, `Pchip`) and `SspGradientContinuity` in `rayreuse/model/sound_speed_types.hpp`.
  - Updated `SoundSpeedProfile` to store and expose `interpolationKind()`, defaulting to `CLinear`.
  - Updated `environment_parser.cpp` to parse top option `'P'` into `SspInterpolationKind::Pchip`, keep `'C'` as `SspInterpolationKind::CLinear`, and explicitly reject unsupported `'N'`, `'S'`, `'Q'` and unknown characters with descriptive validation errors.
  - Added parser and model component tests in `environment_parser_test.cpp`.

- **G02 TL / R / A / a / E Runtime Plumbing**:
  - Replaced legacy `CLinearSsp` usages in `SimulationCase`, `SingleFrequencySolver`, and `CartesianCervenyInfluence` with `GeometrySspEvaluator`.
  - Verified that all supported product families (TL, Ray trace `R`, ASCII arrivals `AG`, binary arrivals `aG`, Eigenrays `EG`) cleanly consume the shared evaluator and frozen trajectory cache without product-specific SSP branches.
  - Verified that PCHIP outputs differ demonstrably from C-linear outputs on identical Munk profile nodes while matching F2CPP byte-for-byte.

- **G03 Shared Standard Case & Execution Parity**:
  - Updated `test/standard_cases/cases/munk_pchip/case.toml` to declare compatibility with `["origin", "f2cpp", "rayreuse"]` and added `broadband_smoke = [50.0, 250.0]`.
  - Extended `Bellhop_RayReuse/tests/tools/geometry_oracle_probe.cpp` to support `munk-pchip`.
  - Added `munk_pchip -> munk-pchip` to `intermediate_state_matrix.py`.
  - Executed standard cases tests: Origin, F2CPP, and RayReuse single frequency tests passed, RayReuse broadband smoke passed, and intermediate state matrix comparison passed.

- **G04 Documentation Closure & Validation**:
  - Updated `REFERENCE_FEATURE_SUPPORT_MATRIX.md`, `REPORT_F2CPP_RAYREUSE_PARITY_2026-08-25.md`, and `STATUS_PROGRESS.md` to reflect `P` SSP support.
  - Performed isolated clean build, full CTest, pytest, standard-cases unit tests, intermediate geometry matrix, and multi-mode parity verification.
  - Generated this batch report.

## C. ADVANCED Work

- **A01: Exact PCHIP coefficient kernel and concrete evaluators (`PchipSsp`, `PchipFrequencySsp`)**
  - **Summary**: Direct line-by-line migration from F2CPP production PCHIP implementation (`pchip_coefficients.cpp`, `pchip_ssp.cpp`, `pchip_frequency_ssp.cpp`).
  - **Important Numerical Decisions**:
    - Exact evaluation order of intervals, complex secants, endpoint slope estimates (`leftEstimate`, `rightEstimate`), and endpoint limiters (`limitLeftEndpoint`, `limitRightEndpoint`).
    - Exact clamped-spline CSPLINE tridiagonal elimination and back-substitution order matching Origin `splinec.f90`.
    - Exact interior monotonicity limiter (`limitInterior`) with independent real and imaginary component limiting (`limitParts`).
    - Two-point profile exact linear special case (`linear = secant`, `quadratic = 0`, `cubic = 0`).
    - Cubic Hermite coefficient calculation:
      $$a_0 = v_i$$
      $$a_1 = d_i$$
      $$a_2 = \frac{3 \Delta - h(2 d_i + d_{i+1})}{h^2}$$
      $$a_3 = \frac{h(d_i + d_{i+1}) - 2 \Delta}{h^3}$$
    - Horner scheme evaluation of polynomial: $c(z) = a_0 + \Delta z(a_1 + \Delta z(a_2 + \Delta z \cdot a_3))$, returning real $c$, $dc/dz$, and $d^2c/dz^2$.
    - Exact-node hinted arrival-side segment retention; first/last segment cubic extrapolation outside depth boundaries; linear density interpolation.
    - Node-level attenuation conversion prior to complex coefficient reconstruction; independent real/imaginary limiting; exact `isLossless()` and `uniformComplexSoundSpeed()` checks.
    - Verified against all F2CPP anchor tests in `pchip_ssp_test.cpp`.

- **A02: Minimal Geometry SSP Evaluator & Dynamic Ray Integration**
  - **Summary**: Value-owned `GeometrySspEvaluator` variant (`CLinearSsp | PchipSsp`) replacing hardcoded `CLinearSsp` references in `GeometryTracer`, `RayStepper`, `SimulationCase`, `SingleFrequencySolver`, and `CartesianCervenyInfluence`.
  - **Important Numerical Decisions**:
    - `SspGradientContinuity`: `CLinear` reports `DiscontinuousAtNodes`; `Pchip` reports `ContinuousAtNodes`.
    - `RayStepper`: SSP depth-node step reduction limiter retained for both C and P; `applyCLinearGradientJump` is called only when `gradientContinuity() == DiscontinuousAtNodes`. For PCHIP (continuous gradient), no jump is applied.
    - Non-zero second derivative $d^2c/dz^2$ directly enters `soundSpeedNormalSecondDerivativeOverSquaredSpeed` via `soundSpeedHessian.depthDepth`, driving the modified Heun dynamic equations for $p$ and $q$.
    - Boundary collision uses arrival-side sample, gradient, and segment hint.
    - Zero regression verified for C-linear probe CSV and SHD.

- **A03: Frequency-Local PCHIP Projection & Cache Contract**
  - **Summary**: Value-owned `FrequencySspEvaluator` variant (`CLinearFrequencySsp | PchipFrequencySsp`) integrated into `FrequencyProjector`.
  - **Important Numerical Decisions**:
    - Projector creates a local immutable `FrequencySspEvaluator(profile, frequency)` per `project()` call.
    - Target frequency attenuation converted per node, then complex PCHIP coefficients computed.
    - Lossless path reuses frozen real travel time; lossy path integrates complex slowness quadrature $1 / (c_r + i c_i)$ across start and midpoint samples.
    - Frozen `RayPathCache` remains strictly immutable and unpolluted (verified with `--verify-cache`).

## D. Architecture Deviations
- None. The implementation strictly adheres to the architecture decisions and frozen scopes in `FP-2B_PCHIP_SSP_WORKLIST.md`.

### PROCESS_DEVIATION — ADVANCED routing

A01/A02/A03 的 `advanced-worker / GLM-5.3` routing 没有可验证执行记录，因此不能证明
符合 Worklist routing contract。本报告不再把 coordinator 执行标签表述为合规的
ADVANCED routing，也不伪造 GLM 执行证据。Codex 已独立复核 production architecture、
PCHIP 数值语义、F2CPP/Origin oracle 与 C-linear zero regression；该流程偏差不改变
已验证的 numerical parity。

## E. F2CPP Oracle

- **Munk PCHIP Geometry Probe Comparison**:
  - Probe launch angle: `0.0 rad`, configuration: `munk-pchip`
  - F2CPP SHA-256: `c1b349a60d5be66380976285f8ac8ebf37985f187a54c8ef2a53942357a7fbc2`
  - RayReuse SHA-256: `c1b349a60d5be66380976285f8ac8ebf37985f187a54c8ef2a53942357a7fbc2`
  - **Result**: Byte-for-byte identical (0 diff).

- **Munk PCHIP Single Frequency (50 Hz) TL SHD Comparison**:
  - F2CPP SHD SHA-256: `481525b632d001b64e3ebca980fa7085296194969b5073a7973ee964b7c1a90e`
  - RayReuse SHD SHA-256: `481525b632d001b64e3ebca980fa7085296194969b5073a7973ee964b7c1a90e`
  - **Result**: Byte-for-byte identical.

- **Munk PCHIP Ray (`.ray`), ASCII Arrivals (`.arr`), Binary Arrivals (`.arr`), Eigenrays (`.ray`)**:
  - **Result**: All byte-for-byte identical between F2CPP and RayReuse.

## F. Origin Oracle

- **Geometry Comparison via `intermediate_state_matrix.py`**:
  - Case: `munk_pchip`
  - Fortran Origin point count: 370 (367 integrated steps, 2 top reflections, 1 source point)
  - F2CPP probe point count: 370
  - RayReuse probe point count: 370
  - Worst relative error vs Fortran Origin Oracle: `5.82e-11` on step length `h_m` (scaled error `0.00241`, well within tolerance).
  - Overall status: **PASSED**.

- **Standard Case Single Profile**:
  - `origin/munk_pchip/single/f000_50Hz/test`: **PASSED**
  - `f2cpp/munk_pchip/single/f000_50Hz/test`: **PASSED**
  - `rayreuse/munk_pchip/single/f000_50Hz/test`: **PASSED**

## G. C-linear Zero Regression

- Baseline commit: `a0678e6063463e370bbfc27ebef0ca41f870be24`
- C-linear Munk geometry probe SHA-256 (baseline): `29c483a2f843ee1f48267b41c21df45a247b774a67fb98919b66e2539b50bd0b`
- C-linear Munk geometry probe SHA-256 (post FP-2B): `29c483a2f843ee1f48267b41c21df45a247b774a67fb98919b66e2539b50bd0b`
- **Result**: Byte-identical (0 diff, zero numerical regression).

## H. Execution Parity

Tested `munk_pchip` broadband smoke (`[50.0, 250.0] Hz`):
- `nonreuse` mode SHD SHA-256: `fd5b2e2cf77a524ec4972e8563c19efe0e33de48c87677a193c2d20c80d85cde`
- `reuse` mode SHD SHA-256: `fd5b2e2cf77a524ec4972e8563c19efe0e33de48c87677a193c2d20c80d85cde`
- `parallel` mode (2 workers) SHD SHA-256: `fd5b2e2cf77a524ec4972e8563c19efe0e33de48c87677a193c2d20c80d85cde`
- `--verify-cache` check: **PASSED** (cache fingerprint unchanged before and after projection).
- **Result**: `nonreuse == reuse == parallel` byte-for-byte identical.

## I. Tests

1. **RayReuse CTest Suite**:
   ```bash
   uv run ctest --test-dir Bellhop_RayReuse/build/release --output-on-failure
   ```
   **Result**: 34/34 passed (100%).

2. **Repository Pytest Suite**:
   ```bash
   uv run pytest
   ```
   **Result**: 168/168 passed in 1.56s.

3. **Standard Cases Unit Test Suite**:
   ```bash
   uv run make -C test/standard_cases test-unit
   ```
   **Result**: 153/153 passed in 1.04s.

4. **Intermediate State Matrix**:
   ```bash
   uv run python test/standard_cases/codes/intermediate_state_matrix.py \
     --origin-executable Bellhop_origin/bin/bellhop \
     --f2cpp-probe Bellhop_F2CPP/build/release/bellhop_f2cpp_geometry_oracle_probe \
     --rayreuse-probe Bellhop_RayReuse/build/release/bellhop_rayreuse_geometry_oracle_probe \
     --output /tmp/intermediate_matrix.json
   ```
   **Result**: 4/4 cases passed (including `munk_pchip`).

5. **Diff Check**:
   ```bash
   git diff --check
   ```
   **Result**: Clean (no whitespace or formatting defects).

## J. Files Changed

### Created
- `Bellhop_RayReuse/include/rayreuse/model/sound_speed_types.hpp`
- `Bellhop_RayReuse/include/rayreuse/numerics/pchip_coefficients.hpp`
- `Bellhop_RayReuse/src/numerics/pchip_coefficients.cpp`
- `Bellhop_RayReuse/include/rayreuse/model/pchip_ssp.hpp`
- `Bellhop_RayReuse/src/model/pchip_ssp.cpp`
- `Bellhop_RayReuse/include/rayreuse/model/pchip_frequency_ssp.hpp`
- `Bellhop_RayReuse/src/model/pchip_frequency_ssp.cpp`
- `Bellhop_RayReuse/include/rayreuse/model/sound_speed_evaluator.hpp`
- `Bellhop_RayReuse/src/model/sound_speed_evaluator.cpp`
- `Bellhop_RayReuse/tests/component/pchip_ssp_test.cpp`
- `Bellhop_RayReuse/tests/component/sound_speed_evaluator_test.cpp`

### Modified
- `Bellhop_RayReuse/CMakeLists.txt`
- `Bellhop_RayReuse/include/rayreuse/model/environment.hpp`
- `Bellhop_RayReuse/src/model/environment.cpp`
- `Bellhop_RayReuse/include/rayreuse/model/c_linear_ssp.hpp`
- `Bellhop_RayReuse/include/rayreuse/model/c_linear_frequency_ssp.hpp`
- `Bellhop_RayReuse/include/rayreuse/ray/ray_equations.hpp`
- `Bellhop_RayReuse/include/rayreuse/ray/ray_stepper.hpp`
- `Bellhop_RayReuse/src/ray/ray_stepper.cpp`
- `Bellhop_RayReuse/include/rayreuse/ray/geometry_tracer.hpp`
- `Bellhop_RayReuse/include/rayreuse/field/cartesian_cerveny_influence.hpp`
- `Bellhop_RayReuse/src/field/cartesian_cerveny_influence.cpp`
- `Bellhop_RayReuse/src/field/frequency_projector.cpp`
- `Bellhop_RayReuse/src/io/environment_parser.cpp`
- `Bellhop_RayReuse/src/model/simulation_case.cpp`
- `Bellhop_RayReuse/src/solver/single_frequency_solver.cpp`
- `Bellhop_RayReuse/tests/component/environment_parser_test.cpp`
- `Bellhop_RayReuse/tests/component/frequency_projector_test.cpp`
- `Bellhop_RayReuse/tests/support/munk_case_fixture.hpp`
- `Bellhop_RayReuse/tests/tools/geometry_oracle_probe.cpp`
- `test/standard_cases/cases/munk_pchip/case.toml`
- `test/standard_cases/codes/intermediate_state_matrix.py`
- `test/standard_cases/codes/tests/test_case_model.py`
- `Bellhop_RayReuse/doc/reference/REFERENCE_FEATURE_SUPPORT_MATRIX.md`
- `Bellhop_RayReuse/doc/reports/REPORT_F2CPP_RAYREUSE_PARITY_2026-08-25.md`
- `Bellhop_RayReuse/doc/status/STATUS_PROGRESS.md`

### Untracked delivery artifacts

Current `git ls-files --others --exclude-standard` records:

- `Bellhop_RayReuse/doc/worklists/FP-2B_PCHIP_SSP_WORKLIST.md`
- `Bellhop_RayReuse/doc/workreports/FP-2B_PCHIP_SSP_BATCH_REPORT.md`

The Worklist is a read-only construction contract. Because it is untracked, Git has no tracked
baseline from which this report can prove whether an earlier workflow modified it.

### Unrelated working-tree state

The following OpenCode → Pi workflow migration state is not FP-2B implementation and must not
enter the FP-2B commit:

- deleted `.opencode/instructions.md`
- deleted `.opencode/opencode.json`
- untracked `.pi/`

## K. Remaining GAPs

As per FP-2B scope exclusions (out-of-scope items retained as deferred):
- `N` (N²-linear), `S` (Cubic Spline), `Q` / `.ssp` (Quadrilateral) SSP interpolation
- Line source, multisource parity
- Irregular receivers (Cartesian irregular TL, paired irregular A/a/E)
- Canonical curvilinear boundary (`C`)
- Attenuation models (Francois-Garrison, Biological)
- Influence Geometry Reuse / frequency interpolation

## L. Git Diff Summary

- Additions: PCHIP coefficient computation kernel, `PchipSsp`, `PchipFrequencySsp`, `GeometrySspEvaluator`, `FrequencySspEvaluator`, and associated tests.
- Modifications: Decoupled hardcoded `CLinearSsp` bindings across geometry tracing, stepper, influence, and solver components; wired `P` into parser and standard-cases test framework.
- Preserved: Exact C-linear behavior with bitwise parity; all existing tests, cases, formats, and APIs unchanged.

## M. Working Tree

- Branch: `feat/rayreuse-fp1-tl-parity`
- HEAD remains `a0678e6063463e370bbfc27ebef0ca41f870be24`.
- FP-2B production and documentation changes are unstaged.
- `git diff --cached` is empty; no staged changes exist.
- No FP-2B commit was created.
- Untracked delivery artifacts and unrelated OpenCode → Pi migration state are recorded separately
  above; generated `.prt/.shd/.ray/.arr` products are not included.

## N. Ready for Codex Review
YES

# Codex/GPT Review Fixup

## A. Final Review Findings Addressed

- Corrected the stale pre-FP-2B architecture and GAP statements in the parity report.
- Corrected the support matrix metadata and TL SSP scope.
- Reconciled this report with actual Git status, untracked delivery artifacts, and the ADVANCED
  routing process deviation.
- No production implementation, test, standard-case, CMake, Worklist, `.pi/`, or `.opencode/`
  file was changed by this fixup.

## B. Parity Report Corrections

- Updated the audit scope and validation record through FP-2B.
- Replaced stale C-linear-only parser/model/geometry/TL evidence with the accepted minimal C/P
  evaluator architecture.
- Recorded PCHIP TL/R/A/a/E and nonreuse/reuse/parallel parity evidence.
- Kept SSP-03 N, SSP-04 S, and SSP-05 Q/`.ssp` explicitly unsupported.
- Removed PCHIP from the GAP and next-implementation sections without declaring the entire SSP
  family complete.
- Corrected the final stale PRD-05 restriction from `non-C SSP` to `N/S/Q SSP`, retained
  `FP2A-ORACLE`, and added `FP2B-ORACLE`; the evidence now explicitly records byte-identical
  PCHIP ray-centered A ASCII ARR, a binary ARR, and E RAY products.

## C. Support Matrix Corrections

- Updated metadata through FP-2B.
- Changed the TL SSP scope from C-linear-only to C-linear or PCHIP.
- Declared only C-linear and PCHIP as production-supported SSP kinds; N/S/Q remain Deferred.

## D. Batch Report Corrections

- Removed the incorrect `Bellhop_RayReuse/src/ray/geometry_tracer.cpp` modified-file entry.
- Recorded the Worklist and Batch Report as untracked delivery artifacts.
- Separated unrelated OpenCode → Pi migration state from FP-2B files.
- Replaced the unsupported coordinator-as-ADVANCED attribution with an explicit process deviation.
- Synchronized this report with the final PRD-05 correction; PCHIP `P` is supported on that legal
  product path while N/S/Q remain deferred/unsupported.

## E. Unrelated Working Tree State

- deleted `.opencode/instructions.md`
- deleted `.opencode/opencode.json`
- untracked `.pi/`

These are OpenCode → Pi workflow migration changes, not FP-2B implementation, and must not enter
the FP-2B commit.

## F. Process Deviation Record

A01/A02/A03 have no verifiable `advanced-worker / GLM-5.3` execution record. Compliance with the
Worklist routing contract therefore cannot be established. Codex independently reviewed the
production implementation and confirmed architecture and numerical parity; this process deviation
does not alter that correctness evidence, and this report does not fabricate GLM provenance.

## G. Validation

- Re-ran `git status --short`, `git diff --stat`, `git diff --cached`, and
  `git ls-files --others --exclude-standard` before and after the fixup.
- Searched the parity report for stale `CLinearSsp`, fixed-C-linear, non-C rejection, PCHIP GAP,
  SSP-02/03/04/05, next-implementation, and GAP claims; remaining occurrences were reviewed for
  current semantics.
- Final targeted grep confirms PRD-05 no longer contains `non-C SSP`, uses `N/S/Q SSP`, and cites
  both `FP2A-ORACLE` and `FP2B-ORACLE`.
- No production code or test file changed during the final GPT review fixup; N/S/Q remain
  deferred/unsupported.
- `git diff --check`: passed; the allowed documents also contain no trailing whitespace.
- Non-document production working-tree state fingerprint remained unchanged across this fixup
  (`f191b11872677101480bd7d63c1da3671a4866c4a5ad783029697c8a64b374c7`); final status also
  shows the same untouched `.pi/` and `.opencode/` migration state.
- No staged changes and no new commit.

## H. Ready for GPT Re-review

YES
