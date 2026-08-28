#include <cmath>
#include <cstddef>
#include <iostream>
#include <string>

#include "rayreuse/model/environment.hpp"
#include "rayreuse/model/n2_linear_ssp.hpp"
#include "rayreuse/model/simulation_case.hpp"
#include "rayreuse/model/sound_speed_types.hpp"
#include "rayreuse/ray/flat_boundary_reflection.hpp"
#include "rayreuse/ray/geometry_tracer.hpp"
#include "rayreuse/ray/ray_path.hpp"
#include "support/test_harness.hpp"

namespace {

using rayreuse::BoundaryCurvatureMode;
using rayreuse::BoundaryModel;
using rayreuse::Environment;
using rayreuse::FlatBoundaryGeometry;
using rayreuse::GeometryTracer;
using rayreuse::IntegratorSettings;
using rayreuse::N2LinearSsp;
using rayreuse::RayPath;
using rayreuse::RayState;
using rayreuse::RayTerminationReason;
using rayreuse::SoundSpeedPoint;
using rayreuse::SoundSpeedProfile;
using rayreuse::Source;
using rayreuse::SspInterpolationKind;
using rayreuse::reflectAtFlatBoundary;
using rayreuse::test::Context;

constexpr double kPi = 3.141592653589793238462643383279502884;
constexpr double kInterfaceDepth = 500.0;
constexpr double kNominalStep = 120.0;
constexpr double kMinimumStep = 1.0e-3 * kNominalStep;

Environment makeDownwardCrossingEnvironment(
    SspInterpolationKind kind = SspInterpolationKind::CLinear) {
  return Environment(
      SoundSpeedProfile(
          {{.depth = 0.0, .soundSpeed = 1500.0, .density = 1000.0},
           {.depth = kInterfaceDepth, .soundSpeed = 1500.0, .density = 1000.0},
           {.depth = 1000.0, .soundSpeed = 1550.0, .density = 1000.0}},
          kind),
      BoundaryModel::vacuum(0.0), BoundaryModel::rigid(1000.0));
}

Environment makeUpwardCrossingEnvironment(
    SspInterpolationKind kind = SspInterpolationKind::CLinear) {
  return Environment(
      SoundSpeedProfile(
          {{.depth = 0.0, .soundSpeed = 1450.0, .density = 1000.0},
           {.depth = kInterfaceDepth, .soundSpeed = 1500.0, .density = 1000.0},
           {.depth = 1000.0, .soundSpeed = 1500.0, .density = 1000.0}},
          kind),
      BoundaryModel::vacuum(0.0), BoundaryModel::rigid(1000.0));
}

// Non-constant speeds in both segments so N² sampling is exercised on the way
// to each boundary; the seabed coincides with the last profile node.
Environment makeReflectingEnvironment(
    SspInterpolationKind kind = SspInterpolationKind::CLinear) {
  return Environment(
      SoundSpeedProfile(
          {{.depth = 0.0, .soundSpeed = 1500.0, .density = 1000.0},
           {.depth = kInterfaceDepth, .soundSpeed = 1520.0, .density = 1000.0},
           {.depth = 1000.0, .soundSpeed = 1500.0, .density = 1000.0}},
          kind),
      BoundaryModel::vacuum(0.0), BoundaryModel::rigid(1000.0));
}

IntegratorSettings makeSettings(std::size_t maximumRayPoints = 8U) {
  return IntegratorSettings{.stepLength = kNominalStep,
                            .rangeLimit = 10000.0,
                            .depthLimit = 2000.0,
                            .maximumRayPoints = maximumRayPoints};
}

bool finiteState(const RayState& state) {
  return rayreuse::isFinite(state.position) &&
         rayreuse::isFinite(state.slowness) &&
         std::isfinite(state.dynamicP[0]) && std::isfinite(state.dynamicP[1]) &&
         std::isfinite(state.dynamicQ[0]) && std::isfinite(state.dynamicQ[1]) &&
         std::isfinite(state.soundSpeed) && std::isfinite(state.realTravelTime);
}

void checkPathInvariants(Context& context, const RayPath& path,
                         const char* direction) {
  context.check(
      path.points.size() == path.steps.size() + 1U,
      std::string(direction) + " crossing retains the points/steps invariant");
  for (const RayState& state : path.points) {
    context.check(
        finiteState(state),
        std::string(direction) + " crossing retains finite ray states");
  }
  for (const auto& step : path.steps) {
    context.check(
        std::isfinite(step.stepLength) && std::isfinite(step.startWeight) &&
            std::isfinite(step.midpointWeight) &&
            rayreuse::isFinite(step.midpoint),
        std::string(direction) + " crossing retains finite step quadrature");
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
  const double initialHorizontalSlowness = path.points.front().slowness.range;
  for (const RayState& state : path.points) {
    context.checkNear(state.slowness.range, initialHorizontalSlowness, 1.0e-15,
                      std::string(direction) +
                          " crossing preserves Snell horizontal slowness");
  }
}

void testDownwardInterfaceCrossing(Context& context) {
  const GeometryTracer tracer(makeDownwardCrossingEnvironment(),
                              makeSettings());
  const RayPath path = tracer.trace(Source{.depth = 450.0}, 30.0 * kPi / 180.0);

  context.check(path.terminationReason == RayTerminationReason::PointLimit,
                "downward SSP crossing continues to the point limit");
  checkPathInvariants(context, path, "downward");
  checkHorizontalSlowness(context, path, "downward");

  const std::size_t nodeIndex = findAlignedNode(context, path, "downward");
  if (nodeIndex + 1U < path.points.size()) {
    context.checkNear(path.steps[nodeIndex].stepLength, kMinimumStep, 1.0e-14,
                      "downward node departure uses one minimum forward step");
    context.check(path.points[nodeIndex + 1U].position.depth > kInterfaceDepth,
                  "downward crossing updates the segment hint after the node");
  }
}

void testUpwardInterfaceCrossing(Context& context) {
  const GeometryTracer tracer(makeUpwardCrossingEnvironment(), makeSettings());
  const RayPath path =
      tracer.trace(Source{.depth = 550.0}, -30.0 * kPi / 180.0);

  context.check(path.terminationReason == RayTerminationReason::PointLimit,
                "upward SSP crossing continues to the point limit");
  checkPathInvariants(context, path, "upward");
  checkHorizontalSlowness(context, path, "upward");

  const std::size_t nodeIndex = findAlignedNode(context, path, "upward");
  if (nodeIndex + 1U < path.points.size()) {
    context.checkNear(path.steps[nodeIndex].stepLength, kMinimumStep, 1.0e-14,
                      "upward node departure uses one minimum forward step");
    context.check(path.points[nodeIndex + 1U].position.depth < kInterfaceDepth,
                  "upward crossing updates the segment hint after the node");
  }
}

void testSourceOnNodeMovesWithoutLooping(Context& context) {
  const GeometryTracer tracer(makeDownwardCrossingEnvironment(),
                              makeSettings(5U));
  const RayPath path =
      tracer.trace(Source{.depth = kInterfaceDepth}, 30.0 * kPi / 180.0);

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
           {.depth = kInterfaceDepth, .soundSpeed = 1500.0, .density = 1000.0},
           {.depth = 1000.0, .soundSpeed = 1500.0, .density = 1000.0}}),
      BoundaryModel::vacuum(0.0), BoundaryModel::rigid(1000.0));
  const GeometryTracer tracer(environment, makeSettings());

  const RayPath surface = tracer.trace(Source{.depth = 50.0}, -0.5 * kPi);
  context.check(surface.terminationReason == RayTerminationReason::PointLimit,
                "sea-surface reflection continues to the point limit");
  context.check(
      !surface.events.empty() && surface.events.front().boundary ==
                                     rayreuse::ReflectionBoundary::SeaSurface,
      "sea-surface encounter emits a surface event");
  context.check(surface.points.size() ==
                    surface.steps.size() + surface.events.size() + 1U,
                "surface reflection preserves P = 1 + S + E");

  const RayPath seabed = tracer.trace(Source{.depth = 950.0}, 0.5 * kPi);
  context.check(seabed.terminationReason == RayTerminationReason::PointLimit,
                "seabed reflection continues to the point limit");
  context.check(
      !seabed.events.empty() && seabed.events.front().boundary ==
                                    rayreuse::ReflectionBoundary::Seabed,
      "seabed encounter emits a bottom event");
  context.check(
      seabed.points.size() == seabed.steps.size() + seabed.events.size() + 1U,
      "seabed reflection preserves P = 1 + S + E");
}

