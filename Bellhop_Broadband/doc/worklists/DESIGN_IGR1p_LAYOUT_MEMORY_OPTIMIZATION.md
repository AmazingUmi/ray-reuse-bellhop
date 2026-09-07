# IGR-1p — Fused Layout & Memory Optimization Feasibility — FROZEN DESIGN

> **Status:** ARCHITECT-FROZEN / DESIGN REVIEW PASS / READY FOR CONSTRUCTION
> **Date:** 2026-09-02
> **Branch / inspected implementation:** `feat/igr-influence-geometry-reuse`, `749741d9eba8559ecdd286fb7080ceabd7e0b605`
> **Execution authority:** [`IGR-1p_LAYOUT_MEMORY_OPTIMIZATION_WORKLIST.md`](IGR-1p_LAYOUT_MEMORY_OPTIMIZATION_WORKLIST.md)
> **Construction gate:** satisfied by independent re-review `PASS` on 2026-09-02; construction has not started in this design turn. A later implementation finding still requires remediation and re-review under the worklist.

> **Design-review remediation (2026-09-02):** all three findings in
> `doc/reviews/IGR1p_DESIGN_REVIEW_2026-09-02.md` are incorporated below:
> clean timing and profiled counter invocations are now separate; `Ppath` and
> `Pf` models plus epsilon payloads are explicit; shared SSP-evaluation dedup
> has been removed from construction authority. Independent re-review closed
> all three findings with final verdict `PASS`.

## 1. Question, scope, and non-goals

IGR-1p answers one question without presupposing the answer:

> Is the IGR-1 serial fused wall-time failure mainly caused by its `Bf=Nf`
> working set and fragmented per-frequency layout, and can controlled layout
> and blocking changes recover a stable wall-time advantage without changing
> acoustic physics or per-frequency accumulation semantics?

The only allowed production domain is the existing opt-in fused path for
Cartesian Cerveny, coherent TL, a rectilinear uniform-range receiver grid,
one source, serial execution, and the existing frozen ray fan. Allowed work is
fused-specific layout, scratch ownership/reuse, explicit frequency blocking,
measurement instrumentation, benchmark support, and the conditional P06
workspace experiment.

The following remain out of scope: parallel fused, multisource, Arrival or
Eigenray products, other beam families or coordinate systems, frequency
interpolation, persistent influence-geometry caches, changes to
`RayPathCache`, GPU work, SIMD/approximate math, fast-math, reassociation,
changed transcendental functions, projector/reflection/attenuation semantics,
and unrelated refactoring. The general per-frequency CC kernel and
`PrecomputedRayValues` need not and should not be redesigned for IGR-1p.

This batch is IGR-1 performance/layout follow-up only; it is not IGR-2.

## 2. Audited as-built implementation

Source anchors are the inspected `749741d` tree:

- `src/solver/fused_ray_reuse_solver.cpp`: allocates all `Nf`
  `FrequencyWorkspace` objects before ray traversal; for each ray it projects
  all frequencies, computes all epsilons, and calls one all-frequency fused
  kernel.
- `src/field/frequency_workspace.cpp`: every workspace owns a separate
  `std::vector<std::complex<double>> pressure_`, indexed depth-major as
  `pressure_[depth * Nr + range]`.
- `src/field/cartesian_cerveny_influence.cpp`: every fused call constructs
  per-frequency `PrecomputedRayValues`, two `vector<bool>` masks, six complex
  lane arrays, prefix/omega/radius arrays, then executes the shared geometry
  traversal with frequency as the innermost physics loop.
- `src/field/frequency_projector.cpp`: `project()` returns a newly constructed
  `RayFrequencyState`; its `points` vector reserves and allocates for the
  current ray. Move-assignment into the solver's state vector transfers that
  new allocation and releases the previous ray's allocation. Therefore the
  outer `frequencyStates` capacity is reused, but the nested points capacities
  are not reused by the current API.

### 2.1 As-built layout and access diagram

