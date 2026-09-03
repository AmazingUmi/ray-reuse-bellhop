# IGR-1p — L1 Data-Layout Lightweight Probe

> **Date:** 2026-09-02
> **Status:** L1/L1c RETAINED; L1b/blocking/dynamic tiles NOT RETAINED; static range partition PROMISING
> **Scope:** independent quick experiments leading to the IGR-2 retained baseline
> **Base HEAD:** `749741d9eba8559ecdd286fb7080ceabd7e0b605`

## 1. Purpose and limitation

The user explicitly reprioritized IGR-1p from completing P01 to an immediate
data-structure experiment followed by extremely lightweight testing. This
probe therefore tests the direction of the frozen P04 L1 candidate; it is not
the frozen one-warmup/five-repeat retention benchmark and cannot establish
statistical significance, viability, or P04 closure.

P01 artifacts collected before reprioritization remain incomplete as a gate:
the P01 report/review was not completed. No P03 mask/scratch candidate was
started. P05 blocking and static/dynamic receiver-range scheduling were later
screened under the same quick-experiment policy; their evidence is recorded
below.

## 2. Isolated implementation

Only `src/field/cartesian_cerveny_influence.cpp` changed.

The general per-frequency `PrecomputedRayValues` and `accumulateImpl` remain
unchanged. The fused kernel now uses a fused-only flattened layout:

```text
old:
  ray[f].p[point]
  ray[f].q[point]
  ray[f].gamma[point]
  ray[f].kmah[point]

L1 probe:
  p[point * Nf + f]
  q[point * Nf + f]
  gamma[point * Nf + f]
  kmah[point * Nf + f]
```

`p` remains allocated and stored so this probe does not mix in the separate
M2 candidate. Masks, projected state, workspaces, projector, frequency
blocking, physics, transcendental functions, and accumulation order were not
changed.

Each frequency retains its own active-prefix scan. Storage is sized to
`Pmax*Nf`, then populated in ascending frequency and ascending point order.
For each frequency, SSP segment-index reset, SSP evaluation, p/q/gamma
expressions, KMAH recurrence, and the hot-loop arithmetic are unchanged.
The existing `rightIndex < activePrefix[f]` gate prevents reads from inactive
rectangular tails.

## 3. Allocation and payload trade-off

The source-visible fused precompute payload allocation shape changes from
approximately `4*Nf` field allocations (plus the outer object vector) to four
flat field allocations.

Payload changes from approximately:

```text
52 * sum(Pf) bytes
```

to:

```text
52 * Pmax * Nf bytes
```

Thus the L1 layout removes fragmentation and makes fixed-point frequency lanes
contiguous, but it can allocate and zero-initialize extra inactive tail values
when active prefixes diverge. Precompute writes are also stride-`Nf`. These
costs are part of the candidate and require measurement.

## 4. Lightweight correctness evidence

Build:

- Directory: `Bellhop_RayReuse/build/igr1p-l1-probe`
- Type: Release
- Compiler: AppleClang 21.0.0
- Binary SHA-256:
  `0cf690510892f088d17bc83dc122768d957bc7f4d34e4df9f443c6ad885144f9`

Targeted command:

```bash
.venv/bin/ctest --test-dir Bellhop_RayReuse/build/igr1p-l1-probe \
  -R 'rayreuse.component.(cartesian_cerveny_influence|fused_cc_parity)' \
  --output-on-failure
```

Result: **2/2 PASS**.

The existing fused parity test includes divergent-prefix/cutoff-truncated
rays and covers cache fingerprint integrity, raw workspace `memcmp`, and
scaled workspace `memcmp` (Levels A-C). An independent reviewer also reran the
parity binary and returned `PASS` for entry to a short performance probe.

The 2F performance probe enforced reuse/fused SHD identity, providing a narrow
Level-D check with common SHA-256:
`cf1f9711aefcab087bd766c395a03b935c1c9cf13980335a368035515fd126bc`.

`git diff --check` passes.

## 5. Short clean performance probe

Artifact:
`build/igr1p-l1-probe/benchmarks/igr1p_l1_probe_2f.json`.