// N²-linear mirror of the downward crossing: the depth-node reduction,
// arrival-side hint, and minimum forward step must behave exactly as they do
// for C-linear, while the nonzero curvature and node jump feed different
// dynamic states.
void testN2DownwardInterfaceCrossing(Context& context) {
  const GeometryTracer tracer(
      makeDownwardCrossingEnvironment(SspInterpolationKind::N2Linear),
      makeSettings());
  const RayPath path = tracer.trace(Source{.depth = 450.0}, 30.0 * kPi / 180.0);

  context.check(path.terminationReason == RayTerminationReason::PointLimit,
                "N2 downward SSP crossing continues to the point limit");
  checkPathInvariants(context, path, "N2 downward");
  checkHorizontalSlowness(context, path, "N2 downward");

  const std::size_t nodeIndex = findAlignedNode(context, path, "N2 downward");
  if (nodeIndex + 1U < path.points.size()) {
    context.checkNear(path.steps[nodeIndex].stepLength, kMinimumStep, 1.0e-14,
                      "N2 downward node departure uses one minimum forward "
                      "step");
    context.check(
        path.points[nodeIndex + 1U].position.depth > kInterfaceDepth,
        "N2 downward crossing updates the segment hint after the node");
  }
}

void testN2UpwardInterfaceCrossing(Context& context) {
  const GeometryTracer tracer(
      makeUpwardCrossingEnvironment(SspInterpolationKind::N2Linear),
      makeSettings());
  const RayPath path =
      tracer.trace(Source{.depth = 550.0}, -30.0 * kPi / 180.0);

  context.check(path.terminationReason == RayTerminationReason::PointLimit,
                "N2 upward SSP crossing continues to the point limit");
  checkPathInvariants(context, path, "N2 upward");
  checkHorizontalSlowness(context, path, "N2 upward");

  const std::size_t nodeIndex = findAlignedNode(context, path, "N2 upward");
  if (nodeIndex + 1U < path.points.size()) {
    context.checkNear(path.steps[nodeIndex].stepLength, kMinimumStep, 1.0e-14,
                      "N2 upward node departure uses one minimum forward "
                      "step");
    context.check(
        path.points[nodeIndex + 1U].position.depth < kInterfaceDepth,
        "N2 upward crossing updates the segment hint after the node");
  }
}