```text
solveStreaming / accumulateFrequencies lifetime

frozen RayPathCache                         read-only, one trace pass
  paths[r].points/steps/events              geometry-major, shared

workspaces: vector<FrequencyWorkspace>      one object-vector allocation
  [f0] -> pressure_[cell 0..C-1]            separate allocation, 16*C bytes
  [f1] -> pressure_[cell 0..C-1]            separate allocation, 16*C bytes
   ...
  [fN] -> pressure_[cell 0..C-1]            separate allocation, 16*C bytes
                                             lifetime: all frequencies until
                                             scale/delivery

per ray, projected state
  frequencyStates[f].points[point]          separate allocation per f
    { complex tau; double amplitude;
      double reflectionPhase; bool active } approximately 40 bytes/point

per fused kernel call
  ray[f] = PrecomputedRayValues
    p[point]       separate allocation       16 bytes/point; precompute-only
    q[point]       separate allocation       16 bytes/point; hot-loop input
    gamma[point]   separate allocation       16 bytes/point; hot-loop input
    kmah[point]    separate allocation        4 bytes/point; hot-loop input

  activePrefix[f], omega[f], radiusMax[f]
  activeMask[f], rangeEligible[f]            vector<bool> proxy-packed
  q/tau/gamma/principal/corrected/imageSum   six separate complex arrays

hot access at fixed (segment, range, depth, image)
  for f:
    ray[f].q[left/right] -> ray[f].gamma[left/right] -> ray[f].kmah[left]
    frequencyStates[f].points[left/right]
    workspaces[f].pressure()[same cell] += contribution

The frequency-inner loop therefore crosses f-owned allocations for both ray
state and output fields. Consecutive logical lanes are not consecutive bytes.
In addition, the traversal holds range fixed while advancing depth, whereas a
workspace is indexed `depth * Nr + range`; this is a stride-`Nr` store pattern
in both reuse and fused. Fused adds B independent strided store streams to that
shared inherited pattern. IGR-1p must attribute the incremental fused cost,
not claim the inherited stride alone explains the cross-mode gap.
```

`C = receiverDepthCount * receiverRangeCount`; on the primary 201 x 501 grid,
one complex field is `201 * 501 * 16 = 1,611,216` bytes (about 1.54 MiB),
consistent with the IGR-1 observed approximately 1.61 MB/frequency model.

### 2.2 Visible allocation and lifetime model

For non-empty rays, excluding allocations internal to environment/acoustics
objects, the source-visible lower bound is:

- solve lifetime: one `workspaces` object-vector allocation plus `Nf`
  pressure allocations; one `frequencyStates` object-vector and one epsilon
  vector allocation;
- each projected ray: at least `Nf` new `RayFrequencyState::points`
  allocations from `FrequencyProjector::project()`;
- each fused influence call: `4*Nf` precompute-array allocations plus twelve
  top-level vector allocations (`activePrefix`, the `ray` object vector,
  omega, radius, two masks, and six complex scratch arrays).

Thus the visible fused project+influence lower bound is approximately
`5*Nf + 12` allocation calls per ray, before library/internal allocations.
This count is a static audit result, not an allocator trace; P02/P03 must
measure it rather than quote it as an exact process-wide count.

Let `Ppath = path.points.size()` be the full frozen path point count. Projector
output always contains `Ppath` points for every frequency, including the
inactive suffix. Let `Pf` be the shorter retained active-prefix point count for
frequency `f` (including the first inactive terminal point), `Pmax` the maximum
`Pf` in the active frequency block, `F=Nf`, and `B` the clamped block size. The
as-built all-frequency payload model, excluding vector headers/capacity
slack/alignment, is:

```text
long-lived output fields       ~= 16*C*F
projected ray state            ~= sizeof(RayFrequencyPoint)*F*Ppath
precompute arrays              ~= (3*16 + 4)*sum(Pf) = 52*sum(Pf)
epsilon vector                 ~= 16*F
frequency hot scratch          ~= (6*16 + 2*8 + 8)*F + masks
```

