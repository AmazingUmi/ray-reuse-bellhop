#include <cstddef>
#include <iostream>
#include <numbers>
#include <utility>
#include <vector>

#include "bellhop/error.hpp"
#include "bellhop/model/simulation_case.hpp"
#include "bellhop/solver/eigenray_solver.hpp"
#include "support/test_harness.hpp"

namespace {

using bellhop::BeamFamily;
using bellhop::BoundaryModel;
using bellhop::CervenyCoordinateSystem;
using bellhop::EigenraySolver;
using bellhop::EigenraySolverStatistics;
using bellhop::Environment;
using bellhop::FrequencyGrid;
using bellhop::IntegratorSettings;
using bellhop::LaunchFan;
using bellhop::ReceiverGrid;
using bellhop::SimulationCase;
using bellhop::SimulationRunMode;
using bellhop::SoundSpeedPoint;
using bellhop::SoundSpeedProfile;
using bellhop::Source;
using bellhop::SourceGeometry;
using bellhop::ValidationError;
using bellhop::VolumeAttenuation;
using bellhop::VolumeAttenuationModel;
using bellhop::test::Context;

SimulationCase makeSimulation(
    SimulationRunMode mode,
    BeamFamily family = BeamFamily::GeometricHat) {
  return SimulationCase(
      Environment(
          SoundSpeedProfile({SoundSpeedPoint{.depth = 0.0,
                                               .soundSpeed = 1500.0,
                                               .density = 1000.0},
                            SoundSpeedPoint{.depth = 100.0,
                                            .soundSpeed = 1500.0,
                                            .density = 1000.0}}),
          BoundaryModel::vacuum(0.0), BoundaryModel::rigid(100.0),
          VolumeAttenuation{.model = VolumeAttenuationModel::Thorp}),
      std::vector<Source>{{.depth = 25.0, .amplitude = 1.0},
                          {.depth = 75.0, .amplitude = 1.0}},
      ReceiverGrid({25.0, 50.0, 75.0}, {100.0, 300.0, 500.0}),
      FrequencyGrid({1000.0}),
      LaunchFan{.minimumAngle = -30.0 * std::numbers::pi / 180.0,
                .maximumAngle = 30.0 * std::numbers::pi / 180.0,
                .explicitLaunchAngleCount = 7U},
      IntegratorSettings{.stepLength = 5.0,
                         .rangeLimit = 550.0,
                         .depthLimit = 105.0,
                         .maximumRayPoints = 4000U},
      bellhop::SourceBeamPattern::omnidirectional(), mode,
      bellhop::FieldComponent::Pressure, SourceGeometry::Point,
      CervenyCoordinateSystem::Cartesian, family);
}

void testStreamingAndContracts(Context& context) {
  const SimulationCase simulation =
      makeSimulation(SimulationRunMode::Eigenray);
  std::vector<std::pair<std::size_t, std::size_t>> order;
  const EigenraySolverStatistics statistics = EigenraySolver::solve(
      simulation,
      [&](std::size_t source, std::size_t launch,
          const bellhop::RayPathCache& cache, const bellhop::RayPath& path,
          const bellhop::EigenrayHit& hit) {
        order.emplace_back(source, launch);
        context.check(cache.frozen() && &cache.at(launch) == &path &&
                          hit.prefixPointCount >= 2U &&
                          hit.prefixPointCount <= path.points.size(),
                      "E solver exposes each prefix in its frozen source cache");
      });
  context.check(statistics.sourceCount == simulation.sourceCount() &&
                    statistics.rayCount ==
                        simulation.sourceCount() *
                            simulation.launchFanPlan().launchAngleCount &&
                    statistics.projectedRayCount == statistics.rayCount,
                "E solver processes every source and launch ray");
  for (std::size_t index = 1U; index < order.size(); ++index) {
    context.check(order[index - 1U] <= order[index],
                  "E hit consumer is source/launch ordered");
  }
  context.check(statistics.totalPrefixPointCount >=
                    2U * statistics.totalHitCount,
                "E solver reports checked prefix totals");

  std::size_t gaussianHitCount = 0U;
  const EigenraySolverStatistics gaussianStatistics = EigenraySolver::solve(
      makeSimulation(SimulationRunMode::Eigenray,
                     BeamFamily::GeometricGaussian),
      [&](std::size_t, std::size_t, const bellhop::RayPathCache& cache,
          const bellhop::RayPath& path, const bellhop::EigenrayHit& hit) {
        context.check(cache.frozen() && hit.prefixPointCount <= path.points.size(),
                      "B E solver exposes active frozen prefixes");
        ++gaussianHitCount;
      });
  context.check(gaussianStatistics.totalHitCount == gaussianHitCount &&
                    gaussianStatistics.projectedRayCount ==
                        gaussianStatistics.rayCount,
                "B E solver dispatches every launch through Gaussian influence");

  context.expectThrows<ValidationError>(
      [&] {
        static_cast<void>(EigenraySolver::solve(
            makeSimulation(SimulationRunMode::CoherentTransmissionLoss),
            [&](std::size_t, std::size_t, const bellhop::RayPathCache&,
                const bellhop::RayPath&,
                const bellhop::EigenrayHit&) {}));
      },
      "E solver rejects field mode");
  context.expectThrows<ValidationError>(
      [&] {
        static_cast<void>(EigenraySolver::solve(simulation, {}));
      },
      "E solver rejects empty consumer");
  context.expectThrows<ValidationError>(
      [&] {
        static_cast<void>(EigenraySolver::solve(
            makeSimulation(SimulationRunMode::Eigenray,
                           BeamFamily::SimpleGaussian),
            [&](std::size_t, std::size_t, const bellhop::RayPathCache&,
                const bellhop::RayPath&, const bellhop::EigenrayHit&) {}));
      },
      "E solver rejects an incomplete beam-family dispatch");
}

}  // namespace

int main() {
  Context context;
  testStreamingAndContracts(context);
  if (context.failureCount() != 0) {
    std::cerr << context.failureCount() << " eigenray solver assertion(s) failed\n";
    return 1;
  }
  std::cout << "All eigenray solver tests passed\n";
  return 0;
}
