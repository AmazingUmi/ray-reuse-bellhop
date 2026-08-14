# I8 — Arrivals / Eigenray

> Design status: architecture reviewed and frozen; implementation has not started.
> Baseline: `recovery/f2cpp-i7-complete` at the accepted I7 baseline.
> Task authority: the `I8-xx-Tn` definitions in this directory are the only
> formal instructions for OpenCode.

## I8 Goal

Add Bellhop-compatible two-dimensional single-frequency arrivals (`A` ASCII
and `a` binary) and eigenray (`E`) products without changing the accepted I0–I7
transmission-loss or ordinary ray-trace paths. Arrivals and eigenrays must reuse
the existing immutable geometry trace and per-frequency acoustic projection;
they must not use `FrequencyWorkspace::pressure` as storage.

## Baseline

- I7-06 is accepted and frozen.
- AppleClang Debug ASan/UBSan, AppleClang Release, and GCC 14 Release/Werror
  each pass 32/32 CTest tests.
- Python standard-case tests pass 123/123.
- F2CPP single-frequency cases pass 52/52 (51 SHD and one ordinary RAY case).
- The existing reusable chain is
  `GeometryTracer -> RayPathCache -> FrequencyProjector -> Influence`.
- `RayTraceSolver` and `RayWriter` already provide source-streamed frozen-cache
  consumption and atomic `.ray` publication for ordinary `R` mode.
- `case_model.py` currently recognizes only `shd` and `ray`; I8 must add
  arrival/eigenray product types before end-to-end cases can be authoritative.

## Bellhop_origin correspondence

The authoritative two-dimensional sources are:

| Origin source | Frozen meaning for I8 |
|---|---|
| `Bellhop/bellhop.f90::BellhopCore` | Run-mode allocation, per-source reset, trace/project/influence order, source-major output lifecycle, and the 20,000,000-slot capacity formula |
| `Bellhop/ArrMod.f90::Arrival` | Stored arrival fields and their default-REAL/default-COMPLEX precision |
| `Bellhop/ArrMod.f90::AddArr` | Last-arrival duplicate test, weighted merge, cusp guard, weakest replacement, and ordering |
| `Bellhop/ArrMod.f90::WriteArrivalsASCII` | ASCII header/body order and output-only source-geometry scaling |
| `Bellhop/ArrMod.f90::WriteArrivalsBinary` | GNU Fortran sequential-unformatted record layout and payload kinds |
| `Bellhop/influence.f90::ApplyContribution` | `A/a` accumulation and `E` eigenray dispatch for geometric beam contributions |
| `Bellhop/influence.f90::{InfluenceGeoHatCart, InfluenceGeoHatRayCen, InfluenceGeoGaussianCart}` | Receiver walking, beam-window hit rules, interpolated delay/amplitude/angles, and caustic phase |
| `Bellhop/WriteRay.f90::WriteRay2D` | Ordinary/eigenray RAY block encoding, prefix length, compression, and bounce counts |
| `Bellhop/ReadEnvironmentBell.f90::{ReadRunType,OpenOutputFiles}` | Run-type grammar and ARR/RAY headers |
| `misc/SourceReceiverPositions.f90::Position` | Header scalar kinds: source/receiver depths are REAL(4), receiver ranges are REAL(8) |

Where comments and behavior differ, executable behavior from the repository's
GNU Fortran build is authoritative. A 2026-08-14 design probe using the current
`Bellhop_origin/bin/bellhop` confirmed that an `E` header retains the configured
launch count while the file contains a variable number of blocks and may repeat
the same launch angle. Eigenray readers must therefore consume blocks to EOF.

## Architecture Decisions

### 1. Product data flow

```text
ENV -> SimulationCase + RunMode
    -> per source: GeometryTracer -> frozen RayPathCache
    -> per ray and frequency: FrequencyProjector -> RayFrequencyState
    -> geometric receiver-contribution traversal
       |-> A/a: ArrivalCandidate -> ArrivalWorkspace -> ArrivalWriter
       `-> E:   EigenrayHit(path prefix) -> EigenrayWriter