`p` contributes `16*sum(Pf)` bytes and is never read after precompute.
Removing it leaves nominal q/gamma/kmah payload of `36*sum(Pf)` bytes.

For a P05 block, projected-state payload is approximately
`sizeof(RayFrequencyPoint)*B*Ppath`; q/gamma/kmah precompute payload before p
removal is `52*sum_block(Pf)` and after p removal is `36*sum_block(Pf)` (a
rectangular point-major capacity may reserve up to `36*Pmax*B`). Epsilons use
`16*B`, and prefix/omega/radius/masks/complex lane arrays are O(B). All `F`
output fields still exist in P05, so the total resident model remains:

```text
M ~= M_frozen + F*M_field + block-local scratch
```

Only a P06 streaming/ownership experiment can approach a resident field model
bounded by `B` (in practice `B` staging fields plus up to one delivery field).

## 3. Findings and hypotheses

The following are audited facts:

1. IGR-1 medians are reuse/fused 7.89/11.24 s (2F), 49.16/51.04 s
   (8F), and 95.66/103.04 s (16F). Historical ratios labelled
   `fused/reuse = 0.70/0.96/0.93` have the direction reversed; raw wall times
   are authoritative.
2. The wall gap is in Influence; Project is much smaller (0.03/0.23/0.46 s
   fused at 2F/8F/16F in IGR-1).
3. The hot frequency loop jumps among per-frequency q/gamma/kmah and pressure
   allocations. `vector<bool>` adds proxy/bit operations. `p` is
   precompute-only. Fused kernel scratch is allocated afresh per ray.
4. The current solver retains `F` complete fields, regardless of any future
   computation block size.
5. Geometry counters fell to exactly `1/F` on the measured Munk rows while
   frequency-local counters remained equal to reuse, yet wall time did not
   improve. Geometry is cheap relative to the retained physics on this case.

The following remain hypotheses and must not be reported as established
causes before controlled measurement:

- H1: point-major frequency-contiguous state reduces cache/TLB misses enough
  to improve Influence time;
- H2: a smaller `B` improves the hot working set enough to repay
  `ceil(F/B)` geometry traversals;
- H3: cached pressure bases or block-interleaved pressure reduce workspace
  pointer chasing/store-stream cost;
- H4: masks or allocation churn are material contributors rather than minor
  effects hidden by transcendental physics;
- H5: any locality benefit survives end-to-end wall measurement and is not
  merely an Influence microbenchmark result.

## 4. Frozen experiment order and rollback discipline

Every experiment starts from the last retained, parity-clean state. It is
benchmarked as one variable against that state. No two unmeasured candidates
may be combined. A candidate is retained only if Levels A-D pass and it has a
repeatable wall/Influence benefit beyond dispersion, or a specifically stated
non-wall benefit (allocation/working-set/RSS) that justifies its complexity.
Otherwise it is reverted before the next experiment. Reverted results remain
documented.

### 4.1 P03 low-risk sequence

1. **M1 — masks:** benchmark the unchanged `vector<bool>`,
   `vector<uint8_t>`, and, for actual block size `B <= 64`, `uint64_t` bit
   masks for both active and range-eligible state. The bit-mask candidate must
   still execute the same ascending frequency-lane loop; it may only replace
   storage/tests. For `B > 64`, retain the winning general representation or
   use `uint8_t`; do not expand IGR-1p into a general dynamic-bitset project.
   No mask choice is accepted from theory alone.
2. **M2 — remove persistent `p`:** compute `p` as a local during precompute,
   retain only q/gamma/kmah, and prove identical q/gamma/kmah bytes or final
   Levels A-D. Do not change the p/q/gamma formulas or evaluation order.
3. **M3 — caller-owned fused scratch:** move fused-only vectors to an explicit
   scratch object owned by the serial fused solver/accumulation call and reuse
   capacity across rays. The scratch is passed explicitly; no global,
   thread-local, static mutable, or mutable `CartesianCervenyInfluence` state
   is allowed. Every resized element read by the kernel must be overwritten
   first. Record capacity bytes and growth events.
