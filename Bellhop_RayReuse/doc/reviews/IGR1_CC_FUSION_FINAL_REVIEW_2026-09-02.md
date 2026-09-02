# IGR-1 Final Review — Cross-Frequency Cartesian Cerveny Influence Fusion

> **Batch:** IGR-1 · **Date:** 2026-09-02 · **Reviewer:** independent final-reviewer (fresh instance; no authorship in the batch)
> **Inspected state:** branch `feat/igr-influence-geometry-reuse`, base HEAD `3ed475e` + uncommitted IGR-1 working tree (23 files), build `Bellhop_RayReuse/build/igr1-clean` (release, binary SHA-256 `95b820f2…`)
> **Inputs read:** frozen worklist `../worklists/IGR-1_CC_FUSION_WORKLIST.md`; frozen design `../worklists/DESIGN_IGR1_CC_FUSION.md`; reports R01/R05/R06 + batch acceptance; full working-tree diff; archived parity matrix and benchmark JSONs/counter PRTs.

## Verdict

**ACCEPTED**

## Independent verification performed

1. **Scientific correctness (existing paths).** Diff audit: existing CC kernel changes are counter increments inside `if constexpr (CollectStatistics)` plus the additive fused kernel; `WorkspaceDelivery` seam is a defaulted trailing parameter gating only the coherent scale; CLI catch-all narrowing keeps byte-identical reuse/parallel strings. Live runs: reuse-mode munk 2F SHD SHA256 `cf1f9711…` equals the pre-batch anchor; fused mode produces the same hash; fingerprint before==after==reuse (`2271226459307825052`); fused single-frequency rejection deterministic. ctest 43/43; pytest 191; standard-cases unittest 176 OK; harness suite 24 OK.
2. **Numerical correctness (Levels A–D).** `fused_cc_parity_test.cpp` uses real `std::memcmp` over full pressure spans; Level B through the Raw seam vs `accumulateFrequencies`; Level C through both production solvers; Level A fingerprints. Divergent-prefix fixture reproduced live (`rays=300 divergent=96 … prefix 263 vs 313`) with hard assertions. All 13 parity-matrix rows: reuse==fused SHA, fingerprint before==after==reuse. Item-by-item kernel diff-check vs `accumulateImpl` confirmed union-bound/activeMask equivalence, terminal retention, P1 (unconditional zero imageSum adds), P6 (one read-add-assign per depth/f), P3 (own-prefix precompute), identical evaluation order everywhere.
3. **Architecture.** Genuine in-traversal cross-frequency transient fusion: geometry hoisted once per segment/range/image; all frequency physics per frequency; per-ray scratch only; the only Nf long-lived state is field workspaces (D6). Not a persistent cache, not a frequency-major wrapper — confirmed by counters (one traversal's worth of geometry work).
4. **Performance claims.** Recomputed medians: 2F 7.89 vs 11.24; 8F 49.16 vs 51.04; 16F 95.66 vs 103.04 (fused slower everywhere, gaps ≫ dispersion ≤1.4 %). Counters: geometry reuse/fused exactly 2.000/8.000/16.000; frequency-kernel counters exactly 1.000. RSS deltas ≈ Nf × 1.61 MB; cross-mode SHD identity one hash per row. `NOT_VIABLE` (wall-time generalization of the v1 layout) is the frozen escape gate's legitimate, correctly-scoped outcome.
5. **Scope.** All 23 files map to declared R01–R06 tasks; no parallel fused / blocking / multisource / other beam families / frequency interpolation / persistent caches / schema change / unrelated refactor. Origin and F2CPP untouched; git scope clean.
6. **Process.** R02 (after one documented remediation loop), R03, R04, R05 reviewer PASS records present. R06 reviewer-outage caveat honestly recorded (worklist §11) and **discharged by this review** (all R06 headline numbers recomputed from archives; harness unittests re-run; diff re-verified). Documentation does not overclaim: no "IGR complete" language; NOT_VIABLE scoped to wall-time while correctness is stated as fully preserved with evidence.

## Minor notes (non-blocking, cosmetic)

1. R05 report table row 11 prints a 17-hex SHA prefix vs 16 elsewhere (full hashes in the archived JSON).
2. R06 report §10 shows an illustrative 64F reproduction command while §1/§5/§9 mark 32F/64F DEFERRED (user decision, runtime budget).

## Closure statement

IGR-1 is closed as: **Cartesian Cerveny · single-source · coherent TL · serial fused reference — implemented, bitwise-identical, and measured; wall-time generalization of the v1 fused layout judged NOT_VIABLE on the tested machine/case.** This does not declare Influence Geometry Reuse complete; any IGR-2 decision starts from the R06 data.