void testN2SourceOnNodeMovesWithoutLooping(Context& context) {
  const GeometryTracer tracer(
      makeDownwardCrossingEnvironment(SspInterpolationKind::N2Linear),
      makeSettings(5U));
  const RayPath path =
      tracer.trace(Source{.depth = kInterfaceDepth}, 30.0 * kPi / 180.0);

  context.check(path.terminationReason == RayTerminationReason::PointLimit,
                "N2 source on an internal node does not fail numerically");
  context.checkNear(path.steps.front().stepLength, kMinimumStep, 1.0e-14,
                    "N2 source on node receives the Fortran minimum step");
  context.check(path.points[1U].position.depth > kInterfaceDepth,
                "N2 source on node moves into the departure segment");
  for (std::size_t index = 1U; index < path.points.size(); ++index) {
    context.check(path.points[index].position.depth >
                      path.points[index - 1U].position.depth,
                  "N2 node-started ray makes strictly positive depth "
                  "progress");
  }
  checkPathInvariants(context, path, "N2 node-started");
}

// Reflects a state through the public flat-boundary formula using the
// N² water sample of the arrival-side segment (hinted evaluate, exactly like
// the tracer's reflection water sample, which tolerates a last-ulp position
// outside the segment), so the tracer's reflected dynamic variables prove
// which SSP gradient the reflection consumed.
void checkN2ReflectionAgainstArrivalSideWaterSample(
    Context& context, const RayPath& path, const N2LinearSsp& ssp,
    std::size_t arrivalSegment, double outwardNormalDepth, const char* label) {
  if (path.events.empty()) {
    context.check(false, std::string(label) + " path contains a reflection");
    return;
  }
  const rayreuse::ReflectionEvent& event = path.events.front();
  const rayreuse::SoundSpeedSample arrivalWater =
      ssp.evaluate(event.position, arrivalSegment);
  context.check(
      std::abs(arrivalWater.soundSpeedGradient.depth) > 1.0e-9,
      std::string(label) +
          " arrival-side N2 water sample carries a nonzero gradient");
  const auto expected = reflectAtFlatBoundary(
      path.points[event.rayPointIndex], event.boundary,
      FlatBoundaryGeometry{.point = event.position,
                           .tangent = rayreuse::Vec2{1.0, 0.0},
                           .outwardNormal = rayreuse::Vec2{0.0, outwardNormalDepth},
                           .soundSpeedGradient = arrivalWater.soundSpeedGradient,
                           .segmentIndex = 0U,
                           .curvature = 0.0,
                           .maximumIncidentPlaneDistance = 1.0e-3 * kNominalStep},
      event.rayPointIndex, BoundaryCurvatureMode::Standard);
  context.checkNear(
      path.points[event.reflectedRayPointIndex].dynamicP[0],
      expected.reflectedState.dynamicP[0], 1.0e-14,
      std::string(label) + " reflection uses the arrival-side N2 water sample");
  context.checkNear(
      path.points[event.reflectedRayPointIndex].dynamicP[1],
      expected.reflectedState.dynamicP[1], 1.0e-14,
      std::string(label) + " reflection jump applies to both dynamic bases");
}

