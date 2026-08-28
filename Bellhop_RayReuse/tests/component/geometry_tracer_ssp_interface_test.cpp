#include <cmath>
#include <cstddef>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "rayreuse/model/cubic_spline_ssp.hpp"
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
using rayreuse::CubicSplineSsp;
using rayreuse::Environment;
using rayreuse::FlatBoundaryGeometry;
using rayreuse::GeometryTracer;
using rayreuse::IntegratorSettings;
using rayreuse::N2LinearSsp;
using rayreuse::QuadrilateralSspGrid;
using rayreuse::RayPath;
using rayreuse::RayState;
using rayreuse::RayTerminationReason;
using rayreuse::SoundSpeedPoint;
using rayreuse::SoundSpeedProfile;
using rayreuse::Source;
using rayreuse::SspGradientContinuity;
using rayreuse::SspInterpolationKind;
using rayreuse::Vec2;
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

// The cubic-spline backend must already present the exact evaluator contract
// the tracer consumes through GeometrySspEvaluator: ContinuousAtNodes
// gradient handling (the shared PCHIP-style node rule, no C/N² jump), the
// arrival-side segment hint at an aligned node, and edge-segment selection
// outside the profile. SspInterpolationKind::CubicSpline and the variant
// backend are G01, so the full spline crossing/reflection runs (mirroring the
// N2 tests above) are added once the tracer can construct the backend.
void testSplineSspInterfaceContract(Context& context) {
  const CubicSplineSsp spline(
      SoundSpeedProfile(
          {{.depth = 0.0, .soundSpeed = 1500.0, .density = 1000.0},
           {.depth = kInterfaceDepth, .soundSpeed = 1500.0, .density = 1000.0},
           {.depth = 1000.0, .soundSpeed = 1550.0, .density = 1000.0}}));
  context.check(spline.gradientContinuity() ==
                    SspGradientContinuity::ContinuousAtNodes,
                "spline exposes the ContinuousAtNodes tracer contract");
  context.check(spline.segmentCount() == 2U,
                "spline segment count matches the depth-node topology");

  const Vec2 node{.range = 0.0, .depth = kInterfaceDepth};
  const auto arrivalFromBelow = spline.evaluate(node, 0U);
  const auto arrivalFromAbove = spline.evaluate(node, 1U);
  context.check(arrivalFromBelow.segmentIndex == 0U &&
                    arrivalFromAbove.segmentIndex == 1U,
                "spline keeps the arrival-side segment at an aligned node");
  context.checkNear(arrivalFromBelow.soundSpeed,
                    arrivalFromAbove.soundSpeed, 1.0e-9,
                    "spline value is continuous across the tracer node");
  context.checkNear(arrivalFromBelow.soundSpeedGradient.depth,
                    arrivalFromAbove.soundSpeedGradient.depth, 1.0e-9,
                    "spline gradient is continuous across the tracer node");

  const auto above = spline.evaluate(
      Vec2{.range = 0.0, .depth = -10.0}, 1U);
  const auto below = spline.evaluate(
      Vec2{.range = 0.0, .depth = 1010.0}, 0U);
  context.check(above.segmentIndex == 0U && below.segmentIndex == 1U,
                "spline extrapolation selects edge segments for the tracer");
}

// ---------------------------------------------------------------------------
// Quadrilateral (Q) range-dependent fixtures
// ---------------------------------------------------------------------------

constexpr double kQStep = 50.0;
constexpr double kQMinimumStep = 1.0e-3 * kQStep;
constexpr double kQRangeNode = 350.0;
constexpr double kQGridBack = 800.0;

