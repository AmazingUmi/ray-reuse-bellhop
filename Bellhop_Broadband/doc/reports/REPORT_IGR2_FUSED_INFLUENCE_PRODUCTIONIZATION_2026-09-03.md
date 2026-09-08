# IGR-2 — Fused Influence Productionization & Optional Range Parallelism

> Date: 2026-09-03
> Construction baseline: `bd4816af105be1ac09aff44e5337187e7bec0c40`
> Branch: `feat/igr-influence-geometry-reuse`
> Current status: ACCEPTED / CLOSED

## Outcome

IGR-2 promotes cross-frequency fused Cartesian Cerveny Influence to the
production RayReuse broadband TL algorithm for its existing supported domain:
multi-frequency coherent Cartesian Cerveny TL, one source, an equally spaced
rectilinear receiver grid, and at least two receiver ranges. The production hot
pressure layout is one contiguous `[range][depth][frequency]` allocation.

Static contiguous receiver-range parallelism is an optional fused execution
optimization. It preserves each cell's ray accumulation stream and remains
byte-identical to serial fused and the legacy per-frequency oracle.

## Workspace cleanup

Before IGR-2 construction, the uncommitted IGR-1p experiments were audited and
reduced to the retained static quotient/remainder range partition. Dynamic
range tiles (`NO_CLEAR_GAIN`), frequency blocking (`REJECTED`), and L1b
projected-state transpose (`NO_CLEAR_GAIN`) have no production residue. The L1
and L1c baselines remain commits `1ffc1d8e` and `bd4816af` respectively.

## Production architecture

The solver keeps one frozen, read-only `RayPathCache` and one shared fused
pressure workspace. For requested range workers `N` and receiver-range count
`R`, `effectiveRangeWorkers = min(N, R)`. Quotient/remainder partitioning forms
non-empty contiguous `[begin,end)` blocks whose sizes differ by at most one.

Each worker owns its projector, Influence object, projected frequency states,
epsilon scratch, timing record, counters, and exception slot. It traverses all
frozen rays in original order but writes only its range block. There are no
pressure atomics, reductions, dynamic tiles, or per-worker pressure fields.
All workers join before exceptions are rethrown and before materialization,
scaling, or consumer delivery.

For every fixed `(range, depth, frequency)` exactly one worker performs the
same ray/segment/range/depth/image/frequency addition stream as serial fused.
The image order remains True, Surface, Bottom and no expression is
reassociated.

Top-level Project and Influence measurements are the maximum per-worker elapsed
time, representing the critical path. Detailed profiled counters and sub-phase
seconds are summed worker work and must not be interpreted as wall time.

## CLI and compatibility

| Invocation | Semantics |
|---|---|
| no execution mode | unchanged global `nonreuse` reference default |
| `--execution-mode fused` | production RayReuse fused serial, workers 1/1 |
| `--execution-mode fused --range-parallel` | static range parallel, requested workers 4 |
| `--execution-mode fused --range-parallel --workers N` | static range parallel, requested workers `N` |
| fused plus `--workers N` without `--range-parallel` | rejected |
| `--range-parallel` outside explicit fused | rejected |
| `--execution-mode parallel --workers N` | unchanged legacy frequency-parallel behavior |

The PRT records `range parallel`, requested range workers, and effective range
workers. Legacy `reuse` and frequency-`parallel` tokens and implementations are
preserved without redirection or API removal. An explicit legacy invocation
emits one stderr deprecation warning only when the parsed run is fully inside
the fused-supported domain; unsupported-domain legacy paths do not receive a
misleading warning. `nonreuse` remains the traditional/reference baseline.

The global implicit default was deliberately not changed to fused. Fused does
not cover single-frequency TL, Arrival/Eigenray/Ray products, other beam
families, non-coherent TL, multisource, or irregular receivers; changing the
enum default would incorrectly reroute those products.

## Correctness and regression evidence

Release build used AppleClang 21.0.0 with warnings-as-errors. The executable
SHA-256 recorded by the acceptance artifact was
`44d0bb7e6e01b9c9b14fd382c56056ad5a6a46b87e999b24aeedfe00100b64f7`.
The long-running evidence below was collected before the acceptance
remediation. The remediation changed only the shared support predicate and
documentation; focused re-validation and the original reviewer re-review pass.

- Focused CTest (command line, Cartesian CC Influence, fused solver, fused
  parity): 4/4 PASS.
- Full CTest: 43/43 PASS.
- Python pytest after the benchmark-harness extension: 192/192 PASS with 399
  subtests.