// Event-carrying paths satisfy P = 1 + S + E rather than P = 1 + S; only the
// finite-state part of the shared invariants applies to them.
void checkFiniteStates(Context& context, const RayPath& path,
                       const char* direction) {
  for (const RayState& state : path.points) {
    context.check(
        finiteState(state),
        std::string(direction) + " reflection retains finite ray states");
  }
  for (const auto& step : path.steps) {
    context.check(
        std::isfinite(step.stepLength) && std::isfinite(step.startWeight) &&
            std::isfinite(step.midpointWeight) &&
            rayreuse::isFinite(step.midpoint),
        std::string(direction) + " reflection retains finite step quadrature");
  }
}

void testN2SeaBoundariesReflect(Context& context) {
  const Environment environment =
      makeReflectingEnvironment(SspInterpolationKind::N2Linear);
  const N2LinearSsp ssp(environment.soundSpeedProfile());
  const GeometryTracer tracer(environment, makeSettings());

  const RayPath surface = tracer.trace(Source{.depth = 50.0}, -0.5 * kPi);
  context.check(surface.terminationReason == RayTerminationReason::PointLimit,
                "N2 sea-surface reflection continues to the point limit");
  context.check(
      !surface.events.empty() && surface.events.front().boundary ==
                                     rayreuse::ReflectionBoundary::SeaSurface,
      "N2 sea-surface encounter emits a surface event");
  context.check(surface.points.size() ==
                    surface.steps.size() + surface.events.size() + 1U,
                "N2 surface reflection preserves P = 1 + S + E");
  checkFiniteStates(context, surface, "N2 surface");
  checkN2ReflectionAgainstArrivalSideWaterSample(
      context, surface, ssp, 0U, -1.0, "N2 surface");

  const RayPath seabed = tracer.trace(Source{.depth = 950.0}, 0.5 * kPi);
  context.check(seabed.terminationReason == RayTerminationReason::PointLimit,
                "N2 seabed reflection continues to the point limit");
  context.check(
      !seabed.events.empty() && seabed.events.front().boundary ==
                                    rayreuse::ReflectionBoundary::Seabed,
      "N2 seabed encounter emits a bottom event");
  context.check(
      seabed.points.size() == seabed.steps.size() + seabed.events.size() + 1U,
      "N2 seabed reflection preserves P = 1 + S + E");
  checkFiniteStates(context, seabed, "N2 seabed");
  // The seabed plane coincides with the last profile node: the water sample
  // must come from the arrival-side segment above it.
  checkN2ReflectionAgainstArrivalSideWaterSample(
      context, seabed, ssp, 1U, 1.0, "N2 seabed");

  // A longer vertical N² ray collects several reflections and node
  // crossings; event ordering and the point/step/event accounting must hold.
  const RayPath multi =
      GeometryTracer(environment, makeSettings(20U))
          .trace(Source{.depth = 50.0}, -0.5 * kPi);
  context.check(multi.events.size() >= 2U,
                "N2 vertical ray accumulates multiple reflections");
  context.check(
      multi.points.size() == multi.steps.size() + multi.events.size() + 1U,
      "N2 multi-reflection path preserves P = 1 + S + E");
  for (std::size_t index = 1U; index < multi.events.size(); ++index) {
    context.check(multi.events[index].rayPointIndex >
                      multi.events[index - 1U].rayPointIndex,
                  "N2 reflection events stay strictly ordered");
  }
  checkFiniteStates(context, multi, "N2 multi");
}

}  // namespace

int main() {
  Context context;
  testDownwardInterfaceCrossing(context);
  testUpwardInterfaceCrossing(context);
  testSourceOnNodeMovesWithoutLooping(context);
  testSeaBoundariesReflect(context);
  testN2DownwardInterfaceCrossing(context);
  testN2UpwardInterfaceCrossing(context);
  testN2SourceOnNodeMovesWithoutLooping(context);
  testN2SeaBoundariesReflect(context);

  if (context.failureCount() != 0) {
    std::cerr << context.failureCount()
              << " SSP-interface geometry-tracer assertion(s) failed\n";
    return 1;
  }

  std::cout
      << "All Bellhop RayReuse SSP-interface geometry-tracer tests passed\n";
  return 0;
}