Protocol: `munk_cerveny_cc`, broadband smoke frequencies `[50, 250]`, modified
Release binary, modes reuse/fused, one warmup and one measured sample, rotated
order, no `--profile-influence`.

| Mode | External wall (s) | Trace (s) | Project (s) | Influence (s) | Scale (s) | Peak RSS (MiB) |
|---|---:|---:|---:|---:|---:|---:|
| reuse | 8.008 | 0.292 | 0.036 | 7.662 | 0.008 | 305.891 |
| fused L1 | 11.307 | 0.296 | 0.034 | 10.954 | 0.008 | 307.656 |

Explicit ratio convention:

- `fused/reuse = 1.412`.
- `reuse/fused speedup = 0.708` (fused remains slower).
- RSS delta fused minus reuse = `+1.766 MiB`.

For context only, the newly collected unchanged-layout P01 2F clean archive
(five samples) has medians reuse `7.962 s`, fused `11.395 s`, and fused
Influence `11.059 s`. Relative to those medians, the single L1 sample is about
`0.78%` lower in fused wall and `0.96%` lower in fused Influence. However, its
wall and Influence both fall inside the unchanged-layout observed ranges
(`11.304..11.726 s` and `10.950..11.370 s`). The apparent improvement is
therefore indistinguishable from the already observed run dispersion.

## 6. Result

**No demonstrated performance improvement yet.**

The short probe shows that L1 remains bitwise-compatible on the targeted
evidence and may have a small favorable direction, but it does not show a
material improvement beyond noise. The candidate is neither retained nor
rejected on this evidence. A retention decision would still require the
frozen 2F/8F/16F clean one-warmup/five-repeat protocol, allocation/working-set
evidence, and formal P04 review.

No other layout, mask, blocking, workspace, or projector experiment is
authorized or started by this report.

# L1c — Contiguous 3D Fused Pressure Workspace

## 7. Before and after layout

L1 was first saved independently as commit
`1ffc1d8e0c47d8180109e529db7fd2a63fa38784`. L1c is an uncommitted experiment
on top of that commit and changes only the fused pressure destination and the
minimum API/test plumbing required by that ownership change.

Before L1c, the fused solver retained `Nf` independent ordinary workspaces:

```text
vector<FrequencyWorkspace>
  pressure[f][depth][range]   // Nf large heap vectors
```

After L1c, fused accumulation owns one contiguous allocation:

```text
FusedPressureWorkspace
  pressure[range][depth][frequency]  // one large heap vector
```

The flat index is exactly:

```cpp
((rangeIndex * depthCount + depthIndex) * frequencyCount + frequencyIndex)
```

Frequency is therefore the innermost contiguous dimension. The hot loop gets
one cell span for `(rangeIndex, depthIndex)` and updates `cell[f]` in ascending
frequency order. No active-lane compression, sparse representation, bit-mask
change, blocking, SIMD, or other optimization is included.

## 8. Allocation and materialization strategy

During accumulation, field ownership changes from `Nf` independent pressure
vectors to one 3D pressure vector with the same nominal complex-value count:

```text
rangeCount * depthCount * frequencyCount
```

After accumulation, the solver processes frequencies in ascending order:

```text
3D fused pressure
  -> materialize one ordinary FrequencyWorkspace
  -> assign values into [depth][range]
  -> scale
  -> consumer / SHD
  -> destroy/move that one ordinary workspace
```

Materialization performs one assignment per value. It does not sum, reduce,
reassociate, scale, or otherwise change the stored complex value. At most one
extra ordinary frequency field is materialized at a time.

## 9. Accumulation-order preservation

The ray, segment, crossed-range, depth, and True -> Surface -> Bottom image
loops are unchanged. For every fixed `(frequency, range, depth)`, each ray
still performs exactly one read-add-assign in the original ray order:

```text
ray0, ray1, ray2, ...
```

Only the destination address changes from a separate frequency-owned vector
to `cell[frequencyIndex]`. Frequency-local active-prefix, gamma, window, taper,
amplitude, and eligibility checks remain independent and unchanged.

## 10. Targeted correctness and SHD identity

Build:

