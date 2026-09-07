# IGR-2 — Fused Influence Productionization & Optional Range Parallelism — FROZEN DESIGN

> **Status:** ARCHITECT-FROZEN / DESIGN REVIEW PASS
> **Date:** 2026-09-03
> **Baseline:** `bd4816af105be1ac09aff44e5337187e7bec0c40` plus the retained static contiguous range-partition experiment
> **Execution authority:**
> [`IGR-2_FUSED_INFLUENCE_PRODUCTIONIZATION_WORKLIST.md`](IGR-2_FUSED_INFLUENCE_PRODUCTIONIZATION_WORKLIST.md)

## 1. Product meaning and compatibility boundary

IGR-2 makes the retained fused solver the production RayReuse broadband TL
algorithm for its current supported domain:

```text
one frozen geometry trace
  + cross-frequency fused Cartesian Cerveny Influence
  + point-major frequency-contiguous ray precompute (L1)
  + pressure[range][depth][frequency] (L1c)
```

Its scientific support boundary remains multi-frequency coherent Cartesian
Cerveny TL, one source, and a rectilinear receiver grid with at least two
receiver ranges. The two-range minimum is an existing
`CartesianCervenyInfluence` scientific precondition; IGR-2 does not relax it.
The fused solver is not a universal replacement for every Bellhop product.

Therefore the global CLI default remains `BroadbandExecutionMode::NonReuse`.
Changing an unspecified mode to fused would silently alter single-frequency,
Arrival/Eigenray, multisource, non-coherent, irregular-receiver, and other-beam
runs. Production status is expressed by the documented and tested
`--execution-mode fused` path, not by unsafe automatic routing.

`nonreuse` remains the traditional/reference baseline. Legacy
`--execution-mode reuse` (serial per-frequency cache reuse) and
`--execution-mode parallel` (frequency-parallel cache reuse) keep their enum
values, parser tokens, solver implementations, numerical behavior, PRT metric
shape, and benchmark-harness compatibility during IGR-2. Where the parsed run
is inside the fused support domain, they are deprecated in favor of fused;
outside that domain they remain supported compatibility paths. They are not
removed or silently redirected. Usage describes this scoped deprecation and
an eligible explicit invocation emits one concise warning on stderr. No C++
solver symbol is deleted or annotated `[[deprecated]]` in this batch because
these paths are still required as compatibility fallbacks and numerical
oracles outside the fused domain.

## 2. CLI semantics

Add a boolean `--range-parallel` option. It selects static contiguous receiver
range partitioning only; it does not select an execution mode. Parsing remains
order-independent and validates final combinations.

| Invocation shape | Result |
|---|---|
| no `--execution-mode` | unchanged `nonreuse` default |
| `--execution-mode fused` | production fused serial, requested/effective range workers = 1 |
| `--execution-mode fused --range-parallel` | range-parallel fused, requested workers = 4 |
| `--execution-mode fused --range-parallel --workers N` | range-parallel fused, requested workers = `N` |
| `--execution-mode fused --workers N` | reject: `--workers` does not enable range parallelism |
| `--range-parallel` without explicit fused mode | reject; no implicit mode switch |
| `--range-parallel` with `nonreuse`, `reuse`, or `parallel` | reject |
| `--execution-mode parallel --workers N` | accepted legacy frequency-parallel behavior |
| `--workers N` with `nonreuse` or `reuse` | reject, unchanged intent |
| `--output-queue-capacity`, `--memory-budget-mib`, or `--profile-frequency-tasks` outside legacy `parallel` | reject, unchanged |
| duplicate `--range-parallel` | reject |
| `N == 0` or malformed | reject through the existing positive-size parser |

After parsing, existing product validation still rejects fused for
single-frequency TL, Ray/Arrival/Eigenray products, non-coherent TL,
non-Cartesian/non-Cerveny beams, multisource input, or irregular receivers.
`--range-parallel` does not weaken or bypass those checks.

The app resolves the fused requested count as:

```cpp
requestedRangeWorkers =
    !rangeParallel ? 1U : (workerCountSpecified ? workerCount : 4U);
```

The solver resolves:

```cpp
effectiveRangeWorkers =
    std::min(requestedRangeWorkers, receiverRangeCount);
```

The PRT records whether range parallelism was requested plus requested and
effective range-worker counts. Legacy `parallel` keeps its current hardware
concurrency default and frequency-worker meaning.