// The shared cross-gradient standard-case grid: depths [0, 100] m, ranges
// [0, 350, 800] m, speeds [[1500, 1540, 1580], [1500, 1520, 1540]]. The
// water column ends at 100 m; a 5-degree ray reaching the 400 m box edge
// stays near 85 m depth, so it crosses the 350 m node without reflecting.
Environment makeQCrossGradientEnvironment() {
  return Environment(
      SoundSpeedProfile(
          {{.depth = 0.0, .soundSpeed = 1490.0, .density = 1000.0},
           {.depth = 100.0, .soundSpeed = 1490.0, .density = 1100.0}},
          SspInterpolationKind::Quadrilateral,
          std::make_shared<const QuadrilateralSspGrid>(QuadrilateralSspGrid{
              .rangesMeters = {0.0, 350.0, 800.0},
              .speedsDepthMajor = {1500.0, 1540.0, 1580.0,
                                   1500.0, 1520.0, 1540.0},
              .depthCount = 2U,
              .rangeCount = 3U})),
      BoundaryModel::vacuum(0.0), BoundaryModel::rigid(100.0));
}

// Uniform 1500 m/s matrix over depths [0, 200] m and ranges [0, 350, 800] m.
// A horizontal launch from depth 100 keeps tangent (1, 0) exactly
// (1500 * fl(1/1500) rounds to 1.0), so a 50 m step lands on every multiple of
// 50—including the range nodes 350 and 800—without rounding, which makes grid
// line ownership and step clamping deterministic.
Environment makeQUniformEnvironment() {
  return Environment(
      SoundSpeedProfile(
          {{.depth = 0.0, .soundSpeed = 1500.0, .density = 1000.0},
           {.depth = 200.0, .soundSpeed = 1500.0, .density = 1100.0}},
          SspInterpolationKind::Quadrilateral,
          std::make_shared<const QuadrilateralSspGrid>(QuadrilateralSspGrid{
              .rangesMeters = {0.0, 350.0, 800.0},
              .speedsDepthMajor = {1500.0, 1500.0, 1500.0,
                                   1500.0, 1500.0, 1500.0},
              .depthCount = 2U,
              .rangeCount = 3U})),
      BoundaryModel::vacuum(0.0), BoundaryModel::rigid(200.0));
}

// One internal depth node at 100 m (depth slopes 0 above and 0.5 below) in a
// single range cell, so the depth-node machinery of the limiter runs under the
// Q evaluator with no range-grid interaction.
Environment makeQDepthNodeEnvironment() {
  return Environment(
      SoundSpeedProfile(
          {{.depth = 0.0, .soundSpeed = 1500.0, .density = 1000.0},
           {.depth = 100.0, .soundSpeed = 1500.0, .density = 1000.0},
           {.depth = 200.0, .soundSpeed = 1550.0, .density = 1000.0}},
          SspInterpolationKind::Quadrilateral,
          std::make_shared<const QuadrilateralSspGrid>(QuadrilateralSspGrid{
              .rangesMeters = {0.0, 1000.0},
              .speedsDepthMajor = {1500.0, 1500.0,
                                   1500.0, 1500.0,
                                   1550.0, 1550.0},
              .depthCount = 3U,
              .rangeCount = 2U})),
      BoundaryModel::vacuum(0.0), BoundaryModel::rigid(200.0));
}

IntegratorSettings makeQSettings(std::size_t maximumRayPoints,
                                 double rangeLimit) {
  return IntegratorSettings{.stepLength = kQStep,
                            .rangeLimit = rangeLimit,
                            .depthLimit = 2000.0,
                            .maximumRayPoints = maximumRayPoints};
}

void checkAllStepsAtOrAboveMinimum(Context& context, const RayPath& path,
                                   const char* label) {
  for (std::size_t index = 0; index < path.steps.size(); ++index) {
    context.check(
        path.steps[index].stepLength >= kQMinimumStep * (1.0 - 1.0e-9),
        std::string(label) + " step " + std::to_string(index) +
            " stays at or above the 1e-3 * nominal minimum clamp");
  }
}