- Directory: `Bellhop_RayReuse/build/igr1p-l1c-probe`
- Type: Release
- Compiler: AppleClang 21.0.0
- Binary SHA-256:
  `2a4f639279cfa83be280ca79b1d58b61f8f35417b4383662de752ef2369cad07`

Targeted CTest:

```text
rayreuse.component.cartesian_cerveny_influence  PASS
rayreuse.component.fused_cc_parity              PASS
2/2 PASS
```

The parity test covers a non-vacuous divergent-prefix fixture (96 divergent
and cutoff-truncated rays), raw workspace `memcmp`, scaled workspace `memcmp`,
and cache fingerprint integrity. A focused independent review returned
`PASS_FOR_SCREENING` for layout, ordering, materialization, bounds, and scope.

An additional 2F CLI gate ran reuse and fused with `--verify-cache`:

- cache fingerprint before == after == `2271226459307825052` in both modes;
- reuse/fused SHD are byte-identical;
- common SHD SHA-256:
  `cf1f9711aefcab087bd766c395a03b935c1c9cf13980335a368035515fd126bc`.

Levels A-D therefore pass for the required lightweight gate.

## 11. 16F lightweight performance screening

Artifact:
`build/igr1p-l1c-probe/benchmarks/igr1p_l1c_probe_16f.json`.

Protocol: `munk_cerveny_cc`, broadband regression frequencies 50..500 Hz
(16F), same modified Release binary, modes reuse/fused, one warmup and one
measured sample, rotated order, no `--profile-influence`.

| Mode | External wall (s) | Trace (s) | Project (s) | Influence (s) | Scale + materialize (s) | Peak RSS (MiB) |
|---|---:|---:|---:|---:|---:|---:|
| reuse | 97.512 | 0.601 | 0.542 | 96.237 | 0.063 | 607.438 |
| fused L1c | 86.605 | 0.586 | 0.489 | 85.369 | 0.106 | 634.203 |

Cross-mode SHD identity passed with common SHA-256
`f01ee48119549a82e79798322bf5227d8fc95054be82de955de5ccadef057c2c`.

Explicit ratios:

- `candidate/reuse = 0.8881` (candidate 11.19% faster in this screening run).
- `candidate/original IGR-1 fused = 86.605 / 103.04 = 0.8405`.
- Change relative to original IGR-1 fused = `-15.95%` wall.
- Candidate/historical reuse = `86.605 / 95.66 = 0.9053`.
- Influence relative to original IGR-1 fused Influence
  (`101.88 s`) = `-16.21%`.

The fused-minus-reuse RSS delta is `+26.766 MiB`. Absolute fused RSS
(`634.203 MiB`) is essentially unchanged from the original IGR-1 fused
measurement (about `634.3 MiB`): one allocation improves locality but does not
reduce the total number of resident complex field values. RSS, working set,
and allocation count remain distinct measures.

Materialization increases the measured fused Scale phase to `0.106 s`, but
that added copy cost is small compared with the Influence reduction.

## 12. L1c verdict

**PROMISING**

The single 16F screening sample improves wall time by about 15.95% relative to
the original IGR-1 fused result, well beyond the approximately 3% screening
threshold, and is faster than the same-run reuse reference. This is still a
one-warmup/one-sample screening result, not a statistically accepted
benchmark. Per the quick-experiment protocol, the L1c source/test changes are
kept uncommitted for the next decision; no blocking, L1b, full regression,
five-repeat matrix, or final review is started.

## 13. L1c short confirmation and retention

The same Release binary and unchanged 16F `munk_cerveny_cc` configuration were
then run for three additional measured samples per mode. Influence profiling
remained disabled. Mode order rotated reuse/fused, fused/reuse, reuse/fused.

Artifact:
`build/igr1p-l1c-probe/benchmarks/igr1p_l1c_confirm_16f_3x.json`.

| Mode | Wall samples (s) | Wall median (s) | Influence samples (s) | Influence median (s) | RSS samples (MiB) | RSS median (MiB) |
|---|---|---:|---|---:|---|---:|
| reuse | 97.546, 97.583, 97.554 | 97.554 | 96.271, 96.287, 96.276 | 96.276 | 607.422, 607.422, 607.484 | 607.422 |
| fused L1c | 86.211, 85.952, 86.022 | 86.022 | 84.975, 84.758, 84.826 | 84.826 | 634.594, 634.609, 634.484 | 634.594 |

