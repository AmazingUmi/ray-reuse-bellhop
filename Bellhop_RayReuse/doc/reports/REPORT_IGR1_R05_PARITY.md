# REPORT — IGR-1 R05 Numerical Parity Gates A–D

> **Batch:** IGR-1 (Cross-Frequency Cartesian Cerveny Influence Fusion)
> **Task:** R05 — Numerical Parity Gates A–D (V2-GATE-07)
> **Date:** 2026-09-02
> **Branch / build:** `feat/igr-influence-geometry-reuse`, build `Bellhop_RayReuse/build/igr1-clean` (release), binary SHA256 `95b820f22e02e76ed0c6219d0304fbd94d9e7d26af361331c9bbd2e93a3b6d9b`
> **Comparators:** fused path vs current reuse path **in the same binary/build** (intra-binary comparison; no cross-build claims).
> Factual report only — no performance claims (R06 scope).

## 1. Level definitions

| Level | Content | Comparator |
|---|---|---|
| **A** | Cache fingerprint: fused `--verify-cache` run must not modify the frozen cache, and its fingerprint (before == after) must equal the serial-reuse fingerprint on the same case | PRT fingerprint records; component test (`SerialRayReuseStatistics` vs `FusedRayReuseStatistics`) |
| **B** | Raw (unscaled) per-frequency workspace bitwise parity: the reuse accumulation path minus scaling (R02-frozen `WorkspaceDelivery::Raw` seam on `SingleFrequencySolver::solveFrequencyFromSourceCache`) vs `FusedRayReuseSolver::accumulateFrequencies` (fused raw seam) | `std::memcmp` over the full `pressure()` span bytes, per frequency, same frozen fan (one `traceSourceFan` pass) |
| **C** | Scaled per-frequency workspace bitwise parity via the two production paths: `SerialRayReuseSolver::solve` vs `FusedRayReuseSolver::solveStreaming` on the same `SimulationCase` (scale via `scaleCoherentCartesianPressure` on both sides) | `std::memcmp` over the full `pressure()` span bytes, per frequency |
| **D** | End-to-end product parity: `SHA256(SHD fused) == SHA256(SHD reuse)` per CLI matrix row | `shasum -a 256` of the written `.shd` files |