// A shallow ray over the cross-gradient grid crosses the 350 m range node: the
// SSP range interval enters the limiter's min/max reduction, the trial lands
// on the grid line (within the blend rounding of a bent ray), and the trace
// continues past it without stalling. The bilinear anchor c(350, z) =
// 1540 - 0.2 z holds from either side of the node.
void testQCrossRangeBoundaryLandsOnGridLine(Context& context) {
  const GeometryTracer tracer(makeQCrossGradientEnvironment(),
                              makeQSettings(64U, 400.0));
  const RayPath path = tracer.trace(Source{.depth = 50.0}, 5.0 * kPi / 180.0);

  context.check(path.terminationReason == RayTerminationReason::ExitedDomain,
                "Q range-boundary trace exits the spatial box");
  context.check(path.points.size() == path.steps.size() + 1U,
                "Q range-boundary trace retains the points/steps invariant");

  std::size_t alignedCount = 0U;
  std::size_t alignedIndex = path.points.size();
  std::size_t nearCount = 0U;
  for (std::size_t index = 0; index < path.points.size(); ++index) {
    const double range = path.points[index].position.range;
    if (std::abs(range - kQRangeNode) <= 1.0e-4) {
      ++alignedCount;
      alignedIndex = index;
      context.checkNear(path.points[index].soundSpeed,
                        1540.0 - 0.2 * path.points[index].position.depth,
                        1.0e-3,
                        "the aligned grid-line point samples the node bilinear "
                        "value");
    }
    if (std::abs(range - kQRangeNode) <= 0.1) {
      ++nearCount;
    }
  }
  context.check(alignedCount == 1U && nearCount <= 2U,
                "the reduced trial lands on the 350 m grid line exactly once");
  context.check(alignedIndex + 2U < path.points.size(),
                "the trace continues beyond the aligned grid line");
  // The blended landing of a bent ray stays a fraction of a millimetre left
  // of the node, so the departing trial crosses the line again and ReduceStep
  // replaces it with the Fortran minimum forward step.
  context.checkNear(path.steps[alignedIndex].stepLength, kQMinimumStep, 1.0e-12,
                    "the grid-line departure is clamped to the minimum step");

  for (std::size_t index = 1; index < path.points.size(); ++index) {
    context.check(path.points[index].position.range >
                      path.points[index - 1U].position.range,
                  "Q range crossing keeps strictly increasing range");
  }
  checkAllStepsAtOrAboveMinimum(context, path, "Q range crossing");
}

// The deterministic horizontal variant: the aligned 350 m point appears
// exactly once, the step leaving it is the full nominal step (the exact node
// belongs to the right-hand cell, so no clamp fires at an internal node), the
// trace marches across the grid line without zero-step oscillation, and the
// final box-exit step is exactly the 1e-3 * nominal minimum clamp.
void testQHorizontalGridLineProgression(Context& context) {
  const GeometryTracer tracer(makeQUniformEnvironment(),
                              makeQSettings(32U, 700.0));
  const RayPath path = tracer.trace(Source{.depth = 100.0}, 0.0);

  context.check(path.terminationReason == RayTerminationReason::ExitedDomain,
                "the horizontal Q trace exits the spatial box");
  context.check(path.points.size() == path.steps.size() + 1U,
                "the horizontal Q trace retains the points/steps invariant");

  std::size_t alignedCount = 0U;
  std::size_t alignedIndex = path.points.size();
  for (std::size_t index = 0; index < path.points.size(); ++index) {
    if (std::abs(path.points[index].position.range - kQRangeNode) <= 1.0e-12) {
      ++alignedCount;
      alignedIndex = index;
    }
  }
  context.check(alignedCount == 1U,
                "the exact horizontal landing stores the 350 m node once");
  context.check(alignedIndex == 7U,
                "the aligned node is the eighth point of the 50 m march");
  context.checkNear(path.steps[alignedIndex].stepLength, kQStep, 0.0,
                    "leaving the exact range node uses the full nominal step "
                    "(right-cell ownership, no clamp)");

  for (std::size_t index = 1; index < path.points.size(); ++index) {
    context.check(path.points[index].position.range >
                      path.points[index - 1U].position.range,
                  "grid-line march makes strictly positive range progress");
  }
  for (std::size_t index = 0; index + 1U < path.steps.size(); ++index) {
    context.checkNear(path.steps[index].stepLength, kQStep, 0.0,
                      "grid-line march keeps the nominal step throughout");
  }
  context.checkNear(path.steps.back().stepLength, kQMinimumStep, 1.0e-15,
                    "the box-exit departure clamps to exactly the 1e-3 "
                    "minimum step");
  checkAllStepsAtOrAboveMinimum(context, path, "Q horizontal march");
}

