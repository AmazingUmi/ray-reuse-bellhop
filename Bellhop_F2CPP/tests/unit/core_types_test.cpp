#include <complex>
#include <iostream>
#include <stdexcept>
#include <utility>
#include <vector>

#include "bellhop/cache/ray_path_cache.hpp"
#include "bellhop/error.hpp"
#include "bellhop/field/frequency_workspace.hpp"
#include "bellhop/model/environment.hpp"
#include "bellhop/model/simulation_case.hpp"
#include "bellhop/numerics/vec2.hpp"
#include "support/test_harness.hpp"

namespace {

using bellhop::BoundaryModel;
using bellhop::Environment;
using bellhop::FrequencyGrid;
using bellhop::IntegratorSettings;
using bellhop::LaunchFan;
using bellhop::RayFrequencyPoint;
using bellhop::RayFrequencyState;
using bellhop::RayPath;
using bellhop::RayPathCache;
using bellhop::RayState;
using bellhop::RayTerminationReason;
using bellhop::ReceiverGrid;
using bellhop::SimulationCase;
using bellhop::SoundSpeedPoint;
using bellhop::SoundSpeedProfile;
using bellhop::Source;
using bellhop::StepQuadrature;
using bellhop::ValidationError;
using bellhop::Vec2;
using bellhop::test::Context;

Environment makeEnvironment() {
  std::vector<SoundSpeedPoint> points{
      {.depth = 0.0, .soundSpeed = 1500.0, .density = 1000.0},
      {.depth = 1000.0, .soundSpeed = 1500.0, .density = 1000.0},
  };
  return Environment(SoundSpeedProfile(std::move(points)),
                     BoundaryModel::vacuum(0.0),
                     BoundaryModel::rigid(1000.0));
}

SimulationCase makeSimulationCase(std::vector<double> frequencies) {
  return SimulationCase(
      makeEnvironment(), Source{.depth = 500.0, .amplitude = 1.0},
      ReceiverGrid({400.0, 500.0, 600.0}, {100.0, 1000.0, 5000.0}),
      FrequencyGrid(std::move(frequencies)),
      LaunchFan{
          .minimumAngle = -0.1,
          .maximumAngle = 0.1,
          .explicitLaunchAngleCount = 301U},
      IntegratorSettings{.stepLength = 10.0,
                         .rangeLimit = 5100.0,
                         .depthLimit = 1100.0,
                         .maximumRayPoints = 10000U});
}

RayPath makeRayPath() {
  RayPath path;
  path.launchAngle = 0.0;
  path.points = {
      RayState{.position = {0.0, 500.0},
               .slowness = {1.0 / 1500.0, 0.0},
               .dynamicP = {1.0, 0.0},
               .dynamicQ = {0.0, 1.0},
               .soundSpeed = 1500.0,
               .realTravelTime = 0.0},
      RayState{.position = {10.0, 500.0},
               .slowness = {1.0 / 1500.0, 0.0},
               .dynamicP = {1.0, 0.0},
               .dynamicQ = {10.0 * 1500.0, 1.0},
               .soundSpeed = 1500.0,
               .realTravelTime = 10.0 / 1500.0},
  };
  path.steps = {
      StepQuadrature{.stepLength = 10.0,
                     .startWeight = 0.0,
                     .midpointWeight = 10.0,
                     .midpoint = {5.0, 500.0}},
  };
  path.terminationReason = RayTerminationReason::ExitedDomain;
  return path;
}

RayPath makeReflectedRayPath() {
  constexpr double kSlownessComponent =
      1.0 / (1500.0 * 1.4142135623730950488);
  RayPath path;
  path.launchAngle = -0.78539816339744830962;
  path.points = {
      RayState{.position = {0.0, 10.0},
               .slowness = {kSlownessComponent, -kSlownessComponent},
               .dynamicP = {1.0, 0.0},
               .dynamicQ = {0.0, 1.0},
               .soundSpeed = 1500.0,
               .realTravelTime = 0.0},
      RayState{.position = {10.0, 0.0},
               .slowness = {kSlownessComponent, -kSlownessComponent},
               .dynamicP = {1.0, 0.0},
               .dynamicQ = {15000.0, 1.0},
               .soundSpeed = 1500.0,
               .realTravelTime = 10.0 / 1500.0},
      RayState{.position = {10.0, 0.0},
               .slowness = {kSlownessComponent, kSlownessComponent},
               .dynamicP = {1.0, 0.0},
               .dynamicQ = {15000.0, 1.0},
               .soundSpeed = 1500.0,
               .realTravelTime = 10.0 / 1500.0},
      RayState{.position = {20.0, 10.0},
               .slowness = {kSlownessComponent, kSlownessComponent},
               .dynamicP = {1.0, 0.0},
               .dynamicQ = {30000.0, 1.0},
               .soundSpeed = 1500.0,
               .realTravelTime = 20.0 / 1500.0},
  };
  path.steps = {
      StepQuadrature{.stepLength = 10.0,
                     .startWeight = 0.0,
                     .midpointWeight = 10.0,
                     .midpoint = {5.0, 5.0}},
      StepQuadrature{.stepLength = 10.0,
                     .startWeight = 0.0,
                     .midpointWeight = 10.0,
                     .midpoint = {15.0, 5.0}},
  };
  path.events = {
      {.rayPointIndex = 1U,
       .boundary = bellhop::ReflectionBoundary::SeaSurface,
       .boundarySegmentIndex = 0U,
       .position = {10.0, 0.0},
       .boundaryTangent = {1.0, 0.0},
       .outwardNormal = {0.0, -1.0},
       .incidentSlowness = {kSlownessComponent, -kSlownessComponent},
       .reflectedSlowness = {kSlownessComponent, kSlownessComponent},
       .tangentSlowness = kSlownessComponent,
       .normalSlowness = kSlownessComponent},
  };
  path.terminationReason = RayTerminationReason::ExitedDomain;
  return path;
}

void testVec2(Context& context) {
  const Vec2 value{3.0, 4.0};
  context.checkNear(bellhop::norm(value), 5.0, 1.0e-15,
                    "Vec2 norm uses range/depth components");
  context.check(bellhop::dot(value, Vec2{1.0, 2.0}) == 11.0,
                "Vec2 dot product");
  context.check(value + Vec2{2.0, -1.0} == Vec2{5.0, 3.0},
                "Vec2 addition");
}

void testSimulationCase(Context& context) {
  const SimulationCase simulation = makeSimulationCase({50.0});
  context.check(simulation.frequencies().size() == 1U,
                "F2CPP case contains exactly one frequency");
  context.check(simulation.frequencies().designFrequency() == 50.0,
                "single frequency is the design frequency");
  context.check(simulation.environment().waterDepth() == 1000.0,
                "environment reports water depth in metres");
  context.check(simulation.receivers().rangeCount() == 3U,
                "receiver range count");
  context.check(simulation.launchFanPlan().launchAngleCount == 300U,
                "SimulationCase enforces D-02 instead of accepting 301");
  context.check(
      simulation.launchFanPlan().launchAngles.size() ==
          simulation.launchFanPlan().launchAngleCount,
      "SimulationCase stores the complete derived launch fan");

  context.expectThrows<ValidationError>(
      [] { static_cast<void>(makeSimulationCase({})); },
      "empty frequency list is rejected");
  context.expectThrows<ValidationError>(
      [] { static_cast<void>(makeSimulationCase({50.0, 100.0})); },
      "multiple frequencies are rejected by F2CPP");
  context.expectThrows<ValidationError>(
      [] { static_cast<void>(makeSimulationCase({0.0})); },
      "non-positive frequencies are rejected");
  context.expectThrows<ValidationError>(
      [] {
        static_cast<void>(
            ReceiverGrid(std::vector<double>{}, std::vector<double>{1.0}));
      },
      "empty receiver depth grid is rejected");
}

void testRayPathCache(Context& context) {
  RayPathCache cache;
  cache.reserve(1U);
  cache.append(makeRayPath());
  context.expectThrows<std::logic_error>(
      [&cache] { static_cast<void>(cache.at(0U)); },
      "unfrozen RayPathCache cannot be consumed");
  cache.freeze();

  context.check(cache.frozen(), "RayPathCache reports frozen state");
  context.check(cache.size() == 1U, "RayPathCache retains appended path");
  context.check(cache.at(0U).points.size() == 2U,
                "RayPathCache owns complete point state");
  context.check(cache.memoryFootprintBytes() >= sizeof(RayPathCache),
                "RayPathCache reports a non-trivial memory footprint");

  RayPathCache reflectedCache;
  reflectedCache.append(makeReflectedRayPath());
  reflectedCache.freeze();
  context.check(reflectedCache.at(0U).points.size() == 4U &&
                    reflectedCache.at(0U).steps.size() == 2U &&
                    reflectedCache.at(0U).events.size() == 1U,
                "double-point reflection mapping is accepted");

  context.expectThrows<std::logic_error>(
      [&cache] { cache.append(makeRayPath()); },
      "frozen RayPathCache rejects mutation");

  const double cachedTravelTime =
      cache.at(0U).points.back().realTravelTime;
  RayFrequencyState frequencyState{
      .frequency = 50.0,
      .points = std::vector<RayFrequencyPoint>(
          cache.at(0U).points.size(),
          RayFrequencyPoint{.complexTravelTime = {1.0, 2.0},
                            .amplitude = 0.5,
                            .reflectionPhase = 0.25,
                            .active = true})};
  frequencyState.points.back().amplitude = 0.25;
  context.check(
      cache.at(0U).points.back().realTravelTime == cachedTravelTime,
      "frequency-state mutation cannot change cached geometry");

  RayPath invalidEventPath = makeRayPath();
  invalidEventPath.events.push_back(
      {.rayPointIndex = 99U,
       .boundary = bellhop::ReflectionBoundary::SeaSurface,
       .boundarySegmentIndex = 0U,
       .position = {0.0, 0.0},
       .boundaryTangent = {1.0, 0.0},
       .outwardNormal = {0.0, -1.0},
       .incidentSlowness = {1.0 / 1500.0, -1.0 / 1500.0},
       .reflectedSlowness = {1.0 / 1500.0, 1.0 / 1500.0},
       .tangentSlowness = 1.0 / 1500.0,
       .normalSlowness = 1.0 / 1500.0});
  RayPathCache invalidEventCache;
  invalidEventCache.append(std::move(invalidEventPath));
  context.expectThrows<ValidationError>(
      [&invalidEventCache] { invalidEventCache.freeze(); },
      "reflection events must reference an existing ray point");

  RayPath invalidQuadraturePath = makeRayPath();
  invalidQuadraturePath.steps.front().midpointWeight = 9.0;
  RayPathCache invalidQuadratureCache;
  invalidQuadratureCache.append(std::move(invalidQuadraturePath));
  context.expectThrows<ValidationError>(
      [&invalidQuadratureCache] { invalidQuadratureCache.freeze(); },
      "quadrature weights must reproduce the actual step length");
}

void testFrequencyWorkspace(Context& context) {
  const ReceiverGrid receivers({10.0, 20.0}, {100.0, 200.0, 300.0});
  bellhop::FrequencyWorkspace workspace(50.0, receivers);
  context.check(workspace.pressure().size() == 6U,
                "workspace allocates depth-major pressure slice");
  workspace.at(1U, 2U) = std::complex<double>{3.0, -4.0};
  context.check(workspace.at(1U, 2U) == std::complex<double>{3.0, -4.0},
                "workspace range is contiguous inside each depth");
  workspace.clear();
  context.check(workspace.at(1U, 2U) == std::complex<double>{},
                "workspace clear resets pressure");
  context.expectThrows<std::out_of_range>(
      [&workspace] { static_cast<void>(workspace.at(2U, 0U)); },
      "workspace rejects invalid depth index");
  context.expectThrows<ValidationError>(
      [&receivers] {
        static_cast<void>(bellhop::FrequencyWorkspace(0.0, receivers));
      },
      "workspace rejects non-positive frequency");
}

}  // namespace

int main() {
  Context context;
  testVec2(context);
  testSimulationCase(context);
  testRayPathCache(context);
  testFrequencyWorkspace(context);

  if (context.failureCount() != 0) {
    std::cerr << context.failureCount() << " test assertion(s) failed\n";
    return 1;
  }
  std::cout << "All Bellhop F2CPP core type tests passed\n";
  return 0;
}
