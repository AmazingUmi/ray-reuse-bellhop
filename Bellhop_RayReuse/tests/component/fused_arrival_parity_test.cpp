#include "rayreuse/solver/fused_ray_reuse_solver.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <iostream>
#include <numbers>
#include <string>
#include <utility>
#include <vector>

#include "rayreuse/field/arrival_workspace.hpp"
#include "rayreuse/field/frequency_projector.hpp"
#include "rayreuse/field/geometric_gaussian_influence.hpp"
#include "rayreuse/field/geometric_hat_influence.hpp"
#include "rayreuse/model/simulation_case.hpp"
#include "rayreuse/solver/single_frequency_solver.hpp"
#include "support/test_harness.hpp"

namespace {

using rayreuse::BeamFamily;
using rayreuse::BoundaryCurvatureMode;
using rayreuse::BoundaryModel;
using rayreuse::CervenyCoordinateSystem;
using rayreuse::Environment;
using rayreuse::FieldComponent;
using rayreuse::FrequencyGrid;
using rayreuse::FrequencyProjector;
using rayreuse::GeometricGaussianInfluence;
using rayreuse::GeometricHatInfluence;
using rayreuse::IntegratorSettings;
using rayreuse::LaunchFan;
using rayreuse::ReceiverGrid;
using rayreuse::SimulationCase;
using rayreuse::SimulationRunMode;
using rayreuse::SoundSpeedPoint;
using rayreuse::SoundSpeedProfile;
using rayreuse::Source;
using rayreuse::SourceBeamPattern;
using rayreuse::test::Context;

[[nodiscard]] SimulationCase makeCase(
    BeamFamily family, CervenyCoordinateSystem coordinates,
    SimulationRunMode runMode) {
  constexpr double kRadiansPerDegree = std::numbers::pi / 180.0;
  return SimulationCase(
      Environment(
          SoundSpeedProfile(
              {SoundSpeedPoint{
                   .depth = 0.0, .soundSpeed = 1500.0, .density = 1000.0},
               SoundSpeedPoint{
                   .depth = 100.0, .soundSpeed = 1510.0, .density = 1000.0}}),
          BoundaryModel::vacuum(0.0), BoundaryModel::rigid(100.0)),
      std::vector<Source>{{.depth = 30.0, .amplitude = 0.75},
                          {.depth = 70.0, .amplitude = 1.25}},
      ReceiverGrid({15.0, 50.0, 85.0},
                   {10.0, 25.0, 40.0, 55.0, 70.0, 85.0, 100.0}),
      FrequencyGrid({75.0, 225.0, 675.0}),
      LaunchFan{.minimumAngle = -18.0 * kRadiansPerDegree,
                .maximumAngle = 18.0 * kRadiansPerDegree,
                .explicitLaunchAngleCount = 81U},
      IntegratorSettings{.stepLength = 5.0,
                         .rangeLimit = 120.0,
                         .depthLimit = 120.0,
                         .maximumRayPoints = 1000U},
      SourceBeamPattern::omnidirectional(), runMode, family,
      FieldComponent::Pressure, BoundaryCurvatureMode::Standard,
      rayreuse::BeamWidthMode::MinimumWidth, coordinates);
}

[[nodiscard]] rayreuse::ArrivalWorkspace legacyArrivals(
    const SimulationCase& simulation, const rayreuse::RayPathCache& cache,
    std::size_t sourceIndex, std::size_t frequencyIndex) {
  const double frequency =
      simulation.frequencies().values().at(frequencyIndex);
  const Source& source = simulation.sources().at(sourceIndex);
  rayreuse::ArrivalWorkspace workspace(frequency, simulation.receivers());
  const FrequencyProjector projector(simulation.environment());
  const GeometricHatInfluence hat(simulation.receivers(),
                                  simulation.cervenyCoordinateSystem(),
                                  simulation.sourceGeometry());
  const GeometricGaussianInfluence gaussian(simulation.receivers(),
                                            simulation.sourceGeometry());
  for (const rayreuse::RayPath& path : cache.paths()) {
    const double amplitude =
        source.amplitude *
        simulation.sourceBeamPattern().amplitudeForLaunchAngle(
            path.launchAngle);
    const rayreuse::RayFrequencyState state =
        projector.project(path, frequency, amplitude);
    if (simulation.beamFamily() == BeamFamily::GeometricGaussian) {
      gaussian.accumulateArrivals(
          workspace, path, state,
          simulation.launchFanPlan().launchAngleStep);
    } else {
      hat.accumulateArrivals(workspace, path, state,
                             simulation.launchFanPlan().launchAngleStep);
    }
  }
  return workspace;
}

void checkParity(Context& context, const SimulationCase& simulation,
                 std::size_t sourceIndex, const std::string& label) {
  const rayreuse::RayFanTraceResult trace =
      rayreuse::SingleFrequencySolver::traceSourceFan(simulation, sourceIndex);
  const std::uint64_t fingerprint = trace.cache.contentFingerprint();

  std::vector<rayreuse::ArrivalWorkspace> legacy;
  legacy.reserve(simulation.frequencies().size());
  std::size_t legacyCandidates = 0U;
  for (std::size_t frequencyIndex = 0U;
       frequencyIndex < simulation.frequencies().size(); ++frequencyIndex) {
    legacy.push_back(
        legacyArrivals(simulation, trace.cache, sourceIndex, frequencyIndex));
    legacyCandidates += legacy.back().candidateCount();
  }

  const rayreuse::FusedArrivalAccumulationResult fusedOne =
      rayreuse::FusedRayReuseSolver::accumulateArrivalFrequencies(
          simulation, trace.cache, sourceIndex, {},
          {.requestedRangeWorkers = 1U});
  const rayreuse::FusedArrivalAccumulationResult fusedFour =
      rayreuse::FusedRayReuseSolver::accumulateArrivalFrequencies(
          simulation, trace.cache, sourceIndex, {},
          {.requestedRangeWorkers = 4U});

  const auto checkResult = [&](
                               const rayreuse::FusedArrivalAccumulationResult&
                                   fused,
                               std::size_t requestedWorkers) {
    std::size_t storedArrivals = 0U;
    for (std::size_t frequencyIndex = 0U;
         frequencyIndex < simulation.frequencies().size(); ++frequencyIndex) {
      const auto actual = fused.rawWorkspace.frequencyView(frequencyIndex);
      const auto one = fusedOne.rawWorkspace.frequencyView(frequencyIndex);
      for (std::size_t depthIndex = 0U;
           depthIndex < simulation.receivers().receiversPerRange();
           ++depthIndex) {
        for (std::size_t rangeIndex = 0U;
             rangeIndex < simulation.receivers().rangeCount(); ++rangeIndex) {
          const std::span<const rayreuse::Arrival> expected =
              legacy[frequencyIndex].arrivalsAt(depthIndex, rangeIndex);
          const std::span<const rayreuse::Arrival> observed =
              actual.arrivalsAt(depthIndex, rangeIndex);
          const std::span<const rayreuse::Arrival> serialFused =
              one.arrivalsAt(depthIndex, rangeIndex);
          const bool legacyEqual =
              expected.size() == observed.size() &&
              (expected.empty() ||
               std::memcmp(expected.data(), observed.data(),
                           expected.size_bytes()) == 0);
          const bool workerEqual =
              serialFused.size() == observed.size() &&
              (serialFused.empty() ||
               std::memcmp(serialFused.data(), observed.data(),
                           serialFused.size_bytes()) == 0);
          const std::string cell =
              label + " w" + std::to_string(requestedWorkers) +
              " frequency " + std::to_string(frequencyIndex) + " cell (" +
              std::to_string(depthIndex) + "," +
              std::to_string(rangeIndex) + ")";
          context.check(legacyEqual,
                        cell + " preserves legacy count and ordered bytes");
          context.check(workerEqual,
                        cell + " is bitwise identical to fused w1");
          storedArrivals += observed.size();
        }
      }
    }
    context.check(storedArrivals > 0U,
                  label + " exercises at least one stored arrival");
    context.check(
        fused.arrivalStatistics.candidateCount == legacyCandidates,
        label + " w" + std::to_string(requestedWorkers) +
            " preserves candidate statistics");
    context.check(
        fused.requestedRangeWorkers == requestedWorkers &&
            fused.effectiveRangeWorkers ==
                std::min(requestedWorkers,
                         simulation.receivers().rangeCount()),
        label + " reports requested/effective static range workers");
    context.check(fused.rayCount == trace.cache.size() &&
                      fused.rayCacheBytes ==
                          trace.cache.memoryFootprintBytes(),
                  label + " reports the frozen cache metrics");
    context.check(fused.timings.traceSeconds == 0.0 &&
                      fused.timings.scaleSeconds == 0.0 &&
                      fused.timings.projectSeconds >= 0.0 &&
                      fused.timings.influenceSeconds >= 0.0,
                  label + " reports raw fused projection/influence time");
    context.check(
        fused.rawWorkspace.storageStatistics().memoryFootprintBytes > 0U,
        label + " reports source-local broadband workspace memory");
  };

  checkResult(fusedOne, 1U);
  checkResult(fusedFour, 4U);
  context.check(
      fusedOne.arrivalStatistics.candidateCount ==
              fusedFour.arrivalStatistics.candidateCount &&
          fusedOne.arrivalStatistics.appendCount ==
              fusedFour.arrivalStatistics.appendCount &&
          fusedOne.arrivalStatistics.mergeCount ==
              fusedFour.arrivalStatistics.mergeCount &&
          fusedOne.arrivalStatistics.cuspGuardCount ==
              fusedFour.arrivalStatistics.cuspGuardCount &&
          fusedOne.arrivalStatistics.weakestReplacementCount ==
              fusedFour.arrivalStatistics.weakestReplacementCount &&
          fusedOne.arrivalStatistics.capacityDiscardCount ==
              fusedFour.arrivalStatistics.capacityDiscardCount &&
          fusedOne.arrivalStatistics.saturatedCellCount ==
              fusedFour.arrivalStatistics.saturatedCellCount,
      label + " w1/w4 worker-local statistics merge identically");
  context.check(trace.cache.contentFingerprint() == fingerprint,
                label + " w1/w4 leave the frozen source cache unchanged");
}

}  // namespace

int main() {
  Context context;
  checkParity(context,
              makeCase(BeamFamily::GeometricHat,
                       CervenyCoordinateSystem::Cartesian,
                       SimulationRunMode::AsciiArrivals),
              0U, "G/A source0");
  checkParity(context,
              makeCase(BeamFamily::GeometricHat,
                       CervenyCoordinateSystem::RayCentered,
                       SimulationRunMode::BinaryArrivals),
              1U, "g/a source1");
  checkParity(context,
              makeCase(BeamFamily::GeometricGaussian,
                       CervenyCoordinateSystem::Cartesian,
                       SimulationRunMode::AsciiArrivals),
              1U, "B/A source1");
  checkParity(context,
              makeCase(BeamFamily::GeometricGaussian,
                       CervenyCoordinateSystem::Cartesian,
                       SimulationRunMode::BinaryArrivals),
              0U, "B/a source0");

  if (context.failureCount() != 0) {
    std::cerr << context.failureCount()
              << " fused-arrival-parity assertion(s) failed\n";
    return 1;
  }
  std::cout << "All Bellhop RayReuse fused-arrival-parity tests passed\n";
  return 0;
}