// The minimum-step clamp must stay in force under the Q evaluator at an SSP
// depth node: the aligned 100 m node receives one Fortran minimum forward
// step on departure, exactly as the range-independent backends do.
void testQMinimumStepClampRetainedAtDepthNode(Context& context) {
  const GeometryTracer tracer(makeQDepthNodeEnvironment(),
                              makeQSettings(8U, 10000.0));
  const RayPath path = tracer.trace(Source{.depth = 90.0}, 30.0 * kPi / 180.0);

  context.check(path.terminationReason == RayTerminationReason::PointLimit,
                "Q depth-node trace continues to the point limit");

  std::size_t alignedCount = 0U;
  std::size_t alignedIndex = path.points.size();
  for (std::size_t index = 0; index < path.points.size(); ++index) {
    if (std::abs(path.points[index].position.depth - 100.0) <= 1.0e-12) {
      ++alignedCount;
      alignedIndex = index;
    }
  }
  context.check(alignedCount == 1U,
                "the aligned 100 m depth node is stored exactly once");
  context.check(alignedIndex + 1U < path.points.size(),
                "the depth-node trace continues past the aligned node");
  context.checkNear(path.steps[alignedIndex].stepLength, kQMinimumStep, 1.0e-14,
                    "Q depth-node departure uses one minimum forward step");
  context.check(path.points[alignedIndex + 1U].position.depth > 100.0,
                "Q depth crossing updates the segment hint after the node");
  checkAllStepsAtOrAboveMinimum(context, path, "Q depth node");
}

// With the spatial box beyond the grid back, the trace reaches the 800 m back
// node exactly, and the next clamped minimum step already samples past the
// grid in its midpoint, which the Q evaluator rejects: the tracer must report
// NumericalFailure rather than loop at the back line.
void testQBackNodeRejectedBeyondGrid(Context& context) {
  const GeometryTracer tracer(makeQUniformEnvironment(),
                              makeQSettings(32U, 10000.0));
  const RayPath path = tracer.trace(Source{.depth = 100.0}, 0.0);

  context.check(path.terminationReason == RayTerminationReason::NumericalFailure,
                "stepping past the grid back terminates with a numerical "
                "failure");
  context.checkNear(path.points.back().position.range, kQGridBack, 1.0e-12,
                    "the last completed point sits on the 800 m back node");
  for (const auto& step : path.steps) {
    context.checkNear(step.stepLength, kQStep, 0.0,
                      "every completed back-node step is the nominal march");
  }
  checkAllStepsAtOrAboveMinimum(context, path, "Q back node");
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
  testSplineSspInterfaceContract(context);
  testQCrossRangeBoundaryLandsOnGridLine(context);
  testQHorizontalGridLineProgression(context);
  testQMinimumStepClampRetainedAtDepthNode(context);
  testQBackNodeRejectedBeyondGrid(context);

  if (context.failureCount() != 0) {
    std::cerr << context.failureCount()
              << " SSP-interface geometry-tracer assertion(s) failed\n";
    return 1;
  }

  std::cout
      << "All Bellhop RayReuse SSP-interface geometry-tracer tests passed\n";
  return 0;
}
