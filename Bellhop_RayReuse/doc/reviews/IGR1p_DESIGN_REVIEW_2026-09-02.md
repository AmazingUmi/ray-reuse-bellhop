# IGR-1p Design Review — Fused Layout & Memory Optimization

> **Date:** 2026-09-02
> **Role:** independent reviewer; no production authorship
> **Inspected implementation:** `749741d9eba8559ecdd286fb7080ceabd7e0b605`
> **Reviewed documents:** `IGR-1p_LAYOUT_MEMORY_OPTIMIZATION_WORKLIST.md` and `DESIGN_IGR1p_LAYOUT_MEMORY_OPTIMIZATION.md`

## Verdict

**PASS — independent re-review completed 2026-09-02.**

The remediated design is sufficiently frozen to enter `CONSTRUCT`. The scope
guard, primary point-major layout, block ownership/loop order, per-frequency
addition-order contract, Levels A-D, Bf matrix/clamp, counter ratios, P05/P06
field-ownership distinction, P06 activation gate, benchmark isolation, and
bounded stop rule are suitable.

## Initial findings (all resolved)

### 1. [HIGH] Clean wall-time samples and instrumented counter samples are not explicitly separated

**Location:** `DESIGN_IGR1p_LAYOUT_MEMORY_OPTIMIZATION.md` §8.1, lines 386-410;
§10, lines 475-487.

The design requires every reported row to include wall/Influence and detailed
geometry/frequency counters, but it does not freeze those as separate
invocations. In this implementation, `--profile-influence` selects the
statistics-collecting fused template and performs counter increments and
additional timing inside the hot traversal. IGR-1 R06 therefore measured
counters in separate profiled runs and explicitly excluded their Influence
times from the wall verdict (`REPORT_IGR1_R06_PERFORMANCE.md` §1, §7).
Allocation tracing is separated in §8.2, but influence profiling is not.

**Required correction:** freeze two protocols for every relevant
mode/Nf/layout/Bf row: (a) clean one-warmup/five-repeat timed runs with influence
profiling disabled, used for end-to-end wall and phase verdicts; and (b)
separate `--profile-influence` diagnostic runs used only for geometry and
frequency-local counters. Require identical case/frequency/Bf/candidate
identity across the pair, label profiled Influence as instrumented, and forbid
using it for retention or viability decisions.

### 2. [MEDIUM] The projected-state byte model incorrectly uses active-prefix lengths

**Location:** `DESIGN_IGR1p_LAYOUT_MEMORY_OPTIMIZATION.md` §2.2, lines 127-146.

`Pf` is defined as the retained active-prefix point count and is correctly used
for q/gamma/KMAH precompute. It is not the size of
`RayFrequencyState::points`. `FrequencyProjector::project()` reserves
`path.points.size()` and appends one projected point for every path point even
after `active` becomes false (`src/field/frequency_projector.cpp`, lines
107-108 and 117-197). Only the later fused precompute truncates at the first
inactive point (`src/field/cartesian_cerveny_influence.cpp`, lines 1152-1167).
Consequently, as-built projected-state payload is approximately
`sizeof(RayFrequencyPoint) * F * Ppath`, not
`sizeof(RayFrequencyPoint) * sum(Pf)`. A blocked P05 implementation should use
approximately `sizeof(RayFrequencyPoint) * B * Ppath` for its block-local
projected-state payload. The current formula undercounts divergent/truncated
rays and weakens the required allocation/working-set audit.

**Required correction:** distinguish full path point count `Ppath` from
per-frequency active prefix `Pf` throughout the static model; update L0/P05
projected-state, precompute, and block-working-set formulas accordingly. Also
include the solver epsilon-vector payload in the stated O(F)/O(B) scratch
inventory, or explicitly list it among omitted payloads. Keep vector headers,
capacity slack, and alignment separately labelled as already intended.

### 3. [MEDIUM] Shared SSP-sample evaluation is outside the frozen layout/blocking study and confounds attribution

**Location:** `DESIGN_IGR1p_LAYOUT_MEMORY_OPTIMIZATION.md` §4.2, lines 264-269.

The proposed optional evaluation of one shared geometry SSP sample per point
reduces computation count. It is not a data-layout transformation, scratch
reuse, mask experiment, projector scratch reuse, or frequency blocking. The
user's hard scope limits IGR-1p to those mechanisms, and its core question is
whether `Bf=Nf` plus fragmented layout explains the wall failure. Even as a
separate benchmark row, SSP-evaluation dedup introduces a different causal
variable and exceeds the frozen experiment list.

**Required correction:** remove shared SSP-evaluation dedup from IGR-1p
construction authority and retain the existing per-lane precompute evaluation
sequence. Direct writes into a point-major destination remain in scope. Any
future compute-dedup experiment requires an explicit scope decision rather than
being activated by an implementation review.

## Scope and hygiene check

- HEAD resolves to `749741d9eba8559ecdd286fb7080ceabd7e0b605` on
  `feat/igr-influence-geometry-reuse`.
- The working tree contains only the two new IGR-1p design/worklist documents
  at review time; no production, Origin, F2CPP, or test file has been modified.
- The historical ratio-label reversal is handled correctly: the reviewed
  design uses the raw reuse/fused wall times and does not infer from the
  mislabeled ratios.

## Re-review — 2026-09-02

The same independent reviewer re-read the revised design and worklist against
the original request and the three findings above:

1. **Finding 1 resolved.** Design §8.1 now freezes a clean timing protocol with
   profiling disabled (one warmup plus at least five repeats) and a separate,
   matching `--profile-influence` diagnostic invocation. Instrumented
   wall/Influence values are explicitly barred from retention, attribution,
   and viability decisions. The risk register and worklist carry the same
   separation.
2. **Finding 2 resolved.** Design §2.2 now defines full `Ppath` separately from
   active-prefix `Pf`; uses `sizeof(RayFrequencyPoint)*F*Ppath` for as-built
   projected state and `*B*Ppath` for P05; retains `sum(Pf)` only for
   precompute; and accounts for `16*F` / `16*B` epsilon payloads. The P05
   ownership pseudocode matches the corrected block-local model.
3. **Finding 3 resolved.** Design §4.2 now requires the original ascending-lane
   per-frequency `soundSpeedProfile_.evaluate` and p/q/gamma/KMAH sequence.
   Shared SSP evaluation/dedup is explicitly outside IGR-1p construction
   authority; point-major direct writes remain the isolated layout variable.

No residual finding remains. The revisions are documentation-only; production,
Origin, and F2CPP sources remain unchanged from inspected HEAD `749741d`.

**Final design-review verdict: `PASS`.** The coordinator may transition the
batch from `DESIGN / NOT_IN_CONSTRUCTION` to `CONSTRUCT` under the frozen
worklist and normal checkpoint-review requirements.