Confirmation ratios and memory:

- `candidate/reuse = 86.022 / 97.554 = 0.8818`.
- Confirmed same-run wall improvement = `11.82%`.
- `candidate/original IGR-1 fused = 86.022 / 103.04 = 0.8348`.
- Confirmed wall improvement relative to original fused = `16.52%`.
- Median fused-minus-reuse RSS = `+27.172 MiB`.
- Cross-mode SHD remained byte-identical, SHA-256
  `f01ee48119549a82e79798322bf5227d8fc95054be82de955de5ccadef057c2c`.

The three samples are tightly grouped and the 11.82% median improvement is
well beyond the approximately 3% short-confirmation threshold.

### 13.1 8F quick trend probe

Artifact: `build/igr1p-l1c-probe/benchmarks/igr1p_l1c_quick_8f.json`.

Protocol: the same Release binary, the same 50..500 Hz eight-frequency list
used by the earlier IGR-1 measurements, one warmup and one measured sample,
reuse/fused, no influence profiling.

| Mode | Wall (s) | Influence (s) | RSS (MiB) |
|---|---:|---:|---:|
| reuse | 49.822 | 48.921 | 607.422 |
| fused L1c | 47.465 | 46.557 | 621.047 |

- `candidate/reuse = 0.9527`, or a 4.73% same-run improvement.
- Fused-minus-reuse RSS = `+13.625 MiB`.
- Cross-mode SHD remained byte-identical, SHA-256
  `246aeb65274eb08b73c1ad9d4be2b2aa849f47e28f7eba3891a75fdfadaa753c`.

The observed improvement grows from 4.73% at 8F to 11.82% at 16F, consistent
with the pressure-destination locality hypothesis. This remains a short
confirmation rather than the previously frozen full benchmark matrix.

### 13.2 Retention verdict

**RETAINED**

L1c retains Levels A-D on the targeted evidence and its 16F median wall gain
is stable and far beyond noise. The contiguous 3D fused pressure workspace is
therefore retained. No blocking, L1b, mask/scratch/projector optimization,
full regression, or final review had been started at that retention checkpoint.

# L1b — Projected Frequency State Point-Major Layout

## 14. Isolated implementation

L1b was evaluated as an uncommitted single-file experiment on retained L1 and
L1c commit `bd4816af105be1ac09aff44e5337187e7bec0c40`.

The general `FrequencyProjector` and `RayFrequencyState` ownership/API were not
changed. After the existing projector completed, the fused Influence path
copied the four projected fields by exact assignment into point-major SoA:

```text
complexTravelTime[point * Nf + frequency]
amplitude[point * Nf + frequency]
reflectionPhase[point * Nf + frequency]
active[point * Nf + frequency]
```

The transpose was inside `accumulateFusedImpl`, so its allocation/copy cost
was included in Influence rather than Project. Validation continued to read
the original projected states. Active-prefix scanning, left-active gating,
tau interpolation, right-point amplitude, and reflection phase then read the
SoA. Existing masks, L1 precompute, L1c pressure storage, projector formulas,
and all traversal/accumulation order remained unchanged.

## 15. Minimal correctness

- Targeted fused parity: PASS.
- Divergent-prefix fixture: PASS (96 divergent/cutoff-truncated rays).
- Raw/scaled workspace memcmp and cache fingerprint checks: PASS.
- 2F reuse/fused SHD byte-identical, SHA-256
  `cf1f9711aefcab087bd766c395a03b935c1c9cf13980335a368035515fd126bc`.
- Cache fingerprint before == after == `2271226459307825052`.
- Focused reviewer verdict: `PASS_FOR_SCREENING`.
- `git diff --check`: PASS.

## 16. 16F lightweight screening

Artifact: `build/igr1p-l1b-probe/benchmarks/igr1p_l1b_probe_16f.json`.

Protocol: `munk_cerveny_cc` 16F, one warmup and one measured sample, reuse and
fused, no influence profiling.

