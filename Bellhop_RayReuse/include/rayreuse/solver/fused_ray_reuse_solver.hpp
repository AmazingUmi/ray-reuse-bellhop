#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

#include "rayreuse/field/cartesian_cerveny_influence.hpp"
#include "rayreuse/field/broadband_arrival_workspace.hpp"
#include "rayreuse/field/frequency_workspace.hpp"
#include "rayreuse/field/fused_intensity_workspace.hpp"
#include "rayreuse/field/fused_pressure_workspace.hpp"
#include "rayreuse/model/simulation_case.hpp"
#include "rayreuse/solver/ray_reuse_frequency_consumer.hpp"
#include "rayreuse/solver/arrival_solver.hpp"
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

// B03 raw source-local Arrival seam. The executor writes directly into the
// broadband [range][depth][frequency] lanes; no legacy workspace is
// materialized.
struct FusedArrivalAccumulationResult {
  BroadbandArrivalWorkspace rawWorkspace;
  SingleFrequencyTimings timings;
  ArrivalAccumulationStatistics arrivalStatistics;
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

// Called once per source, in SimulationCase::sources() order. The broadband
// workspace is source-local and valid only for the duration of the callback;
// consumers should append its zero-copy frequency views synchronously.
using FusedArrivalSourceConsumer = std::function<void(
    std::size_t sourceIndex, const BroadbandArrivalWorkspace& workspace)>;

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

  // IGR-3A A02b (design §3.3/§6.2): intensity twin of accumulateFrequencies
  // for the incoherent/semi-coherent fused sink. The fused scope gate covers
  // every TL run mode of Cartesian Cerveny; callers select the sink to match
  // the run mode (solveStreaming does), and the raw payload is the
  // double-lane FusedIntensityWorkspace of the returned result.
  [[nodiscard]] static FusedIntensityAccumulationResult
  accumulateFrequenciesIntensity(
      const SimulationCase& simulation, const RayPathCache& sourceCache,
      double epsilonMultiplier, double loopRange,
      CartesianCervenySettings influenceSettings = {},
      FusedRayReuseExecutionSettings executionSettings = {});

  // One frozen source cache -> all-frequency Arrival lanes. This source-aware
  // seam is intentionally independent of the TL single-source eligibility
  // predicate so B05 can stream a multi-source run one source at a time.
  [[nodiscard]] static FusedArrivalAccumulationResult
  accumulateArrivalFrequencies(
      const SimulationCase& simulation, const RayPathCache& sourceCache,
      std::size_t sourceIndex,
      CartesianCervenySettings influenceSettings = {},
      FusedRayReuseExecutionSettings executionSettings = {});

  // Trace one source, accumulate all frequencies into one broadband
  // workspace, consume it, then release both workspace and cache before
  // advancing to the next source.
  [[nodiscard]] static ArrivalSolverStatistics solveArrivalStreaming(
      const SimulationCase& simulation,
      const FusedArrivalSourceConsumer& consumer,
      CartesianCervenySettings influenceSettings = {},
      bool verifyCacheFingerprint = false,
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
      FusedRayReuseExecutionSettings executionSettings,
      std::size_t sourceIndex = 0U);
};

}  // namespace rayreuse
