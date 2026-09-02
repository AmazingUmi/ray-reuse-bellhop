# IGR-1p — L1 Data-Layout Lightweight Probe

> **Date:** 2026-09-02
> **Status:** PROBE COMPLETE / NOT RETAINED OR REJECTED
> **Scope:** P04 L1 only; lightweight evidence requested by the user
> **Base HEAD:** `749741d9eba8559ecdd286fb7080ceabd7e0b605`

## 1. Purpose and limitation

The user explicitly reprioritized IGR-1p from completing P01 to an immediate
data-structure experiment followed by extremely lightweight testing. This
probe therefore tests the direction of the frozen P04 L1 candidate; it is not
the frozen one-warmup/five-repeat retention benchmark and cannot establish
statistical significance, viability, or P04 closure.

P01 artifacts collected before reprioritization remain incomplete as a gate:
the P01 report/review was not completed. No P03 mask/scratch candidate and no
P05 blocking work was started.

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
