#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "rayreuse/field/cartesian_cerveny_influence.hpp"
#include "rayreuse/field/frequency_workspace.hpp"
#include "rayreuse/field/fused_pressure_workspace.hpp"
#include "rayreuse/model/simulation_case.hpp"
#include "rayreuse/solver/ray_reuse_frequency_consumer.hpp"
#include "rayreuse/solver/single_frequency_solver.hpp"

namespace rayreuse {

// Level-B parity seam (design §3.1): raw (unscaled) per-frequency workspaces
// plus block-level timings and fused-run Influence statistics.
struct FusedAccumulationResult {
  // Raw accumulated fields in fused [range][depth][frequency] storage.
  FusedPressureWorkspace rawWorkspace;
  // scaleSeconds == 0; projectSeconds/influenceSeconds are block-level;
  // influenceStatistics holds the fused-run counters of design §5.
  SingleFrequencyTimings timings;
  std::size_t rayCount{};
  std::size_t totalRayPointCount{};
  std::size_t rayCacheBytes{};
};

// Same fields and meanings as SerialRayReuseStatistics (fused-run shape, so
// the PRT writer / fingerprint reporting stay reuse-compatible).
struct FusedRayReuseStatistics {
  std::size_t tracePassCount{};
  std::size_t rayCount{};
  std::size_t totalRayPointCount{};
  std::size_t rayCacheBytes{};
  SingleFrequencyTimings phaseTotals;
  double wallSeconds{};
  bool cacheFingerprintVerified{};
  std::uint64_t cacheFingerprintBefore{};
  std::uint64_t cacheFingerprintAfter{};
  std::vector<std::uint64_t> sourceCacheFingerprintsBefore;
  std::vector<std::uint64_t> sourceCacheFingerprintsAfter;
};

// IGR-1 R03 experimental serial fused orchestration.  Structurally separate
// from SerialRayReuseSolver (D10): traces the frozen fan once, projects and
// accumulates all frequencies per ray through the fused Cartesian Cerveny
// kernel, then scales and delivers per frequency in index order.  Scope
// (design §2): transmission loss, Cartesian Cerveny coherent pressure,
// rectilinear uniform-range receivers, single source, >= 2 frequencies;
// everything else is rejected deterministically (no silent fallback).
class FusedRayReuseSolver {
 public:
  // Level-B seam: no tracing, no scaling, no cache mutation, no consumer.
  // `sourceCache` must be frozen and traced from simulation.sources().front().
  [[nodiscard]] static FusedAccumulationResult accumulateFrequencies(
      const SimulationCase& simulation, const RayPathCache& sourceCache,
      double epsilonMultiplier, double loopRange,
      CartesianCervenySettings influenceSettings = {});

  // Production entry; mirrors SerialRayReuseSolver::solveStreaming semantics
  // (consumer invoked per frequency index after that frequency's scale).
  [[nodiscard]] static FusedRayReuseStatistics solveStreaming(
      const SimulationCase& simulation, double epsilonMultiplier,
      double loopRange, const RayReuseFrequencyConsumer& consumer,
      CartesianCervenySettings influenceSettings = {},
      bool verifyCacheFingerprint = false);
};

}  // namespace rayreuse
