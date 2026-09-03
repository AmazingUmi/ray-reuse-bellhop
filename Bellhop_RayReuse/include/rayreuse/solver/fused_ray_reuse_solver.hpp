#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "rayreuse/field/cartesian_cerveny_influence.hpp"
#include "rayreuse/field/frequency_workspace.hpp"
#include "rayreuse/field/fused_intensity_workspace.hpp"
#include "rayreuse/field/fused_pressure_workspace.hpp"
#include "rayreuse/model/simulation_case.hpp"
#include "rayreuse/solver/ray_reuse_frequency_consumer.hpp"
#include "rayreuse/solver/single_frequency_solver.hpp"

namespace rayreuse {

// Single source of truth for the production fused solver's scientific and
// receiver-grid support boundary. CLI compatibility warnings use this same
// predicate so they cannot advertise fused as a replacement for a case the
// solver will reject.
[[nodiscard]] bool supportsFusedRayReuse(
    const SimulationCase& simulation);

struct FusedRayReuseExecutionSettings {
  std::size_t requestedRangeWorkers{1U};
};

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
  std::size_t requestedRangeWorkers{};
  std::size_t effectiveRangeWorkers{};
};

// IGR-3A intensity twin of FusedAccumulationResult (design §3.3/§6.2): same
// fields and meanings; the raw payload is the double-lane fused intensity
// workspace exposed as the Level-B seam for raw intensity parity.
struct FusedIntensityAccumulationResult {
  // Raw accumulated intensity in fused [range][depth][frequency] storage.
  FusedIntensityWorkspace rawIntensityWorkspace;
  SingleFrequencyTimings timings;
  std::size_t rayCount{};
  std::size_t totalRayPointCount{};
  std::size_t rayCacheBytes{};
  std::size_t requestedRangeWorkers{};
  std::size_t effectiveRangeWorkers{};
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
  std::size_t requestedRangeWorkers{};
  std::size_t effectiveRangeWorkers{};
};

// Production RayReuse broadband TL orchestration for coherent Cartesian
// Cerveny pressure. Traces the frozen fan once, projects and accumulates all
// frequencies per ray through the fused kernel, then scales and delivers per
// frequency in index order. Optional static range partitioning preserves the
// serial accumulation stream for every pressure cell.
class FusedRayReuseSolver {
 public:
  // Level-B seam: no tracing, no scaling, no cache mutation, no consumer.
  // `sourceCache` must be frozen and traced from simulation.sources().front().
  [[nodiscard]] static FusedAccumulationResult accumulateFrequencies(
      const SimulationCase& simulation, const RayPathCache& sourceCache,
      double epsilonMultiplier, double loopRange,
      CartesianCervenySettings influenceSettings = {},
      FusedRayReuseExecutionSettings executionSettings = {});

  // IGR-3A A01 interface freeze (design §3.3/§6.2): intensity twin of
  // accumulateFrequencies for the incoherent/semi-coherent fused sink. The
  // fused scope gate is unchanged and still requires the coherent run mode,
  // so a non-coherent request fails validation with today's message until
  // the family gate widens in A02b.
  [[nodiscard]] static FusedIntensityAccumulationResult
  accumulateFrequenciesIntensity(
      const SimulationCase& simulation, const RayPathCache& sourceCache,
      double epsilonMultiplier, double loopRange,
      CartesianCervenySettings influenceSettings = {},
      FusedRayReuseExecutionSettings executionSettings = {});

  // Production entry; mirrors SerialRayReuseSolver::solveStreaming semantics
  // (consumer invoked per frequency index after that frequency's scale).
  [[nodiscard]] static FusedRayReuseStatistics solveStreaming(
      const SimulationCase& simulation, double epsilonMultiplier,
      double loopRange, const RayReuseFrequencyConsumer& consumer,
      CartesianCervenySettings influenceSettings = {},
      bool verifyCacheFingerprint = false,
      FusedRayReuseExecutionSettings executionSettings = {});

 private:
  // Unified fused executor (design §3.1-§3.2), parameterized over a kernel
  // adapter (src/solver/fused_influence_adapters.hpp) and a sink policy
  // (CoherentFusedSink / IntensityFusedSink); instantiated implicitly inside
  // fused_ray_reuse_solver.cpp only. Since A02 this template is the single
  // worker/partition/projection/exception/timing implementation; the public
  // entries are thin dispatchers.
  template <typename Adapter, typename Sink>
  [[nodiscard]] static typename Sink::Result accumulateFrequenciesImpl(
      const SimulationCase& simulation, const RayPathCache& sourceCache,
      double epsilonMultiplier, double loopRange,
      CartesianCervenySettings influenceSettings,
      FusedRayReuseExecutionSettings executionSettings);
};

}  // namespace rayreuse