## 3. Solver API and ownership

Use a fused-specific execution setting so the public call site does not give a
generic worker count an ambiguous frequency-parallel meaning:

```cpp
struct FusedRayReuseExecutionSettings {
  std::size_t requestedRangeWorkers{1U};
};
```

`FusedRayReuseSolver::accumulateFrequencies` and `solveStreaming` accept this
setting as a trailing defaulted parameter. The existing no-setting call remains
serial and source-compatible. A zero requested count is rejected. The result
statistics gain `requestedRangeWorkers` and `effectiveRangeWorkers`; the
existing trace/cache/fingerprint/phase fields retain their meanings.

Ownership for `W = effectiveRangeWorkers`:

| State | Ownership/lifetime |
|---|---|
| frozen `RayPathCache` | one solver-owned cache, shared as immutable `const&` for the complete solve |
| fused pressure | one solver-owned `[R][D][F]` allocation, concurrently shared; each worker has exclusive range indices |
| frequency projector and influence object | one instance per worker; no shared mutable model state |
| projected frequency states, epsilon values, L1 precompute/hot scratch | per worker/per ray; approximately O(`W*F*P`) transient state, never a pressure-field copy |
| range partition | deterministic immutable `[begin,end)` pair per worker |
| exception slot and timing/statistics | one slot/record per worker; merged only after join |

No worker owns a complete or partial staging pressure field. The only complete
field is the shared L1c destination plus its existing one-frequency
materialization workspace after all accumulation completes.

## 4. Static partition and numerical-order contract

For `R` receiver ranges and `W=min(requested,R)`, form `W` contiguous,
non-empty blocks with quotient/remainder partitioning:

```cpp
q = R / W;
r = R % W;
begin(w) = w*q + min(w, r);
end(w)   = begin(w) + q + (w < r ? 1 : 0);
```

Workers are fixed to one block for the whole solve; there is no work queue,
atomic tile counter, stealing, or dynamic scheduling. Each worker executes:

```text
for ray in frozen RayPathCache order
    prepare that ray's frequency-local states in ascending frequency order
    for segment in original order
        visit only crossed ranges in worker [begin,end), preserving range order
            for depth in original order
                for image True -> Surface -> Bottom
                    for frequency in ascending order
                        evaluate unchanged physics
                for frequency in ascending order
                    pressure[range][depth][frequency] += contribution
```

The influence kernel receives `[rangeBegin, rangeEnd)` and intersects that
with the existing crossed-range interval. It must not reorder ray, segment,
range, depth, image, or per-frequency work inside a cell. For every fixed
`(range, depth, frequency)`, exactly one worker owns the cell and encounters
rays in the same order as serial fused; its complex read-add-assign stream is
therefore bitwise identical. Writes to distinct vector elements are disjoint;
no pressure atomic, mutex, reduction, or reassociation is allowed.

Workers may be launched in any OS order because ownership is spatially
disjoint. All are joined before scale/materialization/consumer calls. On a
worker exception, store `exception_ptr`, join every worker, then rethrow; never
deliver a partial result.

## 5. Timing and counter semantics

Top-level timings in fused range-parallel mode represent elapsed critical-path
estimates, not accumulated worker CPU:

- `Trace`: unchanged single trace, before worker launch;
- `Project`: maximum worker Project elapsed time;
- `Influence`: maximum worker Influence elapsed time, including epsilon and
  that worker's range-limited fused kernel;
- `Scale`: unchanged post-join materialization/scaling total;
- wall: complete solver elapsed time, authoritative for performance.

Each worker uses its own timing variables. The merge happens after join with
`max`, never concurrent writes to shared floating-point statistics.

Detailed `CartesianCervenyStatistics` counters are summed as actual executed
work. This means per-ray projection/precompute and segment-side counters may
grow with `W`, because every static range worker traverses the full ray fan,
while range/depth/image contribution counters partitioned by exclusive ranges
should sum to the serial result. Detailed sub-phase second fields inside that
statistics structure are cumulative worker CPU seconds and must be labelled as
such whenever printed for a range-parallel profiled run; they are not compared
to critical-path `Influence seconds`. No profiled run is used for wall-time
acceptance.

The production PRT adds:

```text
range parallel = enabled|disabled
requested range worker count = N
effective range worker count = W
```

## 6. Deprecation behavior

The parser continues accepting all four execution tokens. Usage describes:

- `nonreuse`: reference and global default;
- `fused`: production RayReuse path for the supported domain;
- `reuse`: legacy per-frequency serial reuse, deprecated when fused supports
  the parsed run;
- `parallel`: legacy frequency-parallel reuse, deprecated when fused supports
  the parsed run.

After successful option/product validation and before computation, an
explicit `reuse` or `parallel` invocation emits exactly one stderr warning
only if the parsed run satisfies every fused scope precondition. The warning
says the mode is retained for compatibility and identifies `fused` (or
`fused --range-parallel`) as its replacement. No deprecation warning is
emitted when fused cannot run that product, nor for the implicit `nonreuse`
default or explicit `nonreuse`/`fused`. Warnings are advisory: exit status,
legacy solver selection, products, and PRT metrics remain unchanged.

## 7. Correctness and routing gates

The existing per-frequency reuse seam remains the direct raw/scaled oracle;
`nonreuse` remains the product-level reference. All comparisons use the same
build and inputs.

1. Serial fused: raw workspace `memcmp`, scaled workspace `memcmp`, frozen
   cache fingerprint before/after, divergent active prefixes, and SHD byte
   identity versus the per-frequency oracle; SHD also equals `nonreuse` on the
   production case.
2. Static range workers 1/2/4/8: the same raw/scaled/fingerprint/divergent
   gates and byte-identical SHD. Worker 1 is also required to equal serial
   fused, not merely be numerically close.
3. Partition edges: a non-divisible range count and a minimum-supported
   two-range fixture with requested workers greater than ranges prove
   complete/no-overlap coverage and `effectiveRangeWorkers == 2`. IGR-2 does
   not weaken the existing two-range minimum. Zero workers prove deterministic
   validation failure; focused code review verifies join-before-rethrow and no
   partial delivery on worker exceptions without adding a production
   fault-injection seam.
4. CLI matrix in section 2, default range-worker value 4, product scope
   rejection, legacy mode preservation, and warning presence/absence.
5. Full existing CTest and pytest suites. Use the repository's current
   standard-case unit target as an additional gate when available.
6. `git diff --check`; no changes in `Bellhop_origin/` or `Bellhop_F2CPP/`; no
   dynamic scheduling, frequency blocking, or rejected L1b production code.

Bit identity remains the hard gate. If parallel output differs, stop and find
the ordering/data-race cause; IGR-2 does not authorize a tolerance downgrade.

## 8. Performance protocol

Use one Release binary on the Apple Silicon M4 and `munk_cerveny_cc` 16F.
Disable `--profile-influence`. Compare static range-worker counts 1/2/4/8.
Perform one warmup per configuration, then three measured repetitions in
round-robin order so thermal/time drift is shared across configurations.

Record per sample:

- external wall;
- solver wall and critical-path Project/Influence;
- Scale;
- peak RSS;
- binary SHA-256, Git HEAD/diff identity, exact command, and SHD SHA-256.

Report median, min, max, MAD, `wall(1)/wall(W)` speedup, and efficiency. The
known serial fused approximately 85–89 s and static eight-worker approximately
23.15 s are sanity references only. No threshold is imposed beyond correctness
and honest scaling attribution; production default remains fused serial and
range parallel remains explicit even if the measured speedup is large.

RSS is not the hot working set. L1c keeps one complete `[R][D][F]` pressure
field regardless of `W`; range parallel adds per-worker projected/precompute
scratch and thread stacks, not `W` full output fields.

## 9. Construction and rollback sequence

1. Audit and clean the experiment diff; freeze this design and obtain an
   independent design-review `PASS`.
2. Normalize the retained static implementation to the API, ownership,
   statistics, and exception rules above; run targeted parity and reviewer.
3. Add CLI/routing/deprecation behavior and focused unit tests; run reviewer.
4. Run full correctness/regression once, then the performance protocol.
5. Reconcile IGR-1p evidence, add the IGR-2 implementation report, and update
   `PLAN_CURRENT_WORK.md` without rewriting historical accepted reports.
6. Run Batch Acceptance and independent final review. Remediate and re-review
   every finding. Commit only the accepted scoped diff; do not push or enter
   IGR-3.

If the static parallel implementation fails bit identity or introduces a
non-local ownership/data-race problem, revert only range-parallel construction
and retain serial fused productionization. Do not substitute dynamic tiles,
atomics, reductions, or a wider algorithm without a new design decision.
