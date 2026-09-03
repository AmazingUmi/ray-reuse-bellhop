// IGR-3A src-internal unified-executor interface (design §3-§6). Not
// installed: the adapter set and the sink policy pair are compile-time
// executor plumbing; the public solver surface stays in
// rayreuse/solver/fused_ray_reuse_solver.hpp. Adapters hold no data and no
// logic beyond kernel construction, per-ray preparation, forwarding to the
// kernels' private fused entries, and the legacy post-scale selector.

#pragma once

#include <complex>
#include <cstddef>
#include <span>
#include <utility>
#include <vector>

#include "rayreuse/error.hpp"
#include "rayreuse/field/beam_epsilon.hpp"
#include "rayreuse/field/cartesian_cerveny_influence.hpp"
#include "rayreuse/field/frequency_workspace.hpp"
#include "rayreuse/field/fused_intensity_workspace.hpp"
#include "rayreuse/field/fused_pressure_workspace.hpp"
#include "rayreuse/field/pressure_scaling.hpp"
#include "rayreuse/model/simulation_case.hpp"
#include "rayreuse/ray/ray_path.hpp"
#include "rayreuse/solver/fused_ray_reuse_solver.hpp"

namespace rayreuse {

// Cartesian Cerveny adapter (design §4). makeKernel mirrors the
// single-frequency CC construction verbatim
// (single_frequency_solver.cpp:264-266); preparePerRay is the epsilon loop
// of the fused solver body (fused_ray_reuse_solver.cpp:223-231);
// accumulateFused forwards to the unchanged private fused kernel entry;
// scaleFrequency reproduces the coherent Cerveny branch of the legacy
// post-scale selector (single_frequency_solver.cpp:377-379). The intensity
// twins are A02b construction and reject invocation until that task lands
// (unreachable in production today: the fused scope gate still requires the
// coherent run mode).
struct CartesianCervenyFusedAdapter {
  using Kernel = CartesianCervenyInfluence;

  // Frozen scratch shape (design §4): the per-ray epsilon lane, one value
  // per frequency, materialized by prepareScratch.
  struct PerRayScratch {
    std::vector<std::complex<double>> epsilons;
  };

  // Frozen loop-invariant input set of the per-ray epsilon loop (design
  // §4): widthMode, sourceSoundSpeed (source c0), sourceDepthGradient,
  // launchAngleStep, loopRange, epsilonMultiplier. Every adapter's context
  // is constructible from this same executor-owned argument list so the
  // unified executor stays family-agnostic; geometric families (A03+)
  // define the same constructor and ignore the Cerveny-only fields.
  struct PerRayContext {
    BeamWidthMode widthMode{};
    double sourceSoundSpeed{};
    double sourceDepthGradient{};
    double launchAngleStep{};
    double loopRange{};
    double epsilonMultiplier{};

    PerRayContext(BeamWidthMode widthModeValue, double sourceSoundSpeedValue,
                  double sourceDepthGradientValue,
                  double launchAngleStepValue, double loopRangeValue,
                  double epsilonMultiplierValue)
        : widthMode(widthModeValue),
          sourceSoundSpeed(sourceSoundSpeedValue),
          sourceDepthGradient(sourceDepthGradientValue),
          launchAngleStep(launchAngleStepValue),
          loopRange(loopRangeValue),
          epsilonMultiplier(epsilonMultiplierValue) {}
  };

  [[nodiscard]] static Kernel makeKernel(const SimulationCase& simulation,
                                         CartesianCervenySettings settings) {
    return Kernel(simulation.environment(), simulation.receivers(), settings,
                  simulation.beamWidthMode(), simulation.sourceGeometry());
  }

  static void prepareScratch(PerRayScratch& scratch,
                             std::size_t frequencyCount) {
    scratch.epsilons.assign(frequencyCount, std::complex<double>{});
  }

  static void preparePerRay(const PerRayContext& context,
                            PerRayScratch& scratch, const RayPath& path,
                            std::span<const double> frequencies) {
    for (std::size_t frequencyIndex = 0U; frequencyIndex < frequencies.size();
         ++frequencyIndex) {
      const BeamEpsilon epsilon = pickBeamEpsilon(
          context.widthMode, frequencies[frequencyIndex],
          context.sourceSoundSpeed, context.sourceDepthGradient,
          path.launchAngle, context.launchAngleStep, context.loopRange,
          context.epsilonMultiplier);
      scratch.epsilons[frequencyIndex] = epsilon.value;
    }
  }

  [[nodiscard]] static bool accumulateFused(
      const Kernel& kernel, const PerRayScratch& scratch,
      FusedPressureWorkspace& workspace,
      std::span<const double> frequencies, const RayPath& path,
      std::span<const RayFrequencyState> frequencyStates,
      std::size_t rangeBegin, std::size_t rangeEnd,
      CartesianCervenyStatistics* statistics) {
    return kernel.accumulateFusedPrevalidated(
        workspace, frequencies, path, frequencyStates,
        std::span<const std::complex<double>>(scratch.epsilons), rangeBegin,
        rangeEnd, statistics);
  }