4. **M4 — cached pressure bases:** once workspaces are stable, cache one
   `std::complex<double>*`/span base per active lane per block outside the
   depth/image store site. Pointer validity is bounded by the block call; no
   workspace reallocation is allowed while bases are live.
5. **M5 — projector `projectInto` (conditional, low priority):** investigate
   only if allocation tracing shows projection churn is material or Project
   exceeds 5% of end-to-end wall after earlier changes. A candidate may reuse
   each lane's points capacity but must call the same projection, reflection,
   attenuation, and active-cutoff formulas in the same order. Do not refactor
   projector semantics for elegance. Otherwise record it as deferred.

M2/M3 alter memory ownership and are treated as ADVANCED despite their small
surface. M1 and M4 are STANDARD if mechanically isolated; M5 is ADVANCED if
activated because it touches frequency-local projection lifetime.

### 4.2 P04 fused-specific layouts

Compare the retained fragmented layout against these fused-only candidates in
separate rows:

```text
L0 (as-built, frequency-major fragmented)
  q[f][point], gamma[f][point], kmah[f][point]

L1 (point-major, frequency-contiguous flattened SoA/AoSoA block)
  q[point * B + lane]
  gamma[point * B + lane]
  kmah[point * B + lane]

L1b (point-major projected hot state, layered only after measuring L1)
  tau[point * B + lane]
  amplitude[point * B + lane]
  reflectionPhase[point * B + lane]
  active[point * B + lane]

L2 (point-major record tile, only if L1/L1b leave a credible locality question)
  state[point * B + lane] = {q, gamma, kmah}
```

L1 is the primary required experiment: at a fixed left/right point its
frequency lanes are contiguous, while q/gamma/kmah stay in separate arrays.
It is an AoSoA in which the frequency block is the tile. L2 tests whether
co-locating fields used for one lane is preferable; padding and extra fetched
bytes must be reported. L1b separately tests the other fragmented Influence
inputs currently read through `frequencyStates[f].points[point]`. Populate L1b
by exact assignment after the unchanged projector first, so layout is not
confounded with projector semantics; include that transpose cost in wall and
Influence attribution. Only the conditional M5 experiment may later eliminate
the copy. Neither layout changes the general per-frequency
`PrecomputedRayValues` or projector path.

Precompute may write the point-major destination directly, but it must retain
the existing per-lane precompute evaluation sequence: for each ascending lane,
scan that frequency's prefix and perform the same per-point
`soundSpeedProfile_.evaluate`, p/q/gamma, and KMAH operations in the same order
as the IGR-1 fused kernel. Shared SSP-sample/evaluation dedup is explicitly out
of IGR-1p scope because it changes computation count and confounds the layout
question. Any future compute-dedup experiment requires a new explicit scope
decision.

P04 is ADVANCED and requires targeted parity/oracle evidence followed by an
independent reviewer `PASS` before its result becomes a retained base.

## 5. Frequency blocking architecture (P05)

### 5.1 Ownership and loop order

The fused solver owns block iteration. The influence kernel receives only a
contiguous block view and block-local scratch; it does not choose block size.
Blocks are contiguous and processed in ascending global frequency-index order.

```text
allocate F output FrequencyWorkspaces                 // P05, not P06
for blockBegin = 0; blockBegin < F; blockBegin += B:
    actualB = min(B, F - blockBegin)
    own actualB projected states of Ppath points each
    own actualB epsilons and block-local precompute/hot scratch
    for ray in frozen RayPathCache order:
        for lane = 0..actualB-1: project global frequency blockBegin+lane
        for lane = 0..actualB-1: pick epsilon
        fused accumulate one block, geometry once for this block
scale and deliver global frequencies 0..F-1 in ascending order
```

This deliberately changes cross-frequency scheduling from ray-major/all-F to
block-major/ray-major, but for each fixed frequency it preserves ray order and
the complete segment/range/depth/image/addition stream. The frozen cache is
read through `const&` and may be traversed more than once; it is never modified.