```

`R` remains `GeometryTracer -> RayPathCache -> RayWriter` and writes every full
launch ray. `E` uses the same traced path but writes one prefix for every
receiver contribution that passes the Origin beam-window rule. It does not use
the arrival accumulator and does not deduplicate hits.

### 2. Arrival records

The public model shall distinguish a binary64 calculation candidate from the
Origin-compatible stored record:

- `ArrivalCandidate`: binary64 amplitude, phase in radians, complex delay in
  seconds, source/receiver declination in degrees, and top/bottom bounce counts.
- `Arrival`: `float amplitude`, `float phaseRadians`,
  `std::complex<float> delaySeconds`, `float sourceDeclinationDegrees`,
  `float receiverDeclinationDegrees`, and signed 32-bit top/bottom bounce
  counts.

The float conversion occurs at the same boundary as `SNGL`/`CMPLX` in
`AddArr`. Two-dimensional records do not carry azimuth fields. Receiver and
source indices are container dimensions, not duplicated inside each record.

### 3. Source, receiver, frequency, and arrival organization

- One `ArrivalWorkspace` represents exactly one source and one frequency.
- Receiver cells use the existing `ReceiverGrid` flat order: receiver depth is
  the outer dimension and range is the inner dimension for rectilinear grids.
- An irregular grid has one accumulation cell per range, while the ARR header
  still writes the complete paired depth and range vectors, matching Origin.
- Multiple sources are never merged. The solver streams workspaces to the
  writer in sorted source-depth order and releases each source workspace after
  it is consumed.
- F2CPP remains single-frequency. Frequency is workspace/file metadata and
  affects projection and duplicate detection. A future RayReuse caller may run
  the same source lifecycle independently for each frequency against the same
  frozen geometry.

### 4. Capacity and accumulation lifecycle

- The logical Origin budget is 20,000,000 arrival slots per source workspace.
- `maxArrivalsPerCell = max(20,000,000 / receiverCellCount, 10)` using checked
  integer arithmetic. Source count and frequency count are not divisors because
  Origin clears and reuses the grid for each source.
- Cell storage is lazy; planning a large grid must not eagerly construct all
  logical arrival records.
- Candidate order is source -> launch angle -> ray traversal -> receiver
  contribution. Cell order is preserved; no time or amplitude sort is allowed.
- Saturation statistics must distinguish replacement of the weakest arrival
  from rejection of a weaker candidate. The configured capacity and saturation
  totals are written to PRT without changing ARR bytes.

### 5. Duplicate, zero, multipath, and caustic semantics

- Only the last stored arrival in a receiver cell is eligible for grouping.
- A candidate groups with that last record iff
  `omega * abs(candidate.delay - last.delay) < 0.05` and
  `abs(candidate.phase - last.phase) < 0.05`; both comparisons are strict.
- A grouped record sums float amplitudes and amplitude-weights delay and source/
  receiver angles using Origin's float intermediates. Its phase and bounce
  counts remain those of the existing record.
- If the float summed amplitude is at or below `epsilon(float)` in magnitude,
  the axial-cusp guard leaves the existing record unchanged.
- A distinct candidate appends while capacity remains. At capacity, the first
  minimum-amplitude slot is replaced only when the candidate amplitude is
  strictly greater; otherwise the candidate is discarded. Count and slot order
  do not change.
- Zero arrivals are valid: each source still has a maximum-count record of zero
  and every receiver cell has a zero count.
- Multipath and caustic arrivals remain separate unless the exact grouping rule
  joins adjacent bracketing contributions. Unwrapped phase and complex delay
  are preserved; no phase normalization or time sorting is permitted.

### 6. ASCII `A` format

The `.arr` header is: quoted `2D`, frequency, source-depth count/vector,
receiver-depth count/vector, receiver-range count/vector. For each source, the
body writes the maximum count over its cells, then cell count and arrival rows
in depth-major/range-minor order. Each row is:

```text
scaled_amplitude phase_degrees delay_real delay_imag
source_declination_degrees receiver_declination_degrees
top_bounces bottom_bounces
```

It may be one physical line as in Origin. ASCII comparison is semantic rather
than whitespace/byte based. Point-source amplitude is scaled at write time by
`1/sqrt(range_m)` (`1e5` at exactly zero range); line-source amplitude is
scaled by `4*sqrt(pi)`. Stored records remain unscaled.

### 7. Binary `a` format

The initial compatibility target is the repository's GNU Fortran ABI: little-
endian sequential-unformatted records with signed 32-bit leading/trailing byte
counts. Payload records are:

1. four bytes containing the characters `'2D'` including quotes;
2. float32 frequency;
3. int32 source-depth count plus float32 depths;
4. int32 receiver-depth count plus float32 depths;
5. int32 receiver-range count plus float64 ranges;
6. per source: int32 maximum cell count;
7. per receiver cell: int32 count, followed by one 32-byte record per arrival
   containing eight float32 values (scaled amplitude, phase degrees, complex
   delay real/imaginary, source angle, receiver angle, top bounces, bottom
   bounces).

The two bounce counts are intentionally encoded as float32 in an arrival
payload because Origin does so for fast Matlab reading. Record markers, payload
sizes, endian order, and closing markers are validated independently.

### 8. Eigenray `E` semantics

- A hit is the same receiver contribution accepted by the safe geometric beam
  influence, not an exact centerline-coordinate equality.
- Each accepted hit stores receiver indices and the exclusive ray-prefix point
  count (`rightPointIndex + 1`). Hits are not merged or capacity-limited.
- Each hit writes a normal ASCII RAY block using that prefix. Bounce counts are
  those visible at the prefix endpoint; point compression follows
  `WriteRay2D`.
- File order follows source, launch angle, contribution traversal. The same
  launch angle may appear multiple times for different receivers.
- The RAY header is identical to ordinary `R`, including the original launch
  count. There is no eigenray block-count field; readers parse to EOF.

### 9. Safe run-type matrix

The first complete I8 declares only combinations with a complete, observable
two-dimensional Origin dispatch:

| Product | Beam families | Receiver layouts | Notes |
|---|---|---|---|
| `A` / `a` | Cartesian geometric hat `G`, ray-centered geometric hat `g`, Cartesian geometric Gaussian `B` | Existing family-specific rectilinear/irregular rules | Point/line and source beam patterns use existing projection/scaling |
| `E` | `G`, `g`, `B` | Existing family-specific rules | Writes variable path prefixes to `.ray` |

`C`/`R` Cerveny and `S` simple-Gaussian arrival/eigenray combinations are
recognized but explicitly rejected: current Origin 2-D source lacks a complete
`AddArr`/`WriteRay2D` dispatch for those paths. This is not redefined in F2CPP.
3D, N×2D, beam shift, and RayReuse changes remain out of scope.

### 10. Reuse and new interfaces

Directly reused: `SimulationCase`, sorted sources, `ReceiverGrid`,
`LaunchFanPlan`, `GeometryTracer`, `RayPathCache`, `FrequencyProjector`,
`RayFrequencyState`, geometric contribution calculations, reflection events,
RAY point compression, output-layout checked arithmetic, and atomic publication.

New interfaces: arrival candidate/record, capacity planner, source-local
arrival workspace and statistics, geometric contribution sink, arrival solver,
ASCII/binary arrival writer, eigenray hit/sink, eigenray solver/writer, ARR
readers, EOF-based eigenray reader, and I8 validators.

### 11. Future Bellhop_RayReuse constraints

- No I8 task may modify `Bellhop_RayReuse`.
- Geometry stays immutable and frequency independent; arrival amplitude,
  complex delay, active prefix, grouping, and capacity statistics stay in
  per-frequency temporary state.
- Solvers use source-streaming consumers instead of returning all source grids.
- Writers consume const source-local products and must not mutate the cache.
- Public types must not assume a global current frequency or Fortran-style
  module state. These constraints permit later RayReuse adoption without
  changing I8 numerical semantics.

## Scope

- Two-dimensional, one-frequency-per-process `A`, `a`, and `E`.
- Multiple source depths; rectilinear and already-supported irregular receivers.
- Safe `G`, `g`, and `B` families, point/line sources, source beam patterns,
  existing SSP/boundary acoustics, complex delay, and bounce history.
- Origin-compatible product layout, atomic output, independent readers,
  component oracle, and end-to-end validators.

## Out of Scope

- Any Bellhop_RayReuse change or multi-frequency scheduler.
- 3D, N×2D, azimuth fields, beam shift, HDF5, or a new arrivals format.
- Adding arrivals/eigenrays to incomplete Origin families (`C`, `R`, `S`).
- Sorting arrivals, globally deduplicating arrivals, root-finding a new
  mathematical eigenray definition, or changing ordinary `R` output.
- Relaxing any accepted I0–I7 numerical or parser contract.

## Task Dependency

```text
I8-01-T1 -> T2 -> T3 -> T4 -> T5
                        |      |
                        |      +------------------+
                        v                         v