  static void scaleFrequency(FrequencyWorkspace& workspace,
                             const ReceiverGrid& receivers,
                             double launchAngleStep, double sourceSoundSpeed,
                             SourceGeometry sourceGeometry) {
    scaleCoherentCartesianPressure(workspace, receivers, launchAngleStep,
                                   sourceSoundSpeed, sourceGeometry);
  }

  [[nodiscard]] static bool accumulateFusedIntensity(
      const Kernel& /*kernel*/, const PerRayScratch& /*scratch*/,
      FusedIntensityWorkspace& /*workspace*/,
      std::span<const double> /*frequencies*/, const RayPath& /*path*/,
      std::span<const RayFrequencyState> /*frequencyStates*/,
      std::size_t /*rangeBegin*/, std::size_t /*rangeEnd*/,
      CartesianCervenyStatistics* /*statistics*/) {
    throw ValidationError(
        "Cartesian Cerveny fused intensity kernel is not implemented "
        "(IGR-3A A02b)");
  }

  [[nodiscard]] static FrequencyWorkspace scaleIntensityFrequency(
      const IntensityWorkspace& /*workspace*/,
      const ReceiverGrid& /*receivers*/, double /*launchAngleStep*/,
      double /*sourceSoundSpeed*/, SourceGeometry /*sourceGeometry*/) {
    throw ValidationError(
        "Cartesian Cerveny fused intensity kernel is not implemented "
        "(IGR-3A A02b)");
  }
};

// Sink policies (design §3.1/§6.2): compile-time selection of everything
// mode-specific in the unified executor — the raw workspace type and its
// construction, the adapter accumulation hook, and the result type. Closed
// pair; the executor body is mode-agnostic.
struct CoherentFusedSink {
  using Workspace = FusedPressureWorkspace;
  using Result = FusedAccumulationResult;

  [[nodiscard]] static Workspace makeWorkspace(
      const ReceiverGrid& receivers, std::size_t frequencyCount) {
    return Workspace(receivers, frequencyCount);
  }

  template <typename Adapter>
  [[nodiscard]] static bool accumulate(
      const typename Adapter::Kernel& kernel,
      const typename Adapter::PerRayScratch& scratch, Workspace& workspace,
      std::span<const double> frequencies, const RayPath& path,
      std::span<const RayFrequencyState> frequencyStates,
      std::size_t rangeBegin, std::size_t rangeEnd,
      CartesianCervenyStatistics* statistics) {
    return Adapter::accumulateFused(kernel, scratch, workspace, frequencies,
                                    path, frequencyStates, rangeBegin,
                                    rangeEnd, statistics);
  }

  [[nodiscard]] static Result makeResult(
      Workspace&& rawWorkspace, const SingleFrequencyTimings& timings,
      std::size_t rayCount, std::size_t totalRayPointCount,
      std::size_t rayCacheBytes, std::size_t requestedRangeWorkers,
      std::size_t effectiveRangeWorkers) {
    return Result{
        .rawWorkspace = std::move(rawWorkspace),
        .timings = timings,
        .rayCount = rayCount,
        .totalRayPointCount = totalRayPointCount,
        .rayCacheBytes = rayCacheBytes,
        .requestedRangeWorkers = requestedRangeWorkers,
        .effectiveRangeWorkers = effectiveRangeWorkers};
  }
};

struct IntensityFusedSink {
  using Workspace = FusedIntensityWorkspace;
  using Result = FusedIntensityAccumulationResult;

  [[nodiscard]] static Workspace makeWorkspace(
      const ReceiverGrid& receivers, std::size_t frequencyCount) {
    return Workspace(receivers, frequencyCount);
  }

  template <typename Adapter>
  [[nodiscard]] static bool accumulate(
      const typename Adapter::Kernel& kernel,
      const typename Adapter::PerRayScratch& scratch, Workspace& workspace,
      std::span<const double> frequencies, const RayPath& path,
      std::span<const RayFrequencyState> frequencyStates,
      std::size_t rangeBegin, std::size_t rangeEnd,
      CartesianCervenyStatistics* statistics) {
    return Adapter::accumulateFusedIntensity(kernel, scratch, workspace,
                                             frequencies, path,
                                             frequencyStates, rangeBegin,
                                             rangeEnd, statistics);
  }

  [[nodiscard]] static Result makeResult(
      Workspace&& rawWorkspace, const SingleFrequencyTimings& timings,
      std::size_t rayCount, std::size_t totalRayPointCount,
      std::size_t rayCacheBytes, std::size_t requestedRangeWorkers,
      std::size_t effectiveRangeWorkers) {
    return Result{
        .rawIntensityWorkspace = std::move(rawWorkspace),
        .timings = timings,
        .rayCount = rayCount,
        .totalRayPointCount = totalRayPointCount,
        .rayCacheBytes = rayCacheBytes,
        .requestedRangeWorkers = requestedRangeWorkers,
        .effectiveRangeWorkers = effectiveRangeWorkers};
  }
};

}  // namespace rayreuse