- Standard-case unittest after the extension: 177/177 PASS.
- Raw and scaled workspace `memcmp`: PASS for workers 1/2/4/8.
- Divergent-prefix fixture: PASS at eight workers.
- Frozen cache fingerprint: before == after.
- Minimum supported two-range fixture with requested workers 8 reports
  effective workers 2; the nine-range/eight-worker fixture covers a
  non-divisible partition.
- 2F CLI reuse, fused serial, default-4, and workers-8 SHD SHA-256:
  `cf1f9711aefcab087bd766c395a03b935c1c9cf13980335a368035515fd126bc`.
- 16F benchmark configurations share SHD SHA-256:
  `f01ee48119549a82e79798322bf5227d8fc95054be82de955de5ccadef057c2c`.

The existing Cartesian Cerveny domain requires at least two receiver ranges;
IGR-2 does not relax that scientific precondition. Clamp coverage therefore
uses the minimum supported two-range grid rather than an invalid one-range
fixture.

## 16F range-worker performance

Environment: Apple Silicon M4, 10 logical cores, 24 GiB, macOS 26.6.2 arm64.
Case: `munk_cerveny_cc`, broadband regression frequencies 50..500 Hz (16F),
Release, one warmup per configuration and three measured repetitions in
rotated order. `--profile-influence` was disabled. Artifact:
`build/igr2-clean/benchmarks/igr2_range_workers_16f.json`, SHA-256
`d3711ab50748af9ad7584a20d0f66528e37796346381b8c9a4d1b0105b7c0449`.
The artifact records source commit
`bd4816af105be1ac09aff44e5337187e7bec0c40`, `git.dirty = true`, commit tree
`2b3c034923daefd6ce30424dcdd083d906019a53`, and executable SHA-256
`44d0bb7e6e01b9c9b14fd382c56056ad5a6a46b87e999b24aeedfe00100b64f7`.
It did not capture an exact hash of the dirty production/test diff, so these
measurements must not be presented as evidence for an exactly reconstructible
source diff or a later commit identity.

The worker-4 row uses an explicit worker override but resolves to the same
production execution settings as `--range-parallel` without `--workers`;
separate CLI/PRT smoke verifies that the latter requests/effectively uses 4/4.

| Range workers | Wall samples (s) | Wall median / min / max / MAD (s) | Influence median / MAD (s) | Project median (s) | RSS median / min / max (MiB) | Speedup vs 1 | Efficiency |
|---:|---|---|---|---:|---|---:|---:|
| 1 | 86.283, 85.857, 86.133 | 86.133 / 85.857 / 86.283 / 0.150 | 84.879 / 0.168 | 0.499 | 634.625 / 634.625 / 634.688 | 1.00x | 100.0% |
| 2 | 45.641, 45.585, 45.402 | 45.585 / 45.402 / 45.641 / 0.056 | 44.312 / 0.111 | 0.494 | 637.063 / 636.922 / 637.453 | 1.89x | 94.5% |
| 4 (CLI default when enabled) | 27.742, 27.930, 27.376 | 27.742 / 27.376 / 27.930 / 0.188 | 26.453 / 0.186 | 0.559 | 641.766 / 641.313 / 642.141 | 3.10x | 77.6% |
| 8 | 22.953, 23.499, 22.737 | 22.953 / 22.737 / 23.499 / 0.216 | 21.408 / 0.207 | 0.829 | 652.734 / 651.516 / 654.047 | 3.75x | 46.9% |

The results reproduce the earlier static experiment: range partitioning gives
large, stable wall gains while remaining bitwise identical. Scaling becomes
sublinear above four workers, consistent with load imbalance, duplicated
per-worker projection/precompute work, and shared memory bandwidth. Dynamic
tiles were already shown not to produce a repeatable gain and are not retained.

Peak RSS rises by about 18.1 MiB from workers 1 to 8. This is not eight copies
of the approximately 24.6 MiB 16-frequency field payload. L1c retains one full
`[R][D][F]` field; the increase comes from worker-local scratch and thread
resources. Peak RSS, total allocation, and hot working set remain distinct.

## Current batch assessment

The collected evidence supports the fused path and optional static range
parallelism, and Batch Acceptance passed after remediation. Remediation made
the public fused-support predicate the single source of truth used by both the
solver validation and legacy replacement warnings, including equal range
spacing. Targeted re-validation and the original reviewer re-review pass.
Legacy per-frequency paths remain compatibility fallbacks and oracles. IGR-2
does not extend scientific scope and does not start IGR-3.

Independent final review returned `ACCEPTED` on 2026-09-03 with no remaining
critical or actionable finding.
