#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <numbers>
#include <utility>
#include <vector>

#include "bellhop/error.hpp"
#include "bellhop/model/simulation_case.hpp"
#include "bellhop/solver/arrival_solver.hpp"
#include "support/test_harness.hpp"

namespace {

using bellhop::Arrival;
using bellhop::ArrivalSolver;
using bellhop::ArrivalSolverStatistics;
using bellhop::BeamFamily;
using bellhop::BoundaryModel;
using bellhop::CervenyCoordinateSystem;
using bellhop::Environment;
using bellhop::FrequencyGrid;
using bellhop::IntegratorSettings;
using bellhop::LaunchFan;
using bellhop::ReceiverGrid;
using bellhop::ReceiverGridLayout;
using bellhop::SimulationCase;
using bellhop::SimulationRunMode;
using bellhop::SoundSpeedPoint;
using bellhop::SoundSpeedProfile;
using bellhop::Source;
using bellhop::SourceBeamPattern;
using bellhop::SourceBeamPatternSample;
using bellhop::SourceGeometry;
using bellhop::ValidationError;
using bellhop::VolumeAttenuation;
using bellhop::VolumeAttenuationModel;
using bellhop::test::Context;

SimulationCase makeSimulation(
    SimulationRunMode mode = SimulationRunMode::AsciiArrivals,
    BeamFamily family = BeamFamily::GeometricHat,
    CervenyCoordinateSystem coordinates = CervenyCoordinateSystem::Cartesian,
    ReceiverGridLayout layout = ReceiverGridLayout::Rectilinear,
    SourceBeamPattern pattern = SourceBeamPattern::omnidirectional(),
    SourceGeometry geometry = SourceGeometry::Point,
    std::size_t maximumRayPoints = 4000U) {
  const std::vector<double> depths{20.0, 50.0, 80.0};
  return SimulationCase(
      Environment(
          SoundSpeedProfile(
              {SoundSpeedPoint{.depth = 0.0,
                               .soundSpeed = 1500.0,
                               .density = 1000.0},
               SoundSpeedPoint{.depth = 100.0,
                               .soundSpeed = 1500.0,
                               .density = 1000.0}}),
          BoundaryModel::vacuum(0.0), BoundaryModel::rigid(100.0),
          VolumeAttenuation{.model = VolumeAttenuationModel::Thorp}),
      std::vector<Source>{{.depth = 30.0, .amplitude = 1.0},
                          {.depth = 70.0, .amplitude = 2.0}},
      ReceiverGrid(depths, {100.0, 300.0, 500.0}, layout),
      FrequencyGrid({1000.0}),
      LaunchFan{.minimumAngle = -30.0 * std::numbers::pi / 180.0,
                .maximumAngle = 30.0 * std::numbers::pi / 180.0,
                .explicitLaunchAngleCount = 7U},
      IntegratorSettings{.stepLength = 5.0,
                         .rangeLimit = 550.0,
                         .depthLimit = 105.0,
                         .maximumRayPoints = maximumRayPoints},
      std::move(pattern), mode, bellhop::FieldComponent::Pressure, geometry,
      coordinates, family);
}

std::vector<Arrival> flatten(const bellhop::ArrivalWorkspace& workspace) {
  std::vector<Arrival> result;
  for (std::size_t cell = 0U; cell < workspace.receiverCellCount(); ++cell) {
    const auto arrivals = workspace.cellAt(cell);
    result.insert(result.end(), arrivals.begin(), arrivals.end());
  }
  return result;
}

void testSourceStreamingAndProjection(Context& context) {
  const SimulationCase simulation = makeSimulation();
  std::vector<std::size_t> sourceOrder;
  std::vector<std::vector<Arrival>> sourceArrivals;
  const ArrivalSolverStatistics stats = ArrivalSolver::solve(
      simulation,
      [&](std::size_t sourceIndex, const bellhop::RayPathCache& cache,
          const bellhop::ArrivalWorkspace& workspace) {
        sourceOrder.push_back(sourceIndex);
        context.check(cache.frozen() &&
                          cache.size() ==
                              simulation.launchFanPlan().launchAngleCount,
                      "arrival consumer sees one complete frozen fan");
        sourceArrivals.push_back(flatten(workspace));
      });

  context.check(sourceOrder == std::vector<std::size_t>{0U, 1U},
                "arrival source workspaces stream in sorted order");
  context.check(sourceArrivals.size() == 2U &&
                    !sourceArrivals[0U].empty() &&
                    !sourceArrivals[1U].empty(),
                "each source receives a distinct nonempty arrival grid");
  const std::size_t expectedRayCount =
      2U * simulation.launchFanPlan().launchAngleCount;
  context.check(stats.sourceCount == 2U &&
                    stats.rayCount == expectedRayCount &&
                    stats.projectedRayCount == expectedRayCount &&
                    stats.totalRayPointCount > stats.rayCount &&
                    stats.candidateCount > 0U &&
                    stats.peakRayCacheBytes > 0U &&
                    stats.peakArrivalWorkspaceBytes > 0U,
                "arrival solver reports checked lifecycle statistics");

  bool sawComplexDelay = false;
  bool sawReflection = false;
  for (const auto& arrivals : sourceArrivals) {
    for (const Arrival& arrival : arrivals) {
      sawComplexDelay = sawComplexDelay || arrival.delaySeconds.imag() < 0.0F;
      sawReflection = sawReflection || arrival.topBounceCount > 0 ||
                      arrival.bottomBounceCount > 0;
    }
  }
  context.check(sawComplexDelay,
                "projected attenuation survives in complex arrival delay");
  context.check(sawReflection,
                "projected reflected paths retain prefix bounce counts");
}

void testPatternAmplitudeAndContracts(Context& context) {
  const auto maximumAmplitude = [](const SimulationCase& simulation) {
    float maximum = 0.0F;
    static_cast<void>(ArrivalSolver::solve(
        simulation,
        [&](std::size_t, const bellhop::RayPathCache&,
            const bellhop::ArrivalWorkspace& workspace) {
          for (const Arrival& arrival : flatten(workspace)) {
            maximum = std::max(maximum, arrival.amplitude);
          }
        }));
    return maximum;
  };
  const float base = maximumAmplitude(makeSimulation());
  const float half = maximumAmplitude(makeSimulation(
      SimulationRunMode::AsciiArrivals, BeamFamily::GeometricHat,
      CervenyCoordinateSystem::Cartesian,
      ReceiverGridLayout::Rectilinear,
      SourceBeamPattern::directional(
          std::vector<SourceBeamPatternSample>{{-30.0, -6.020599913279624},
                                               {30.0, -6.020599913279624}})));
  context.checkNear(half / base, 0.5, 2.0e-6,
                    "directional source amplitude reaches candidates once");
  context.check(
      maximumAmplitude(makeSimulation(
          SimulationRunMode::BinaryArrivals,
          BeamFamily::GeometricGaussian)) > 0.0F,
      "arrival solver dispatches Cartesian geometric-Gaussian beams");

  const auto consumer = [](std::size_t, const bellhop::RayPathCache&,
                           const bellhop::ArrivalWorkspace&) {};
  context.expectThrows<ValidationError>(
      [&] {
        static_cast<void>(ArrivalSolver::solve(
            makeSimulation(SimulationRunMode::CoherentTransmissionLoss),
            consumer));
      },
      "arrival solver rejects field run modes");
  context.expectThrows<ValidationError>(
      [&] {
        static_cast<void>(ArrivalSolver::solve(
            makeSimulation(SimulationRunMode::AsciiArrivals,
                           BeamFamily::CervenyGaussian),
            consumer));
      },
      "arrival solver rejects incomplete beam families");
  context.expectThrows<ValidationError>(
      [&] {
        static_cast<void>(ArrivalSolver::solve(makeSimulation(), {}));
      },
      "arrival solver rejects an empty consumer");
  context.expectThrows<ValidationError>(
      [&] {
        static_cast<void>(ArrivalSolver::solve(
            makeSimulation(SimulationRunMode::AsciiArrivals,
                           BeamFamily::GeometricHat,
                           CervenyCoordinateSystem::RayCentered,
                           ReceiverGridLayout::Irregular),
            consumer));
      },
      "arrival solver retains ray-centered receiver restrictions");
  context.expectThrows<ValidationError>(
      [&] {
        static_cast<void>(ArrivalSolver::solve(
            makeSimulation(SimulationRunMode::AsciiArrivals,
                           BeamFamily::GeometricGaussian,
                           CervenyCoordinateSystem::RayCentered),
            consumer));
      },
      "geometric-Gaussian arrival dispatch remains Cartesian-only");

  std::size_t partialCalls = 0U;
  context.expectThrows<ValidationError>(
      [&] {
        static_cast<void>(ArrivalSolver::solve(
            makeSimulation(SimulationRunMode::AsciiArrivals,
                           BeamFamily::GeometricHat,
                           CervenyCoordinateSystem::Cartesian,
                           ReceiverGridLayout::Rectilinear,
                           SourceBeamPattern::omnidirectional(),
                           SourceGeometry::Point, 2U),
            [&](std::size_t, const bellhop::RayPathCache&,
                const bellhop::ArrivalWorkspace&) { ++partialCalls; }));
      },
      "abnormal ray termination rejects the source before delivery");
  context.check(partialCalls == 0U,
                "abnormal first source produces no partial consumer call");
}

}  // namespace

int main() {
  Context context;
  testSourceStreamingAndProjection(context);
  testPatternAmplitudeAndContracts(context);
  if (context.failureCount() != 0) {
    std::cerr << context.failureCount()
              << " arrival solver assertion(s) failed\n";
    return 1;
  }
  std::cout << "All arrival solver tests passed\n";
  return 0;
}
