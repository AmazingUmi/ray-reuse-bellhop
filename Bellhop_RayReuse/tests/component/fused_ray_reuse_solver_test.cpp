#include "rayreuse/solver/fused_ray_reuse_solver.hpp"

#include <algorithm>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <numbers>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "rayreuse/error.hpp"
#include "rayreuse/model/simulation_case.hpp"
#include "rayreuse/solver/serial_ray_reuse_solver.hpp"
#include "rayreuse/solver/single_frequency_solver.hpp"
#include "support/test_harness.hpp"

namespace {

using rayreuse::BeamFamily;
using rayreuse::BeamWidthMode;
using rayreuse::BoundaryCurvatureMode;
using rayreuse::BoundaryModel;
using rayreuse::CartesianCervenySettings;
using rayreuse::CervenyCoordinateSystem;
using rayreuse::Environment;
using rayreuse::FieldComponent;
using rayreuse::FrequencyGrid;
using rayreuse::FrequencyWorkspace;
using rayreuse::FusedRayReuseSolver;
using rayreuse::FusedRayReuseStatistics;
using rayreuse::IntegratorSettings;
using rayreuse::LaunchFan;
using rayreuse::ReceiverGrid;
using rayreuse::ReceiverGridLayout;
using rayreuse::SerialRayReuseResult;
using rayreuse::SerialRayReuseSolver;
using rayreuse::SimulationCase;
using rayreuse::SimulationRunMode;
using rayreuse::SingleFrequencyTimings;
using rayreuse::SoundSpeedPoint;
using rayreuse::SoundSpeedProfile;
using rayreuse::Source;
using rayreuse::SourceBeamPattern;
using rayreuse::ValidationError;
using rayreuse::test::Context;

// Small in-scope fused fixture: CC coherent, Cartesian, single source,
// rectilinear uniform-range grid, two frequencies.
SimulationCase makeSimulation(
    SimulationRunMode runMode = SimulationRunMode::Coherent,
    BeamFamily beamFamily = BeamFamily::CervenyGaussian,
    CervenyCoordinateSystem coordinates = CervenyCoordinateSystem::Cartesian,
    FrequencyGrid frequencies = FrequencyGrid({50.0, 100.0}),
    ReceiverGrid receivers = ReceiverGrid({25.0, 50.0, 75.0},
                                          {10.0, 55.0, 100.0})) {
  return SimulationCase(
      Environment(
          SoundSpeedProfile(
              {SoundSpeedPoint{
                   .depth = 0.0, .soundSpeed = 1500.0, .density = 1000.0},
               SoundSpeedPoint{
                   .depth = 100.0, .soundSpeed = 1500.0, .density = 1000.0}}),
          BoundaryModel::vacuum(0.0), BoundaryModel::rigid(100.0)),
      Source{.depth = 50.0, .amplitude = 1.0}, receivers, frequencies,
      LaunchFan{.minimumAngle = -2.0 * std::numbers::pi / 180.0,
                .maximumAngle = 2.0 * std::numbers::pi / 180.0,
                .explicitLaunchAngleCount = 300U},
      IntegratorSettings{.stepLength = 10.0,
                         .rangeLimit = 110.0,
                         .depthLimit = 110.0,
                         .maximumRayPoints = 100U},
      SourceBeamPattern::omnidirectional(), runMode, beamFamily,
      FieldComponent::Pressure, BoundaryCurvatureMode::Standard,
      BeamWidthMode::MinimumWidth, coordinates);
}

template <typename Function>
[[nodiscard]] std::optional<std::string> capturedValidationMessage(
    Function&& function) {
  try {
    function();
  } catch (const ValidationError& error) {
    return std::string(error.what());
  }
  return std::nullopt;
}

void noOpConsumer(std::size_t, std::vector<FrequencyWorkspace>&&,
                  const SingleFrequencyTimings&) {}

void testSolverScopeRejections(Context& context) {
  std::vector<std::string> messages;
  const auto reject = [&context,
                       &messages](SimulationCase bad, const char* label) {
    const std::optional<std::string> message =
        capturedValidationMessage([&bad] {
          static_cast<void>(FusedRayReuseSolver::solveStreaming(
              bad, 1.0, 50.0, noOpConsumer));
        });
    context.check(message.has_value(), label);
    messages.push_back(message.value_or(std::string("no throw for ") + label));
  };

  reject(makeSimulation(SimulationRunMode::Incoherent),
         "non-coherent run mode is rejected by the fused solver");
  reject(makeSimulation(SimulationRunMode::Coherent, BeamFamily::GeometricHat),
         "non-Cerveny beam family is rejected by the fused solver");
  reject(makeSimulation(SimulationRunMode::Coherent,
                        BeamFamily::CervenyGaussian,
                        CervenyCoordinateSystem::RayCentered),
         "ray-centered Cerveny is rejected by the fused solver");
  reject(makeSimulation(SimulationRunMode::Coherent,
                        BeamFamily::CervenyGaussian,
                        CervenyCoordinateSystem::Cartesian,
                        FrequencyGrid({50.0})),
         "single-frequency run is rejected by the fused solver");
  reject(makeSimulation(SimulationRunMode::Coherent,
                        BeamFamily::CervenyGaussian,
                        CervenyCoordinateSystem::Cartesian,
                        FrequencyGrid({50.0, 100.0}),
                        ReceiverGrid({10.0, 55.0}, {25.0, 75.0},
                                     ReceiverGridLayout::Irregular)),
         "irregular receiver grid is rejected by the fused solver");
  {
    SimulationCase multiSource(
        Environment(
            SoundSpeedProfile(
                {SoundSpeedPoint{
                     .depth = 0.0, .soundSpeed = 1500.0, .density = 1000.0},
                 SoundSpeedPoint{
                     .depth = 100.0, .soundSpeed = 1500.0, .density = 1000.0}}),
            BoundaryModel::vacuum(0.0), BoundaryModel::rigid(100.0)),
        std::vector<Source>{{.depth = 50.0, .amplitude = 1.0},
                            {.depth = 60.0, .amplitude = 1.0}},
        ReceiverGrid({25.0, 50.0, 75.0}, {10.0, 55.0, 100.0}),
        FrequencyGrid({50.0, 100.0}),
        LaunchFan{.minimumAngle = -2.0 * std::numbers::pi / 180.0,
                  .maximumAngle = 2.0 * std::numbers::pi / 180.0,
                  .explicitLaunchAngleCount = 300U},
        IntegratorSettings{.stepLength = 10.0,
                           .rangeLimit = 110.0,
                           .depthLimit = 110.0,
                           .maximumRayPoints = 100U});
    reject(multiSource, "multi-source run is rejected by the fused solver");
  }

  for (const std::string& message : messages) {
    context.check(message.starts_with("fused ray-reuse solver"),
                  "solver-level fused rejection carries the fused prefix: " +
                      message);
  }
  std::vector<std::string> sorted = messages;
  std::sort(sorted.begin(), sorted.end());
  context.check(std::adjacent_find(sorted.begin(), sorted.end()) ==
                    sorted.end(),
                "solver-level fused rejection messages are distinct");

  // Frozen-cache defense in depth on the Level-B seam: a non-frozen cache is
  // rejected before any work.
  const SimulationCase simulation = makeSimulation();
  rayreuse::RayPathCache unfrozenCache;
  const std::optional<std::string> unfrozenMessage =
      capturedValidationMessage([&simulation, &unfrozenCache] {
        static_cast<void>(FusedRayReuseSolver::accumulateFrequencies(
            simulation, unfrozenCache, 1.0, 50.0));
      });
  context.check(
      unfrozenMessage.has_value() &&
          unfrozenMessage->starts_with("fused ray-reuse solver"),
      "an unfrozen cache is rejected by the fused Level-B seam");
}

void testFusedStreamingMatchesSerialReuse(Context& context) {
  const SimulationCase simulation = makeSimulation();
  const CartesianCervenySettings settings{.collectStatistics = true};
  const SerialRayReuseResult reuse =
      SerialRayReuseSolver::solve(simulation, 1.0, 50.0, settings, true);

  std::vector<std::optional<std::vector<FrequencyWorkspace>>> streamed(
      simulation.frequencies().size());
  std::vector<std::size_t> callbackCounts(
      simulation.frequencies().size(), 0U);
  std::vector<std::size_t> callbackOrder;
  std::vector<double> callbackScaleSeconds;
  const FusedRayReuseStatistics statistics =
      FusedRayReuseSolver::solveStreaming(
      simulation, 1.0, 50.0,
      [&](std::size_t frequencyIndex,
          std::vector<FrequencyWorkspace>&& sourceWorkspaces,
          const SingleFrequencyTimings& timings) {
        ++callbackCounts.at(frequencyIndex);
        callbackOrder.push_back(frequencyIndex);
        callbackScaleSeconds.push_back(timings.scaleSeconds);
        streamed.at(frequencyIndex).emplace(std::move(sourceWorkspaces));
      },
      settings, true);

  context.check(
      callbackOrder == std::vector<std::size_t>{0U, 1U},
      "fused streaming callback preserves frequency order");
  context.check(callbackCounts == std::vector<std::size_t>{1U, 1U},
                "fused streaming consumes every frequency once");
  context.check(
      statistics.tracePassCount == 1U &&
          statistics.rayCount ==
              simulation.launchFanPlan().launchAngleCount &&
          statistics.rayCacheBytes > 0U &&
          statistics.totalRayPointCount > statistics.rayCount,
      "fused streaming reports one trace pass and the shared cache metrics");

  for (std::size_t frequencyIndex = 0U;
       frequencyIndex < simulation.frequencies().size(); ++frequencyIndex) {
    const std::optional<std::vector<FrequencyWorkspace>>& workspaces =
        streamed[frequencyIndex];
    const FrequencyWorkspace& expected =
        reuse.frequencyResults[frequencyIndex].workspaces.front();
    context.check(
        workspaces.has_value() && workspaces->size() == 1U &&
            workspaces->front().frequency() == expected.frequency() &&
            workspaces->front().depthCount() == expected.depthCount() &&
            workspaces->front().rangeCount() == expected.rangeCount() &&
            std::equal(workspaces->front().pressure().begin(),
                       workspaces->front().pressure().end(),
                       expected.pressure().begin(), expected.pressure().end()),
        "fused frequency " + std::to_string(frequencyIndex) +
            " is bitwise equal to serial reuse");
    context.check(callbackScaleSeconds[frequencyIndex] >= 0.0,
                  "fused per-frequency consumer timings carry the scale time");
  }

  context.check(
      statistics.cacheFingerprintVerified &&
          statistics.cacheFingerprintBefore ==
              statistics.cacheFingerprintAfter &&
          statistics.cacheFingerprintBefore ==
              reuse.statistics.cacheFingerprintBefore,
      "fused streaming leaves the frozen cache unchanged and matches the "
      "reuse fingerprint");
  context.check(
      statistics.wallSeconds >= 0.0 &&
          statistics.phaseTotals.traceSeconds >= 0.0 &&
          statistics.phaseTotals.projectSeconds >= 0.0 &&
          statistics.phaseTotals.influenceSeconds >= 0.0 &&
          statistics.phaseTotals.scaleSeconds >= 0.0,
      "fused streaming exposes the block-level phase timings");
}

void testFusedCounterSemantics(Context& context) {
  const SimulationCase simulation = makeSimulation();
  const CartesianCervenySettings settings{.collectStatistics = true};
  const SerialRayReuseResult reuse =
      SerialRayReuseSolver::solve(simulation, 1.0, 50.0, settings, true);
  const FusedRayReuseStatistics statistics =
      FusedRayReuseSolver::solveStreaming(simulation, 1.0, 50.0,
                                          noOpConsumer, settings, true);
  const rayreuse::CartesianCervenyStatistics& fused =
      statistics.phaseTotals.influenceStatistics;
  const rayreuse::CartesianCervenyStatistics& baseline =
      reuse.statistics.phaseTotals.influenceStatistics;

  context.check(
      fused.rayAccumulations == baseline.rayAccumulations &&
          fused.activeRayPoints == baseline.activeRayPoints,
      "fused per-frequency-kernel entry counters stay baseline-equal");
  context.check(
      fused.frequencyRangeKernelEvaluations ==
              baseline.frequencyRangeKernelEvaluations &&
          fused.frequencyImageKernelEvaluations ==
              baseline.frequencyImageKernelEvaluations &&
          fused.imageEvaluations == fused.frequencyImageKernelEvaluations,
      "fused frequency-kernel counters equal the reuse totals and keep the "
      "image identity");
  context.check(
      fused.windowRejections == baseline.windowRejections &&
          fused.taperRejections == baseline.taperRejections &&
          fused.nonzeroImageContributions ==
              baseline.nonzeroImageContributions,
      "fused image-kernel rejection counters stay baseline-equal");
  context.check(
      fused.validatedRayPoints == 0U && fused.validatedWorkspaceValues == 0U,
      "fused prevalidated route never restores full validation scans");
  context.check(
      fused.geometrySegmentEvaluations < baseline.geometrySegmentEvaluations &&
          fused.geometryRangeEvaluations < baseline.geometryRangeEvaluations &&
          fused.geometryDepthEvaluations < baseline.geometryDepthEvaluations &&
          fused.geometryImageGeometryEvaluations <
              baseline.geometryImageGeometryEvaluations &&
          fused.receiverRangeEvaluations <=
              baseline.receiverRangeEvaluations &&
          fused.receiverDepthEvaluations <= baseline.receiverDepthEvaluations,
      "fused shared-geometry counters deduplicate the per-frequency sum");
}

}  // namespace

int main() {
  Context context;
  testSolverScopeRejections(context);
  testFusedStreamingMatchesSerialReuse(context);
  testFusedCounterSemantics(context);

  if (context.failureCount() != 0) {
    std::cerr << context.failureCount()
              << " fused-ray-reuse-solver assertion(s) failed\n";
    return 1;
  }
  std::cout << "All Bellhop RayReuse fused-ray-reuse-solver tests passed\n";
  return 0;
}