Method notes: Level B/C use `std::memcmp` over the span bytes (never `operator==`); size equality is asserted first and the first differing element (index + both complex values) is reported on mismatch. Level D runs the same binary on the same env with the same explicit `--frequencies-hz` CSV (taken verbatim from each generated `run_manifest.json`, so the profile linspace semantics are the generator's, not hand-computed); every row runs `--verify-cache`, and the munk rows add `--profile-influence` on **both** sides (frozen design §10.2 protocol, identical flags per row).

## 2. Component test (Levels A/B/C) — `rayreuse.component.fused_cc_parity`

New test `Bellhop_RayReuse/tests/component/fused_cc_parity_test.cpp` (CMake: `add_rayreuse_test`, labels `component;reuse`). One shared frozen fan per fixture via `SingleFrequencySolver::traceSourceFan`; Level B reference replays the exact reuse accumulation order (`for f in index order → solveFrequencyFromSourceCache(..., WorkspaceDelivery::Raw)`). Level B also asserts the timing fields exist per the frozen seam contract (`traceSeconds == 0`, `scaleSeconds == 0`, block phases >= 0) without asserting timing values.

| Fixture | Construction | Level B | Level C | Level A |
|---|---|---|---|---|
| A | Munk CC coherent (shared `makeMunkEnvironment` profile), 2F {50, 250} Hz, 7x5 grid, 61-ray ±12° fan, default `imageCount = 3` | PASS | PASS | PASS |
| A2 | Fixture A with `CartesianCervenySettings::imageCount = 2` (kernel dispatch coverage) | PASS | PASS | PASS |
| B | Constant-speed 100 m water, lossy reflecting acoustic half-space bottom (1700 m/s, power-law attenuation 10 dB/m @1 kHz ref, exponent 2), 2F {100, 1000} Hz, ±60° fan, 300 rays — **divergent per-frequency active prefixes** | PASS | PASS | PASS |
| C | Fixture A geometry with `BeamWidthMode::Wkb` (real epsilon; alternate KMAH branch rule) — reachable in-code via the `SimulationCase` constructor | PASS | PASS | PASS |

### Fixture B divergence evidence (non-vacuous D5 path)

The test projects every ray at both frequencies and derives each state's active prefix (first inactive point index; the `<0.005` cumulative cutoff). Observed on the passing run:

```text
fused-cc-parity fixture B (lossy halfspace): rays=300 divergent-prefix rays=96
cutoff-truncated rays=96 example ray 299 prefix(f=100)=263 prefix(f=1000)=313
```

96 of 300 rays have prefix(f=100) != prefix(f=1000) — e.g. ray 299: prefix 263 at 100 Hz vs 313 at 1000 Hz — so the union-prefix traversal, per-frequency left-endpoint active checks, and terminal retention are genuinely exercised in both directions (the union is not always driven by the same frequency). The test asserts `divergentRays > 0 && truncatedRays > 0`, so a degenerate fixture cannot pass vacuously. (Fan note: the requested 121 explicit rays are raised to 300 by the launch-fan planner's TL-mode minimum `kMinimumPhaseCriterionCount = 300`.)

## 3. CLI-level parity matrix (Levels A + D) — frozen 13-row list

Protocol per row: run `bellhop_rayreuse <env> --frequencies-hz <profile CSV> --execution-mode {reuse,fused} --verify-cache [--profile-influence on munk rows]` in a scratch dir; hash both `.shd`; compare; record fingerprints from both PRTs. Full hashes, frequency CSVs, and fingerprints are archived in `Bellhop_RayReuse/build/igr1-clean/parity/igr1_r05_parity_matrix.json` (per-row PRTs and SHDs under `.../parity/rows/`, scratch runs under `/tmp/igr1_r05_parity/`).

| # | Case | Profile (Nf) | SHD SHA256 (reuse == fused, 16-hex prefix) | Fingerprint (reuse == fused before == after) | Verdict |
|---|---|---|---|---|---|
| 1 | munk_cerveny_cc | broadband_smoke (2F) | `cf1f9711aefcab08` | 2271226459307825052 | PASS |
| 2 | munk_cerveny_cc | broadband_regression (16F) | `f01ee48119549a82` | 16716541753253518712 | PASS |
| 3 | constant_speed_direct | broadband_smoke (2F) | `acd9a2a6730a6124` | 11632125087325642441 | PASS |
| 4 | constant_speed_direct | broadband_regression (16F) | `edc818ea763eea92` | 13917212041181755229 | PASS |
| 5 | constant_speed_thorp | broadband_regression (16F) | `c8ef3fad90e32753` | 4134998748544866669 | PASS |
| 6 | volume_attenuation_francois_garrison | broadband_smoke (2F) | `2837277287c8ccd3` | 4134998748544866669 | PASS |
| 7 | constant_speed_vacuum_rigid | broadband_smoke (2F) | `df1b18f9c8fea190` | 3486598293779455808 | PASS |
| 8 | elastic_halfspace_flat | broadband_smoke (2F) | `8bd688cc6fcf6a04` | 15036134024010061163 | PASS |
| 9 | cerveny_width_wkb_flat_gradient | broadband_smoke (2F) | `833a453575a04169` | 925351105865613188 | PASS |
| 10 | cerveny_width_space_filling_flat_gradient | broadband_smoke (2F) | `4604c5c27eb68bbe` | 925351105865613188 | PASS |
| 11 | cerveny_curvature_zero_flat_gradient | broadband_smoke (2F) | `0f71c5ed291cdaaac` (17-hex prefix; full hash in JSON) | 12252141946749303168 | PASS |
| 12 | cerveny_curvature_double_flat_gradient | broadband_smoke (2F) | `f22cdec2e90a44e19b` | 11714653119191126191 | PASS |
| 13 | munk_spline | broadband_smoke (2F) | `74028065178ff80d` | 1526667602348633172 | PASS |

Coverage check against the worklist R05 acceptance: direct/simple CC (3, 4); Munk spline CC (13); lossy volume attenuation at scale (5, 6); reflection/polarity cases (7 vacuum/rigid, 8 frequency-dependent elastic half-space); curvature/epsilon variants (9-12, plus component Fixture C). Cross-checks: row 2 reuse SHA equals the R01/R03 anchor (below); row 13 fingerprint equals the frozen munk_spline geometry anchor `kMunkSplineFingerprint` in `serial_ray_reuse_solver_test.cpp`; rows 5 and 6 share one frozen fingerprint (same geometry, different frequency-local attenuation — cache excludes environment payload as designed).

Frequency CSVs used (from `run_manifest.json`): rows 2/4 = `50,80,...,500` (step 30, 16 values); row 5 = `1000,1600,...,10000` (step 600, 16 values); rows 6/8 = explicit pairs `5000,10000` / `1000,2000`; rows 7/9-11 = `100,500` / `100,250`; rows 1/13 = `50,250`.

Missing envs were generated with `codes/standard_cases.py generate --version rayreuse --case <case> --profile broadband_smoke --executable ../../Bellhop_RayReuse/build/igr1-clean/bellhop_rayreuse` (two curvature smoke rows; artifacts under `test/standard_cases/results/rayreuse/`).

## 4. Seam-neutrality proof (WorkspaceDelivery inert for existing callers)

Within this matrix run, row 2's **reuse-mode** 16F SHD SHA256 is still

```text
f01ee48119549a82e79798322bf5227d8fc95054be82de955de5ccadef057c2c
```

— identical to the R01 pre-instrumentation anchor and the R03 post-seam record, re-confirming at R05 build `95b820f2...` that the Level-B seam (and the fused surface) leaves the reuse path byte-identical (full `uv run ctest` green, 43/43, including all pre-existing tests).

## 5. Gates

- Build clean in `build/igr1-clean` (release).
- `uv run ctest --test-dir build/igr1-clean --output-on-failure -R "fused_cc_parity|fused_solver"`: 2/2 PASS (`rayreuse.component.fused_solver` 0.01 s, `rayreuse.component.fused_cc_parity` 0.45 s).
- Full `uv run ctest --test-dir build/igr1-clean --output-on-failure`: **100% tests passed out of 43** (42 pre-existing + the new parity test; no existing test weakened or removed).
- CLI matrix: **13/13 rows PASS** (Level D identical SHDs; Level A fingerprints reuse == fused before == after).
- Seam neutrality: PASS (§4).
- `git diff --check`: clean. R05 diff scope: `Bellhop_RayReuse/tests/component/fused_cc_parity_test.cpp` (new), one `add_rayreuse_test` block in `Bellhop_RayReuse/CMakeLists.txt`, this report, and generated standard-case env artifacts under `test/standard_cases/results/rayreuse/cerveny_curvature_{zero,double}_flat_gradient/`.

## 6. Blockers

None. Levels B and C were fully exposed via the frozen seams; Fixture B prefixes genuinely diverge (96/300 rays); no fallback or SHD-only substitution was needed.
