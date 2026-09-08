# IGR-3 Scope & Architecture Decision

Status: **ACCEPTED / CLOSED (2026-09-04)**

This document records the authoritative user-approved IGR-3 direction and its
closure. IGR-3A and IGR-3B were independently accepted and committed as
`dda1c2c` and `0050f59`; the
[`IGR-3A worklist`](IGR-3A_TL_BEAM_FAMILY_ADAPTATION_WORKLIST.md) and
[`IGR-3B worklist`](IGR-3B_ARRIVAL_FUSED_INFLUENCE_WORKLIST.md) remain the
execution evidence.

## 1. Fixed execution direction

RayReuse broadband Influence uses one execution architecture:

```text
Cross-Frequency Fused
        +
Static Range Parallelism
```

These are Influence execution capabilities, not permanent properties of the
Cartesian Cerveny kernel. Cartesian Cerveny TL is the accepted IGR-1/IGR-2
reference implementation. Static range parallelism is an optional execution
optimization; fused serial remains the deterministic execution reference.

## 2. Architectural separation

```text
Execution architecture
    !=
Beam-family physics kernel
    !=
Contribution/output sink
```

The IGR-3 organizing principle is:

```text
One execution architecture
    + multiple beam-family kernels
    + multiple contribution sinks
```

The execution layer may share frozen geometry traversal and static range
ownership. Each beam-family kernel retains its own scientific formula. TL and
Arrival retain distinct contribution and output lifecycles.

## 3. IGR-3A — Remaining TL Beam Family Adaptation

IGR-3A adapted the fixed execution architecture to:

- Ray-Centered Cerveny TL;
- Geometric Gaussian TL;
- Geometric Hat TL;
- Simple Gaussian TL.

Cartesian Cerveny TL remains the existing reference and regression baseline;
IGR-3A does not redefine its scientific formula.

## 4. IGR-3B — Arrival Fused Influence and Broadband Arrival Layout

After IGR-3A acceptance, IGR-3B adapted the current `A/a` geometric contribution
paths:

- Geometric Gaussian (`B`);
- Cartesian Geometric Hat (`G`);
- Ray-Centered Geometric Hat (`g`).

The accepted implementation provides cross-frequency fused Arrival
contribution, static range parallelism, a source-local `[R][D][F]` broadband
Arrival layout, shared AddArr semantics, and source-streamed frequency-view
writer delivery with transactional publication.

`A/a` uses geometric beam contribution and influence-style receiver traversal.
The current `GeometricHatInfluence` and `GeometricGaussianInfluence` Arrival
paths produce `ArrivalCandidate` values. The architectural difference from TL
is primarily the sink:

```text
TL sink       -> pressure / intensity
Arrival sink  -> ArrivalCandidate -> AddArr-compatible arrival lane
```

IGR-3B preserves Origin-compatible `AddArr` encounter order and ARR byte
identity with legacy reuse.

## 5. R/E boundary

- `R` is a ray product and remains outside fused Influence accumulation and
  adaptation.
- `E` is outside IGR-3 fused Influence construction. Geometric beam code may
  share helpers or traversal with `E`, so `E` remains a regression boundary.
- This decision changes no `R/E` product behavior or current support status.

Precisely: IGR-3 does not alter `R/E` execution because they are outside the
IGR-3 fused Influence accumulation/adaptation path.

## 6. Construction sequencing

```text
IGR-3A
  -> independent acceptance
  -> commit
  -> IGR-3B
  -> independent acceptance
  -> commit
```

The required sequence was documentation preflight, IGR-3A independent
acceptance and commit, then IGR-3B construction and independent acceptance.

The sequence completed as required: IGR-3A was accepted and committed first,
then IGR-3B was constructed, independently accepted, and committed. IGR-3 is
closed; this document authorizes no subsequent batch.

## 7. Non-goals

- frequency interpolation;
- BARR;
- GPU execution;
- a new beam family;
- scientific formula changes;
- dynamic scheduling;
- frequency-parallel redesign;
- unrelated product expansion.
