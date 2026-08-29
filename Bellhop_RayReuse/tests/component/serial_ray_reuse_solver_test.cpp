#include "rayreuse/solver/serial_ray_reuse_solver.hpp"

#include <algorithm>
#include <complex>
#include <cstddef>
#include <iostream>
#include <memory>
#include <numbers>
#include <optional>
#include <utility>
#include <vector>

#include "rayreuse/model/simulation_case.hpp"
#include "rayreuse/solver/broadband_nonreuse_solver.hpp"
#include "rayreuse/solver/single_frequency_solver.hpp"
#include "support/munk_case_fixture.hpp"
#include "support/test_harness.hpp"

namespace {

using rayreuse::BiologicalAttenuationLayers;
using rayreuse::BoundaryModel;
using rayreuse::BroadbandNonReuseResult;
using rayreuse::BroadbandNonReuseSolver;
using rayreuse::Environment;
using rayreuse::FrancoisGarrisonParameters;
using rayreuse::FrequencyGrid;
using rayreuse::IntegratorSettings;
using rayreuse::LaunchAngleDegreeBounds;
using rayreuse::LaunchFan;
using rayreuse::ReceiverGrid;
using rayreuse::SerialRayReuseFrequencyResult;
using rayreuse::SerialRayReuseResult;
using rayreuse::SerialRayReuseSolver;
using rayreuse::SimulationCase;
using rayreuse::SoundSpeedPoint;
using rayreuse::SoundSpeedProfile;
using rayreuse::Source;
using rayreuse::VolumeAttenuation;
using rayreuse::VolumeAttenuationModel;
using rayreuse::test::Context;

VolumeAttenuation makeThorpAttenuation() {
  return VolumeAttenuation{.model = VolumeAttenuationModel::Thorp};
}

VolumeAttenuation makeFrancoisGarrisonAttenuation(double temperature) {
  return VolumeAttenuation{
      .model = VolumeAttenuationModel::FrancoisGarrison,
      .parameters = FrancoisGarrisonParameters{.temperatureCelsius = temperature,
                                               .salinityPsu = 35.0,
                                               .pH = 8.0,
                                               .meanDepthMeters = 50.0}};
}

VolumeAttenuation makeBiologicalAttenuation(double coefficient) {
  return VolumeAttenuation{
      .model = VolumeAttenuationModel::Biological,
      .parameters =
          std::make_shared<const BiologicalAttenuationLayers>(
              BiologicalAttenuationLayers{{
                  .minimumDepth = 0.0,
                  .maximumDepth = 100.0,
                  .resonanceFrequency = 100.0,
                  .qualityFactor = 2.0,
                  .attenuationCoefficientDecibelsPerKilometer = coefficient}})};
}

SimulationCase makeSimulation(VolumeAttenuation volumeAttenuation = {}) {
  return SimulationCase(
      Environment(
          SoundSpeedProfile(
              {SoundSpeedPoint{
                   .depth = 0.0, .soundSpeed = 1500.0, .density = 1000.0},
               SoundSpeedPoint{
                   .depth = 100.0, .soundSpeed = 1500.0, .density = 1000.0}}),
          BoundaryModel::vacuum(0.0), BoundaryModel::rigid(100.0),
          std::move(volumeAttenuation)),
      Source{.depth = 50.0, .amplitude = 1.0},
      ReceiverGrid({25.0, 50.0, 75.0}, {10.0, 55.0, 100.0}),
      FrequencyGrid({50.0, 100.0}),
      LaunchFan{.minimumAngle = -2.0 * std::numbers::pi / 180.0,
                .maximumAngle = 2.0 * std::numbers::pi / 180.0,
                .explicitLaunchAngleCount = 300U},
      IntegratorSettings{.stepLength = 10.0,
                         .rangeLimit = 110.0,
                         .depthLimit = 110.0,
                         .maximumRayPoints = 100U});
}

void checkPressureEqual(Context& context,
                        const SerialRayReuseFrequencyResult& actual,
                        const rayreuse::SingleFrequencyResult& expected,
                        const char* message) {
  context.check(
      actual.workspaces.size() == expected.sourceCount() &&
          actual.workspaces.front().frequency() ==
              expected.workspace.frequency() &&
          actual.workspaces.front().depthCount() ==
              expected.workspace.depthCount() &&
          actual.workspaces.front().rangeCount() ==
              expected.workspace.rangeCount() &&
          std::equal(actual.workspaces.front().pressure().begin(),
                     actual.workspaces.front().pressure().end(),
                     expected.workspace.pressure().begin(),
                     expected.workspace.pressure().end()),
      message);
}

void testTwoFrequencySerialReuse(Context& context) {
  const SimulationCase simulation = makeSimulation();
  const BroadbandNonReuseResult nonReuse =
      BroadbandNonReuseSolver::solve(simulation, 1.0, 50.0);
  const SerialRayReuseResult reuse = SerialRayReuseSolver::solve(
      simulation, 1.0, 50.0,
      rayreuse::CartesianCervenySettings{.collectStatistics = true}, true);

  context.check(reuse.frequencyResults.size() == 2U &&
                    reuse.frequencyResults[0].workspaces.front().frequency() == 50.0 &&
                    reuse.frequencyResults[1].workspaces.front().frequency() == 100.0,
                "serial reuse preserves input frequency order");
  checkPressureEqual(context, reuse.frequencyResults[0],
                     nonReuse.frequencyResults[0],
                     "first reused frequency is bitwise equal to non-reuse");
  checkPressureEqual(context, reuse.frequencyResults[1],
                     nonReuse.frequencyResults[1],
                     "second reused frequency is bitwise equal to non-reuse");

  context.check(
      reuse.statistics.tracePassCount == 1U &&
          nonReuse.statistics.tracePassCount == 2U,
      "serial reuse traces once while two-frequency non-reuse traces twice");
  context.check(
      reuse.statistics.rayCount ==
              simulation.launchFanPlan().launchAngleCount &&
          reuse.statistics.totalRayPointCount > reuse.statistics.rayCount &&
          reuse.statistics.rayCacheBytes > 0U,
      "serial reuse reports the shared ray-cache metrics");
  context.check(
      reuse.statistics.cacheFingerprintVerified &&
          reuse.statistics.cacheFingerprintBefore ==
              reuse.statistics.cacheFingerprintAfter,
      "serial frequency projection leaves the frozen cache unchanged");

  const auto& firstTimings = reuse.frequencyResults[0].timings;
  const auto& secondTimings = reuse.frequencyResults[1].timings;
  context.check(
      firstTimings.traceSeconds == 0.0 && secondTimings.traceSeconds == 0.0 &&
          firstTimings.projectSeconds >= 0.0 &&
          firstTimings.influenceSeconds >= 0.0 &&
          firstTimings.scaleSeconds >= 0.0 &&
          secondTimings.projectSeconds >= 0.0 &&
          secondTimings.influenceSeconds >= 0.0 &&
          secondTimings.scaleSeconds >= 0.0 &&
          reuse.statistics.phaseTotals.traceSeconds >= 0.0 &&
          reuse.statistics.wallSeconds >= 0.0,
      "serial reuse exposes one trace timing and per-frequency timings");
  context.check(
      firstTimings.influenceStatistics.rayAccumulations ==
              reuse.statistics.rayCount &&
          secondTimings.influenceStatistics.rayAccumulations ==
              reuse.statistics.rayCount &&
          reuse.statistics.phaseTotals.influenceStatistics.rayAccumulations ==
              2U * reuse.statistics.rayCount &&
          reuse.statistics.phaseTotals.influenceStatistics.validatedRayPoints ==
              0U &&
          reuse.statistics.phaseTotals.influenceStatistics
                  .validatedWorkspaceValues == 0U,
      "serial reuse aggregates opt-in Influence statistics "
      "without restoring full validation scans");
}

void testEnvironmentPayloadIsExternalToFrozenCache(Context& context) {
  for (const auto& parameterPair :
       {std::pair{makeThorpAttenuation(), VolumeAttenuation{}},
        std::pair{makeFrancoisGarrisonAttenuation(10.0),
                  makeFrancoisGarrisonAttenuation(18.0)},
        std::pair{makeBiologicalAttenuation(100.0),
                  makeBiologicalAttenuation(250.0)}}) {
    const SimulationCase firstSimulation = makeSimulation(parameterPair.first);
    const SimulationCase changedSimulation = makeSimulation(parameterPair.second);
    const rayreuse::RayFanTraceResult trace =
        rayreuse::SingleFrequencySolver::traceRayFan(firstSimulation);
    const rayreuse::RayFanTraceResult changedTrace =
        rayreuse::SingleFrequencySolver::traceRayFan(changedSimulation);
    const std::uint64_t fingerprint = trace.cache.contentFingerprint();
    context.check(fingerprint == changedTrace.cache.contentFingerprint(),
                  "Thorp/FG/biological environment payload is excluded from "
                  "the frozen geometry fingerprint");

    const rayreuse::SingleFrequencyResult firstLow =
        rayreuse::SingleFrequencySolver::solveFrequencyFromCache(
            firstSimulation, 50.0, trace.cache, 1.0, 50.0);
    context.check(fingerprint == trace.cache.contentFingerprint(),
                  "first low-frequency projection leaves RayPathCache "
                  "unchanged");

    const rayreuse::SingleFrequencyResult firstHigh =
        rayreuse::SingleFrequencySolver::solveFrequencyFromCache(
            firstSimulation, 100.0, trace.cache, 1.0, 50.0);
    context.check(fingerprint == trace.cache.contentFingerprint(),
                  "high-frequency projection leaves RayPathCache unchanged");

    const rayreuse::SingleFrequencyResult repeatedLow =
        rayreuse::SingleFrequencySolver::solveFrequencyFromCache(
            firstSimulation, 50.0, trace.cache, 1.0, 50.0);
    context.check(fingerprint == trace.cache.contentFingerprint(),
                  "repeated low-frequency projection leaves RayPathCache "
                  "unchanged");

    const rayreuse::SingleFrequencyResult changedLow =
        rayreuse::SingleFrequencySolver::solveFrequencyFromCache(
            changedSimulation, 50.0, trace.cache, 1.0, 50.0);
    context.check(fingerprint == trace.cache.contentFingerprint(),
                  "cross-environment projection leaves RayPathCache "
                  "unchanged");
    context.check(
        std::equal(firstLow.workspace.pressure().begin(),
                   firstLow.workspace.pressure().end(),
                   repeatedLow.workspace.pressure().begin()),
        "serial low/high/low projection is bitwise deterministic");
    context.check(
        !std::equal(firstLow.workspace.pressure().begin(),
                    firstLow.workspace.pressure().end(),
                    changedLow.workspace.pressure().begin()),
        "changed Thorp/FG/biological environment changes pressure from the "
        "same frozen geometry");
    context.check(
        !std::equal(firstLow.workspace.pressure().begin(),
                    firstLow.workspace.pressure().end(),
                    firstHigh.workspace.pressure().begin()),
        "frequency-local attenuation state separates low and high pressure");
  }
}

void testMunkSplineFrozenGeometryAnchor(Context& context) {
  constexpr std::uint64_t kMunkSplineFingerprint = 1526667602348633172ULL;
  constexpr double kRadiansPerDegree = std::numbers::pi / 180.0;
  const SimulationCase simulation(
      rayreuse::test::makeMunkEnvironment(
          rayreuse::SspInterpolationKind::CubicSpline),
      Source{.depth = 1000.0, .amplitude = 1.0},
      ReceiverGrid({0.0, 5000.0}, {0.0, 100000.0}),
      FrequencyGrid({50.0, 250.0}),
      LaunchFan{.minimumAngle = -20.3 * kRadiansPerDegree,
                .maximumAngle = 20.3 * kRadiansPerDegree,
                .explicitLaunchAngleCount = 5000U,
                .inputDegreeBounds =
                    LaunchAngleDegreeBounds{.minimum = -20.3,
                                            .maximum = 20.3}},
      rayreuse::test::makeMunkIntegratorSettings());

  const rayreuse::RayFanTraceResult trace =
      rayreuse::SingleFrequencySolver::traceRayFan(simulation);
  context.check(trace.cache.contentFingerprint() == kMunkSplineFingerprint,
                "canonical munk_spline frozen geometry fingerprint remains "
                "anchored");
}

void testStreamingSerialReuse(Context& context) {
  const SimulationCase simulation = makeSimulation();
  const SerialRayReuseResult collected =
      SerialRayReuseSolver::solve(simulation, 1.0, 50.0);
  std::vector<std::optional<std::vector<rayreuse::FrequencyWorkspace>>>
      streamed(simulation.frequencies().size());
  std::vector<std::size_t> callbackCounts(simulation.frequencies().size(), 0U);
  std::vector<std::size_t> callbackSourceCounts(
      simulation.frequencies().size(), 0U);
  std::vector<std::size_t> callbackOrder;

  const rayreuse::SerialRayReuseStatistics statistics =
      SerialRayReuseSolver::solveStreaming(
          simulation, 1.0, 50.0,
          [&](std::size_t frequencyIndex,
              std::vector<rayreuse::FrequencyWorkspace>&& sourceWorkspaces,
              const rayreuse::SingleFrequencyTimings&) {
            ++callbackCounts.at(frequencyIndex);
            callbackSourceCounts.at(frequencyIndex) = sourceWorkspaces.size();
            callbackOrder.push_back(frequencyIndex);
            streamed.at(frequencyIndex).emplace(std::move(sourceWorkspaces));
          });

  context.check(callbackOrder == std::vector<std::size_t>{0U, 1U},
                "serial streaming callback preserves frequency order");
  context.check(callbackCounts == std::vector<std::size_t>{1U, 1U},
                "serial streaming callback consumes every frequency once");
  context.check(callbackSourceCounts ==
                    std::vector<std::size_t>{1U, 1U},
                "single-source streaming publishes one workspace per "
                "frequency");
  context.check(statistics.tracePassCount == 1U,
                "serial streaming traces the ray fan once");

  for (std::size_t index = 0U; index < streamed.size(); ++index) {
    context.check(
        streamed[index].has_value() &&
            streamed[index]->size() == 1U &&
            std::equal(
                streamed[index]->front().pressure().begin(),
                streamed[index]->front().pressure().end(),
                collected.frequencyResults[index]
                    .workspaces.front()
                    .pressure()
                    .begin(),
                collected.frequencyResults[index]
                    .workspaces.front()
                    .pressure()
                    .end()),
        "serial streamed workspace is bitwise equal to "
        "the collected result");
  }
}

}  // namespace

int main() {
  Context context;
  testTwoFrequencySerialReuse(context);
  testEnvironmentPayloadIsExternalToFrozenCache(context);
  testMunkSplineFrozenGeometryAnchor(context);
  testStreamingSerialReuse(context);

  if (context.failureCount() != 0) {
    std::cerr << context.failureCount()
              << " serial-ray-reuse-solver assertion(s) failed\n";
    return 1;
  }
  std::cout << "All Bellhop RayReuse serial-ray-reuse-solver tests passed\n";
  return 0;
}