| Mode | Wall (s) | Project (s) | Influence (s) | RSS (MiB) |
|---|---:|---:|---:|---:|
| reuse | 97.446 | 0.548 | 96.168 | 607.406 |
| fused L1+L1c+L1b | 87.766 | 0.481 | 86.565 | 635.453 |

The relevant comparison is against retained L1c, not original IGR-1:

- Retained L1c 16F fused wall median: `86.022 s`.
- L1b/L1c wall ratio: `87.766 / 86.022 = 1.0203`.
- L1b wall is approximately `2.03%` slower than retained L1c.
- Retained L1c Influence median: `84.826 s`.
- L1b Influence is approximately `2.05%` slower.
- L1b fused RSS is `635.453 MiB`, about `0.859 MiB` above the retained L1c
  median (`634.594 MiB`).

Cross-mode SHD remained byte-identical with SHA-256
`f01ee48119549a82e79798322bf5227d8fc95054be82de955de5ccadef057c2c`.

## 17. L1b verdict

**NO_CLEAR_GAIN**

The single screening sample is within the approximately +/-2% decision band
and moves in the unfavorable direction. Per the quick-experiment rule, no two
additional fused samples and no 8F probe were run. The L1b production change
was rolled back and was not committed. Retained L1/L1c remain unchanged.

# Frequency Blocking — Rejected

The retained L1/L1c implementation added a temporary explicit batch size and
screened 16F with `Bf=4,8,16`, where `Bf=16` was the unchanged retained
baseline. Raw/scaled workspace `memcmp`, divergent-prefix behavior, cache
fingerprint, and SHD identity passed for every candidate. Neither smaller
block produced a stable improvement beyond the screening noise band; the
frequency-blocking production diff was rolled back and is not present in the
IGR-2 baseline. This deliberately leaves `Bf=Nf` in the production fused
kernel.

# Static Contiguous Receiver-Range Partition — Promising

The next isolated experiment partitioned receiver ranges into deterministic
contiguous quotient/remainder blocks. Every worker traversed frozen rays in
the original order and wrote only its exclusive `[range][depth][frequency]`
cells. It used no pressure atomic and no per-worker pressure copy. Targeted
raw/scaled parity, divergent-prefix, fingerprint, and SHD byte identity gates
passed.

One-warmup/one-measured 16F screening results were:

| range workers | Wall (s) | Critical-path Influence (s) | Peak RSS (MiB) | Speedup vs 1 |
|---:|---:|---:|---:|---:|
| 1 | 89.058 | 87.728 | 634.09 | 1.00x |
| 2 | 45.294 | 44.054 | 637.30 | 1.97x |
| 4 | 28.041 | 26.779 | 641.64 | 3.18x |
| 8 | 23.462 | 21.854 | 654.28 | 3.80x |

The static implementation was marked **PROMISING** and retained uncommitted
as the construction baseline for IGR-2. These samples are screening evidence,
not final IGR-2 acceptance statistics.

# Dynamic Range Tiles — No Clear Gain

A temporary dynamic atomic tile allocator screened 8, 16, and 32 tiles at
eight workers. An initial 8-tile sample appeared 5.61% faster than the static
sample, but a requested interleaved three-round reproduction showed:

| schedule | Wall samples (s) | Wall median (s) | Influence median (s) | RSS median (MiB) |
|---|---|---:|---:|---:|
| static | 23.180, 23.154, 22.994 | 23.154 | 21.588 | 652.391 |
| dynamic 8 tiles | 22.995, 23.425, 23.103 | 23.103 | 21.537 | 652.469 |

`dynamic/static = 0.9978`, only a 0.22% wall difference. The earlier 5.61%
was therefore not repeatable. Dynamic scheduling was marked
**NO_CLEAR_GAIN**, completely rolled back, and excluded from IGR-2.

# IGR-1p retained handoff

- L1 commit: `1ffc1d8e0c47d8180109e529db7fd2a63fa38784`.
- L1c commit: `bd4816af105be1ac09aff44e5337187e7bec0c40`.
- Retained production data layouts: point-major frequency-contiguous fused
  ray precompute and `[range][depth][frequency]` pressure.
- Retained uncommitted experiment: static contiguous receiver-range
  partition only.
- Rejected production experiments absent: L1b, frequency blocking, dynamic
  range tiles.
