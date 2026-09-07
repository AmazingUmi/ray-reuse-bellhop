# IGR-1 — Cross-Frequency Cartesian Cerveny Influence Fusion — BATCH ACCEPTANCE

> **Batch:** IGR-1 (CC fused influence, serial reference path, Bf=Nf)
> **Date:** 2026-09-02
> **Branch:** `feat/igr-influence-geometry-reuse`; base HEAD `3ed475e` (clean at start); IGR-1 delivered as uncommitted working tree (23 files)
> **Build:** `Bellhop_RayReuse/build/igr1-clean` (release), binary SHA-256 `95b820f2…`
> **Worklist:** [`../worklists/IGR-1_CC_FUSION_WORKLIST.md`](../worklists/IGR-1_CC_FUSION_WORKLIST.md) (R01–R06 all DONE)
> **Design:** [`../worklists/DESIGN_IGR1_CC_FUSION.md`](../worklists/DESIGN_IGR1_CC_FUSION.md) (reviewer PASS)

## 1. Acceptance checklist (coordinator-verified)

| Item | Status | Evidence |
|---|---|---|
| Working tree scope | PASS | 23 files, all within declared R01–R06 scope (§2); `git diff --check` clean; `Bellhop_origin/` + `Bellhop_F2CPP/` untouched (0 entries) |
| Full CTest | PASS | 43/43 (build/igr1-clean) — includes new `rayreuse.component.fused_solver` and `rayreuse.component.fused_cc_parity` |
| Python tests | PASS | `uv run pytest`: 191 passed |
| Standard-case unit tests | PASS | `unittest discover codes/tests`: 176 OK |
| Level A cache fingerprint | PASS | fused `--verify-cache` before==after==reuse on every row; munk_spline row fingerprint equals frozen anchor `1526667602348633172` |
| Level B raw workspace | PASS | memcmp bitwise per frequency, 4 C++ fixtures (Munk CC 2F; imageCount=2; WKB; **divergent-prefix lossy fixture**: 96/300 rays prefix 263 vs 313) |
| Level C scaled workspace | PASS | memcmp bitwise after production scaling, same fixtures |
| Level D SHD SHA-256 | PASS | 13/13 CLI rows reuse==fused (munk 2F/16F, direct 2F/16F, thorp 16F, FG, vacuum-rigid, elastic, wkb/space-filling/curvature variants, munk_spline) |
| Existing paths unchanged | PASS | munk 16F reuse SHA256 still `f01ee481…` (R01 anchor) after all instrumentation+seam+fused changes; narrowed CLI catch-alls keep identical strings/outcomes for reuse/parallel; seam parameter defaulted and inert |
| Benchmark archive | PASS | `build/igr1-clean/benchmarks/igr1_r06_munk_{2f,8f,16f}.json` + counter PRTs |
| Memory measurements | PASS (partial per user decision) | 2F/8F/16F: ΔRSS ≈ +1.9/+12.7/+27.0 MiB ≈ Nf × 1.61 MB; frozen cache bytes constant; 32F/64F DEFERRED (user, runtime budget) |
| Counter behavior | PASS | geometry counters exactly 1/Nf; frequency-kernel counters exactly 1.0; window/taper/nonzero identical |
| Reviewer checkpoints | PASS with one caveat | R02/R03/R04/R05 independent reviewer PASS (R02 after one remediation loop). R06 verified by coordinator (subagent infra outage, recorded in worklist §11); independent examination discharged at final review |

## 2. Changed files

**Production (9):** `include/rayreuse/field/cartesian_cerveny_influence.hpp`, `src/field/cartesian_cerveny_influence.cpp` (fused kernel + counters), `include/rayreuse/solver/fused_ray_reuse_solver.hpp`, `src/solver/fused_ray_reuse_solver.cpp` (new fused orchestration), `include/rayreuse/solver/single_frequency_solver.hpp`, `src/solver/single_frequency_solver.cpp` (WorkspaceDelivery seam), `include/rayreuse/io/command_line.hpp`, `src/io/command_line.cpp` (Fused enum/token), `app/main.cpp` (validation/dispatch/PRT), `CMakeLists.txt` (new sources/tests).

**Tests (4):** new `tests/component/fused_ray_reuse_solver_test.cpp`, `tests/component/fused_cc_parity_test.cpp`; extended `tests/component/cartesian_cerveny_influence_test.cpp`, `tests/unit/command_line_test.cpp`.

**Benchmark/test infra (4):** `test/standard_cases/codes/benchmark_rayreuse.py` (stale parse fix + fused mode), `standard_cases.py` (fused mode registration), `model_matrix.py` (default modes pinned), `codes/tests/test_benchmark_rayreuse.py` (fixtures).

**Docs (5):** worklist + frozen design + R01/R05/R06 reports.

## 3. Headline results

- **Correctness: complete.** Fused is bitwise-identical to the existing reuse path at every level observable (raw workspace, scaled workspace, SHD bytes, cache fingerprint), across 13 CLI cases and 4 C++ fixtures including divergent per-frequency active prefixes.
- **Geometry dedup: complete.** Geometry evaluations exactly 1/Nf of the frequency-major baseline (counters), frequency-local kernel work unchanged (exactly 1.0).
- **Wall time: NO improvement** (the research question's answer is negative on this machine/case): fused/reuse speedup 0.70 (2F) / 0.96 (8F) / 0.93 (16F); dispersion ≪ gaps. Attribution in R06 §8: geometry is cheap pipelined arithmetic, not the bottleneck; the fused layout trades it for worse memory locality (Nf scattered state arrays + Nf workspace write streams per depth/image) and per-frequency eligibility branching. Parallel (8 workers) remains the wall-time lever: 4.3× vs reuse at 16F.
- **Memory: as modeled.** ΔRSS ≈ Nf × 1.61 MB; no cache growth; no OOM.
- **Verdict (R06 frozen escape gate):** `NOT_VIABLE` for wall-time generalization of the v1 fused layout. The fused path remains an opt-in experimental execution mode (`--execution-mode fused`); it does not become a default or required production path.

## 4. Contract compliance

- RayReuse frozen-cache contract (AGENTS.md §9): `RayPathCache` immutable, frequency-independent, zero write-back — verified by fingerprint gates on every fused run.
- Frequency-local boundary: no frequency-local state persisted anywhere; per-ray temporaries only.
- No persistent geometry cache exists (transient reuse only); no `Nray × Nsegment × Nreceiver` structure.
- Accumulation-order HARD GATE: reviewer item-by-item diff-check (R04) + bitwise Level B/C/D prove the addition stream is unchanged.

## 5. Deviations / deferrals

1. 32F/64F memory rows deferred by user decision (2026-09-02) — recorded in R06 report; not a correctness gate.
2. R06 independent reviewer checkpoint replaced by coordinator verification due to subagent infrastructure outage (provider configuration error); recorded in worklist §11; final-reviewer re-examines the batch as a whole.
3. Minor frozen-interface deviations (mirror statistics struct, bool kernel return, rawWorkspaces field name) — architect-ratified in the design doc.
4. Uniform-range fused rejection placement (env parser + CC ctor as last line of defense instead of a dedicated fused CLI message) — architect-ratified.

## 6. Batch acceptance verdict

**PASS** — all worklist tasks closed with evidence; all mandatory gates green; the NOT_VIABLE performance verdict is a legitimate, fully-evidenced outcome of the frozen Go/No-Go protocol, not a gate failure. Ready for independent final review.
