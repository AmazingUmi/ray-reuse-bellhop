#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "rayreuse/field/cartesian_cerveny_influence.hpp"
#include "rayreuse/field/frequency_workspace.hpp"
#include "rayreuse/model/simulation_case.hpp"
#include "rayreuse/solver/ray_reuse_frequency_consumer.hpp"
#include "rayreuse/solver/single_frequency_solver.hpp"

namespace rayreuse {

struct SerialRayReuseFrequencyResult {
  FrequencyWorkspace workspace;
  SingleFrequencyTimings timings;
};

struct SerialRayReuseStatistics {
  std::size_t tracePassCount{};
  std::size_t rayCount{};
  std::size_t totalRayPointCount{};
  std::size_t rayCacheBytes{};
  SingleFrequencyTimings phaseTotals;
  double wallSeconds{};
  bool cacheFingerprintVerified{};
  std::uint64_t cacheFingerprintBefore{};
  std::uint64_t cacheFingerprintAfter{};
};

struct SerialRayReuseResult {
  // Compatibility collection API. Input frequency order is preserved.
  // New callers can use solveStreaming to keep only one workspace resident.
  std::vector<SerialRayReuseFrequencyResult> frequencyResults;
  SerialRayReuseStatistics statistics;
};

class SerialRayReuseSolver {
 public:
  [[nodiscard]] static SerialRayReuseStatistics solveStreaming(
      const SimulationCase& simulation, double epsilonMultiplier,
      double loopRange, const RayReuseFrequencyConsumer& consumer,
      CartesianCervenySettings influenceSettings = {},
      bool verifyCacheFingerprint = false);

  [[nodiscard]] static SerialRayReuseResult solve(
      const SimulationCase& simulation, double epsilonMultiplier,
      double loopRange, CartesianCervenySettings influenceSettings = {},
      bool verifyCacheFingerprint = false);
};

}  // namespace rayreuse