I8-02-T1 -> T2 -> T3                 I8-03-T1 -> T2 -> T3 -> T4 -> T5
       \___________/                         \_____________________/
             |                                          |
             +-----------------> I8-04-T1 <--------------+
                                      |
                         I8-04-T2 -> T3 -> T4 -> T5
```

Exact `Depends On` clauses in the stage documents override this summary.
OpenCode must execute only one named task per session.

## Current Status

| Stage | Status | Tasks |
|---|---|---|
| I8-01 Arrival data model & accumulation | IN_PROGRESS | T1–T4 ACCEPTED; T5 TODO |
| I8-02 Arrival writers | TODO | T1–T3 |
| I8-03 Eigenray mode | TODO | T1–T5 |
| I8-04 Validation & documentation | TODO | T1–T5 |

I8-01-T1–T4 have established the accepted record types, checked capacity
planner, source-local workspace, and Origin-compatible candidate accumulation.
Geometric-hat contribution delivery now shares one accepted traversal across
pressure, intensity, and arrival sinks plus source-streamed orchestration.
Geometric-Gaussian arrival dispatch remains next.

## Codex / OpenCode handoff

Codex owns architecture, task definitions and status, review, tests, Origin
oracle acceptance, acceptance records, and Git commits. OpenCode implements one
task exactly as written and does not commit. Codex validates the actual working
tree rather than relying on the completion report. Failed work is not committed;
the preferred response is a narrowly scoped fix task.

Task status and `Acceptance Record` are edited only by Codex during review.
OpenCode must not edit this directory, `PROGRESS.md`, or
`FURTHER_REPLICATION_PLAN.md` unless a later task explicitly says otherwise.

## Git checkpoints

Every accepted task, or an explicitly declared tightly coupled task group,
forms a checkpoint containing implementation, tests, task status, and acceptance
evidence. Codex reviews `git diff`, stages explicit paths or hunks (never
`git add .`), then checks `git diff --cached` and
`git diff --cached --check` before committing.

## I8 complete acceptance criteria

- Every I8 task is `ACCEPTED` with an acceptance commit and recorded tests.
- AppleClang Debug ASan/UBSan, AppleClang Release, and GCC 14 Release/Werror
  pass the full CTest suite.
- The full Python suite and every F2CPP single-frequency standard case pass.
- ASCII and binary arrival readers validate headers, source/cell order, zero and
  multiple arrivals, complex delays, unwrapped phases, angles, and bounces.
- Accumulator component oracle covers strict duplicate boundaries, weighted
  merge, cusp guard, capacity replacement/drop, and first-minimum tie behavior.
- Origin/F2CPP arrival sequence and counts agree for direct, multipath/caustic,
  zero-arrival, multi-source, point/line, directional-pattern, and irregular
  cases within the frozen per-field ULP gates.
- Origin/F2CPP eigenray block count/order, prefix point counts, launch angles,
  bounce counts, and coordinates agree; the validator parses blocks to EOF.
- `A`, `a`, and `E` publish only `.arr` or `.ray` as appropriate, remove stale
  incompatible products only after success, preserve prior valid products on
  failure, and leak no `.tmp` files.
- Unsupported `C/R/S` arrival/eigenray combinations, 3D, N×2D, and beam shift
  fail explicitly.
- Machine-readable reports are generated in `doc/validation`, project progress
  and usage documents are updated, `git diff --check` passes, and the working
  tree is clean.
