#include <cmath>
#include <cstddef>
#include <iostream>
#include <string>

#include "bellhop/model/environment.hpp"
#include "bellhop/model/simulation_case.hpp"
#include "bellhop/ray/geometry_tracer.hpp"
#include "bellhop/ray/ray_path.hpp"
#include "support/test_harness.hpp"

namespace {

using bellhop::BoundaryModel;
using bellhop::Environment;
using bellhop::GeometryTracer;
using bellhop::IntegratorSettings;
using bellhop::RayPath;
using bellhop::RayState;
using bellhop::RayTerminationReason;
using bellhop::SoundSpeedPoint;
using bellhop::SoundSpeedProfile;
using bellhop::Source;
using bellhop::test::Context;

constexpr double kPi = 3.141592653589793238462643383279502884;
constexpr double kInterfaceDepth = 500.0;
constexpr double kNominalStep = 120.0;
constexpr double kMinimumStep = 1.0e-3 * kNominalStep;

Environment makeDownwardCrossingEnvironment() {
  return Environment(
      SoundSpeedProfile(
          {{.depth = 0.0, .soundSpeed = 1500.0, .density = 1000.0},
           {.depth = kInterfaceDepth,
            .soundSpeed = 1500.0,
            .density = 1000.0},
           {.depth = 1000.0,
            .soundSpeed = 1550.0,
            .density = 1000.0}}),
      BoundaryModel::vacuum(0.0), BoundaryModel::rigid(1000.0));
}

Environment makeUpwardCrossingEnvironment() {
  return Environment(
      SoundSpeedProfile(
          {{.depth = 0.0, .soundSpeed = 1450.0, .density = 1000.0},
           {.depth = kInterfaceDepth,
            .soundSpeed = 1500.0,
            .density = 1000.0},
           {.depth = 1000.0,
            .soundSpeed = 1500.0,
            .density = 1000.0}}),
      BoundaryModel::vacuum(0.0), BoundaryModel::rigid(1000.0));
}

IntegratorSettings makeSettings(std::size_t maximumRayPoints = 8U) {
  return IntegratorSettings{.stepLength = kNominalStep,
                            .rangeLimit = 10000.0,
                            .depthLimit = 2000.0,
                            .maximumRayPoints = maximumRayPoints};
}

bool finiteState(const RayState& state) {
  return bellhop::isFinite(state.position) &&
         bellhop::isFinite(state.slowness) &&
         std::isfinite(state.dynamicP[0]) &&
         std::isfinite(state.dynamicP[1]) &&
         std::isfinite(state.dynamicQ[0]) &&
         std::isfinite(state.dynamicQ[1]) &&
         std::isfinite(state.soundSpeed) &&
         std::isfinite(state.realTravelTime);
}

void checkPathInvariants(Context& context, const RayPath& path,
                         const char* direction) {
  context.check(path.points.size() == path.steps.size() + 1U,
                std::string(direction) +
                    " crossing retains the points/steps invariant");
  for (const RayState& state : path.points) {
    context.check(finiteState(state),
                  std::string(direction) +
                      " crossing retains finite ray states");
  }
  for (const auto& step : path.steps) {
    context.check(std::isfinite(step.stepLength) &&
                      std::isfinite(step.startWeight) &&
                      std::isfinite(step.midpointWeight) &&
                      bellhop::isFinite(step.midpoint),
                  std::string(direction) +
                      " crossing retains finite step quadrature");
    context.checkNear(
        step.startWeight + step.midpointWeight, step.stepLength, 1.0e-12,
        std::string(direction) + " quadrature weights sum to the step");
  }
}

std::size_t findAlignedNode(Context& context, const RayPath& path,
                            const char* direction) {
  std::size_t nodeIndex = path.points.size();
  std::size_t nodeCount = 0U;
  for (std::size_t index = 0U; index < path.points.size(); ++index) {
    if (std::abs(path.points[index].position.depth - kInterfaceDepth) <=
        1.0e-12) {
      nodeIndex = index;
      ++nodeCount;
    }
  }

  context.check(nodeCount == 1U,
                std::string(direction) +
                    " crossing stores the aligned SSP node exactly once");
  context.check(nodeIndex + 1U < path.points.size(),
                std::string(direction) +
                    " crossing continues beyond the aligned SSP node");
  return nodeIndex;
}

void checkHorizontalSlowness(Context& context, const RayPath& path,
                             const char* direction) {
  const double initialHorizontalSlowness =
      path.points.front().slowness.range;
  for (const RayState& state : path.points) {
    context.checkNear(
        state.slowness.range, initialHorizontalSlowness, 1.0e-15,
        std::string(direction) +
            " crossing preserves Snell horizontal slowness");
  }
}

void testDownwardInterfaceCrossing(Context& context) {
  const GeometryTracer tracer(makeDownwardCrossingEnvironment(),
                              makeSettings());
  const RayPath path =
      tracer.trace(Source{.depth = 450.0}, 30.0 * kPi / 180.0);

  context.check(path.terminationReason == RayTerminationReason::PointLimit,
                "downward SSP crossing continues to the point limit");
  checkPathInvariants(context, path, "downward");
  checkHorizontalSlowness(context, path, "downward");

  const std::size_t nodeIndex =
      findAlignedNode(context, path, "downward");
  if (nodeIndex + 1U < path.points.size()) {
    context.checkNear(path.steps[nodeIndex].stepLength, kMinimumStep,
                      1.0e-14,
                      "downward node departure uses one minimum forward step");
    context.check(path.points[nodeIndex + 1U].position.depth >
                      kInterfaceDepth,
                  "downward crossing updates the segment hint after the node");
  }
}

void testUpwardInterfaceCrossing(Context& context) {
  const GeometryTracer tracer(makeUpwardCrossingEnvironment(),
                              makeSettings());
  const RayPath path =
      tracer.trace(Source{.depth = 550.0}, -30.0 * kPi / 180.0);

  context.check(path.terminationReason == RayTerminationReason::PointLimit,
                "upward SSP crossing continues to the point limit");
  checkPathInvariants(context, path, "upward");
  checkHorizontalSlowness(context, path, "upward");

  const std::size_t nodeIndex = findAlignedNode(context, path, "upward");
  if (nodeIndex + 1U < path.points.size()) {
    context.checkNear(path.steps[nodeIndex].stepLength, kMinimumStep,
                      1.0e-14,
                      "upward node departure uses one minimum forward step");
    context.check(path.points[nodeIndex + 1U].position.depth <
                      kInterfaceDepth,
                  "upward crossing updates the segment hint after the node");
  }
}

void testSourceOnNodeMovesWithoutLooping(Context& context) {
  const GeometryTracer tracer(makeDownwardCrossingEnvironment(),
                              makeSettings(5U));
  const RayPath path =
      tracer.trace(Source{.depth = kInterfaceDepth},
                   30.0 * kPi / 180.0);

  context.check(path.terminationReason == RayTerminationReason::PointLimit,
                "source on an internal node does not fail numerically");
  context.checkNear(path.steps.front().stepLength, kMinimumStep, 1.0e-14,
                    "source on node receives the Fortran minimum step");
  context.check(path.points[1U].position.depth > kInterfaceDepth,
                "source on node moves into the departure segment");
  for (std::size_t index = 1U; index < path.points.size(); ++index) {
    context.check(path.points[index].position.depth >
                      path.points[index - 1U].position.depth,
                  "node-started ray makes strictly positive depth progress");
  }
  checkPathInvariants(context, path, "node-started");
}

void testSeaBoundariesReflect(Context& context) {
  const Environment environment(
      SoundSpeedProfile(
          {{.depth = 0.0, .soundSpeed = 1500.0, .density = 1000.0},
           {.depth = kInterfaceDepth,
            .soundSpeed = 1500.0,
            .density = 1000.0},
           {.depth = 1000.0,
            .soundSpeed = 1500.0,
            .density = 1000.0}}),
      BoundaryModel::vacuum(0.0), BoundaryModel::rigid(1000.0));
  const GeometryTracer tracer(environment, makeSettings());

  const RayPath surface =
      tracer.trace(Source{.depth = 50.0}, -0.5 * kPi);
  context.check(
      surface.terminationReason == RayTerminationReason::PointLimit,
      "sea-surface reflection continues to the point limit");
  context.check(!surface.events.empty() &&
                    surface.events.front().boundary ==
                        bellhop::ReflectionBoundary::SeaSurface,
                "sea-surface encounter emits a surface event");
  context.check(surface.points.size() ==
                    surface.steps.size() + surface.events.size() + 1U,
                "surface reflection preserves P = 1 + S + E");

  const RayPath seabed =
      tracer.trace(Source{.depth = 950.0}, 0.5 * kPi);
  context.check(
      seabed.terminationReason == RayTerminationReason::PointLimit,
      "seabed reflection continues to the point limit");
  context.check(!seabed.events.empty() &&
                    seabed.events.front().boundary ==
                        bellhop::ReflectionBoundary::Seabed,
                "seabed encounter emits a bottom event");
  context.check(seabed.points.size() ==
                    seabed.steps.size() + seabed.events.size() + 1U,
                "seabed reflection preserves P = 1 + S + E");
}

}  // namespace

int main() {
  Context context;
  testDownwardInterfaceCrossing(context);
  testUpwardInterfaceCrossing(context);
  testSourceOnNodeMovesWithoutLooping(context);
  testSeaBoundariesReflect(context);

  if (context.failureCount() != 0) {
    std::cerr << context.failureCount()
              << " SSP-interface geometry-tracer assertion(s) failed\n";
    return 1;
  }

  std::cout
      << "All Bellhop F2CPP SSP-interface geometry-tracer tests passed\n";
  return 0;
}
