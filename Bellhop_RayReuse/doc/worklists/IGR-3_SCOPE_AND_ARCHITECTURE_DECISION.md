# IGR-3 Scope & Architecture Decision

Status: **USER-FROZEN / PRE-CONSTRUCTION**

This document is the authoritative handoff of user-approved IGR-3 direction. It
is not the IGR-3 design and is not a construction worklist. No IGR-3 production
capability is implied until the corresponding batch is independently accepted.

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

IGR-3A adapts the fixed execution architecture to:

- Ray-Centered Cerveny TL;
- Geometric Gaussian TL;
- Geometric Hat TL;
- Simple Gaussian TL.

Cartesian Cerveny TL remains the existing reference and regression baseline;
IGR-3A does not redefine its scientific formula.

## 4. IGR-3B — Arrival Fused Influence and Broadband Arrival Layout

After IGR-3A acceptance, IGR-3B adapts the current `A/a` geometric contribution
paths:

- Geometric Gaussian (`B`);
- Cartesian Geometric Hat (`G`);
- Ray-Centered Geometric Hat (`g`).

The frozen objectives are cross-frequency fused Arrival contribution, static
range parallelism, a broadband Arrival hot data structure, and an optimized
writer/materialization lifecycle. The lifecycle direction is source-streamed,
frequency-view writer delivery; this decision does not freeze a class name or
container implementation.

`A/a` uses geometric beam contribution and influence-style receiver traversal.
The current `GeometricHatInfluence` and `GeometricGaussianInfluence` Arrival
paths produce `ArrivalCandidate` values. The architectural difference from TL
is primarily the sink:

```text
TL sink       -> pressure / intensity
Arrival sink  -> ArrivalCandidate -> AddArr-compatible arrival lane
```

IGR-3B must preserve Origin-compatible `AddArr` encounter order.

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

Documentation preflight precedes IGR-3A. Construction has not started, and
IGR-3B must not begin before IGR-3A is independently accepted and committed.

## 7. Non-goals

- frequency interpolation;
- BARR;
- GPU execution;
- a new beam family;
- scientific formula changes;
- dynamic scheduling;
- frequency-parallel redesign;
- unrelated product expansion.