The production-facing default remains `B=F`, reproducing IGR-1. The internal
solver API gains a positive `frequencyBlockSize` setting. An absent setting
means `F`; a supplied value is clamped as:

```text
B = min(requestedFrequencyBlockSize, F), requested > 0
```

Zero is rejected. Larger values do not create duplicate benchmark rows.

### 5.2 CLI decision

Expose one experimental fused-only option,
`--fused-frequency-block-size <positive integer>`, because the required
end-to-end SHD/wall benchmark must be reproducible with the production
executable. It is rejected unless `--execution-mode fused` is selected.
Omission means `Bf=Nf`; no existing fused invocation changes behavior. Do not
expose mask/layout selectors: those are sequential construction experiments,
and only retained implementation remains in production.

`Bf=1` is a locality extreme, not an alias for reuse. It still uses the fused
solver, one shared frozen trace, fused-specific validation/layout/counters and
the fused kernel, while traversing geometry once per one-frequency block. It
must be labelled `fused Bf=1` in every report.

P05 is ADVANCED and requires a numerical-order review plus targeted evidence
and independent reviewer `PASS`.

## 6. Numerical-order and cache contract

Reuse is the oracle. Each candidate first attempts and is required to preserve:

- **Level A:** `RayPathCache` content fingerprint before equals after;
- **Level B:** raw workspace bytes equal reuse per frequency (`std::memcmp`);
- **Level C:** scaled workspace bytes equal reuse per frequency;
- **Level D:** SHD SHA-256 equals reuse.

For every fixed frequency, preserve exactly:

1. ray order from `RayPathCache::paths()`;
2. active-prefix definition, including retention of the first inactive point;
3. segment/right-index, crossed range, receiver depth, and image order;
4. image order True -> Surface -> Bottom;
5. p/q/gamma/KMAH, interpolation, window, taper, phase, exponential,
   contribution, corrected constant, and workspace-add expressions;
6. zero contribution additions within `imageSum` and exactly one
   read-add-assign to its workspace cell for each eligible ray/range/depth;
7. ascending frequency delivery after complete accumulation and identical
   scaling/consumer semantics.

Blocking is valid because, for a fixed frequency, the outer block selection
does not enter its arithmetic stream; ray order inside that block is unchanged.
Point-major storage only changes addresses. `p` removal is valid only when its
computed value is unchanged and used immediately to form the same gamma.
Scratch reuse is valid only when all live values are overwritten. Cached bases
only replace repeated address discovery. P06 staging is valid only when each
lane accumulates in the same ray order and the final transpose is a byte copy,
not a reduction.

If any candidate fails any level, stop and revert that candidate immediately.
IGR-1p does not authorize tolerance gates, reassociation, or a weaker oracle.
Passing SHD alone cannot excuse a Level B/C failure.

## 7. Counter semantics under blocking

Let `K = ceil(F/B)`. On identical active prefixes, geometry traversal counters
should be approximately K times one fused traversal and their ratio to reuse
should be:

```text
geometry ratio ~= ceil(F/B) / F
```

Examples: 16F/8 -> 0.125; 16F/4 -> 0.25. Divergent active prefixes, per-block
union bounds, early range exit, and degenerate segments can make exact legacy
counter relationships case-dependent; explain deviations rather than forcing
the formula.

Frequency range/image kernel evaluations, window/taper rejections, nonzero
contributions, and per-(ray,frequency) accumulation counts must equal reuse for
the same case. Increased geometry is deliberate and is not a correctness
failure. Counter aggregation uses global totals across all blocks; block-local
statistics are never presented as full-run counts.

## 8. Measurement plan

### 8.1 Baseline and mandatory matrix

Use a Release build, Apple M4 host, `munk_cerveny_cc`, the same executable
configuration/frequency list within a comparison. Every relevant
mode/Nf/layout/Bf row has two separate protocols with identical candidate,
case, frequency list, Bf, binary, and build identity:

1. **Clean timing protocol:** influence profiling/statistics collection is
   disabled. Run one warmup plus at least five measured repeats, rotating or
   alternating mode order to limit temporal bias. Report median, min, max, and
   MAD for end-to-end wall and clean phase timings (Trace, Project, Influence,
   Scale, SHD as available), plus clean-run peak RSS. Only these clean samples
   may decide candidate retention, performance attribution, or final
   viability.
2. **Diagnostic counter protocol:** run a separate matching invocation with
   `--profile-influence` enabled after the clean samples. Use it only for
   geometry/frequency-local counters and counter identities. Label its
   Influence and wall values as **instrumented**; neither may be mixed with
   clean samples or used for retention, attribution, or viability. One
   diagnostic run is sufficient for deterministic counters; repeat it only to
   investigate unexpected nondeterminism.

Mandatory rows:

| Nf | reuse | unchanged fused | blocking candidates |
|---:|---|---|---|
| 2 | yes | `Bf=2` | `Bf=1,2` |
| 8 | yes | `Bf=8` | `Bf=1,2,4,8` |
| 16 | yes | `Bf=16` | `Bf=1,2,4,8,16` |
| 32 | optional if runtime permits | `Bf=32` | `Bf=4,8,16,32` |

Requested sizes larger than Nf clamp and are not repeated. Include parallel
as a reference row only; it is not implemented or optimized by this batch.
The unchanged `Bf=Nf` row must be remeasured before candidate work and checked
against IGR-1 raw wall medians; explain drift before interpreting candidates.

Each reported row pairs clean end-to-end wall/Influence/Project/Scale/RSS with
the separately instrumented geometry and frequency-kernel counters, parity
Levels A-D, and build/HEAD identity. The report must retain distinct sample
labels and invocation records. Instrumented Influence and any other Influence
microbenchmark may diagnose but cannot decide viability.
Every M1-M4 and P04 retain/revert decision is measured at 2F, 8F, and 16F with
the same clean one-warmup/five-repeat protocol, plus its separate matching
counter invocation; a shorter diagnostic run may precede it but cannot justify
retention. M5 follows the same matrix if activated.

### 8.2 Allocation and byte measurement

Use three distinct evidence sources:

1. the static source-visible lower-bound inventory in section 2;
2. one representative allocation trace per retained layout/block family on
   macOS (Instruments Allocations or equivalent call-stack-capable tooling),
   filtered to projector/fused solver/fused influence frames; record allocation
   calls and cumulative requested bytes outside timed benchmark runs;
3. fused-specific diagnostics for scratch capacity bytes and capacity-growth
   events at explicit vectors, plus `sizeof` values used in the byte model.

Allocator tracing/instrumentation must not be enabled in wall-time samples.
If system tooling cannot attribute exact calls, label the static number as a
lower bound and the diagnostic number as managed scratch only; do not invent an
exact allocation total.

Peak RSS continues to use isolated child `ru_maxrss` (or the existing harness
equivalent). Keep these concepts separate in every report:

- **allocated bytes/calls:** cumulative allocator traffic;
- **working set/locality:** bytes/streams actively revisited by the hot loop;
- **peak RSS:** maximum process-resident pages, including frozen cache, fields,
  allocator slack, executable, and other state.

Blocking with all F output workspaces can improve working set without reducing
peak RSS. This exact sentence, or an equivalent unambiguous statement, is
required if P05 retains all F fields.

## 9. P06 conditional workspace experiment

P06 remains inactive unless P04/P05 evidence meets at least one activation
criterion:

- the best parity-clean candidate is statistically indistinguishable from
  reuse under the repeated-run dispersion, and profiling/allocation evidence
  identifies scattered pressure stores as a credible remaining limiter; or
- peak RSS/field ownership is the only material obstacle to an otherwise
  promising blocked result at Nf >= 16.

If activated, compare:

```text
A: existing pressure[f][cell] for the active block
B: block-local pressure[cell][lane], lane contiguous
```

For B, each cell/lane receives the same per-frequency ray-order addition
stream. After a block, deinterleave by exact assignment into frequency-major
delivery storage; include allocation, transpose/copy, scaling, and delivery in
end-to-end wall. The Level-B harness may retain all F outputs to compare bytes.
A production streaming variant may instead deliver one completed frequency at
a time in ascending block/lane order, making resident field ownership roughly
`B` staging fields plus one delivery field; report that honestly rather than as
exactly `B*M_field`. If the consumer contract requires retaining all fields,
do not claim RSS reduction.

Stop P06 immediately on parity failure. Revert it if transpose/copy cost erases
the locality benefit or its complexity is not justified. P06 is ADVANCED and
requires independent review if activated.

## 10. Risk register and gates

| Risk | Class | Required mitigation |
|---|---|---|
| changed per-frequency addition order under block scheduling | ADVANCED / HIGH | order audit; raw/scaled memcmp; SHD hash; reviewer PASS |
| point-major indexing or divergent-prefix out-of-bounds | ADVANCED / HIGH | explicit stride/actualB invariants; divergent-prefix fixture; sanitizers/targeted tests |
| stale scratch after resize/reuse | ADVANCED / HIGH | overwrite-before-read invariant; varying ray lengths/block tail tests |
| cache mutation or hidden shared mutable state | ADVANCED / HIGH | caller-owned scratch only; fingerprint before/after |
| pointer invalidation for cached pressure bases | STANDARD / MEDIUM | cache after final allocation; block-scoped lifetime |
| bit-mask lane/shift errors and `B=64` edge | STANDARD / MEDIUM | representation A/B tests; 1/2/8/16/64 lane unit coverage as applicable |
| allocation instrumentation distorts timing | STANDARD / MEDIUM | tracing separated from clean timed runs |
| `--profile-influence` counter overhead contaminates verdict | ADVANCED / HIGH | separate matching diagnostic invocation; instrumented wall/Influence never used for retention or viability |
| CLI silently changes old fused behavior | STANDARD / MEDIUM | absent option means Nf; fused-only validation; parser/CLI tests |
| Project refactor expands scope | ADVANCED / MEDIUM | 5%/allocation activation threshold; otherwise defer |
| RSS claim conflates locality and ownership | ADVANCED / MEDIUM | report all three memory measures and ownership formula |
| benchmark noise or thermal/order bias | ADVANCED / MEDIUM | warmup, five repeats, rotated order, median/min/max/MAD |

P04 and P05 cannot close on worker self-review. Each requires an independent
reviewer `PASS`; findings return to advanced-worker remediation and the same
review role re-validates. P06 has the same gate if activated.

## 11. Viability and stopping rule

Do not impose a preset minimum percentage. Compare candidate deltas with MAD,
range, repeat consistency, phase attribution, RSS, and added complexity.

- **VIABLE:** at least one retained layout/B is Levels A-D clean, stably faster
  than reuse by more than measurement noise end-to-end, with reasonable RSS
  and complexity.
- **PARTIALLY_VIABLE:** benefit is limited to higher Nf, or wall is neutral but
  working-set/RSS improvement is material, or a measured layout gain is too
  small to justify production complexity.
- **NOT_VIABLE:** after low-risk cleanup, required point-major layout work, and
  the complete mandatory B sweep, no candidate stably beats reuse.

On `NOT_VIABLE`, terminate Cartesian Cerveny serial-fusion wall optimization.
Do not continue open-ended block-size/layout tuning. Parallel remains reference
only and does not become an IGR-1p escape implementation.

## 12. Construction entry recommendation

**Recommendation: enter CONSTRUCT; independent design review is `PASS`.**

The design is implementable without changing acoustic formulas or the frozen
cache, and it isolates the causal questions in a bounded sequence. The first
construction action is P01 baseline reproduction, followed by the P03
one-variable experiments. Construction has not started in this design turn;
the transition must remain serial under the execution worklist.
