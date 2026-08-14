#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <string>
#include <vector>

#include "bellhop/cache/ray_path_cache.hpp"
#include "bellhop/model/environment.hpp"
#include "bellhop/model/simulation_case.hpp"
#include "bellhop/ray/flat_boundary_reflection.hpp"
#include "bellhop/ray/geometry_tracer.hpp"
#include "support/test_harness.hpp"

namespace {

using bellhop::BoundaryCurvatureMode;
using bellhop::BoundaryGeometry;
using bellhop::BoundaryModel;
using bellhop::BoundaryOrientation;
using bellhop::Environment;
using bellhop::FlatBoundaryGeometry;
using bellhop::GeometryTracer;
using bellhop::IntegratorSettings;
using bellhop::RayPath;
using bellhop::RayPathCache;
using bellhop::RayTerminationReason;
using bellhop::ReflectionBoundary;
using bellhop::SoundSpeedPoint;
using bellhop::SoundSpeedProfile;
using bellhop::Source;
using bellhop::Vec2;
using bellhop::test::Context;

constexpr double kPi = 3.141592653589793238462643383279502884;
constexpr double kSoundSpeed = 1500.0;

Environment makeConstantEnvironment(double surfaceDepth = 0.0,
                                    double seabedDepth = 100.0) {
  return Environment(
      SoundSpeedProfile(
          {{.depth = surfaceDepth,
            .soundSpeed = kSoundSpeed,
            .density = 1000.0},
           {.depth = seabedDepth,
            .soundSpeed = kSoundSpeed,
            .density = 1000.0}}),
      BoundaryModel::vacuum(surfaceDepth),
      BoundaryModel::rigid(seabedDepth));
}

Environment makeSurfaceGradientEnvironment() {
  return Environment(
      SoundSpeedProfile(
          {{.depth = 0.0, .soundSpeed = 1500.0, .density = 1000.0},
           {.depth = 100.0,
            .soundSpeed = 1500.001,
            .density = 1000.0}}),
      BoundaryModel::vacuum(0.0), BoundaryModel::rigid(100.0));
}

Environment makePiecewiseBottomEnvironment() {
  return Environment(
      SoundSpeedProfile(
          {{.depth = 0.0,
            .soundSpeed = kSoundSpeed,
            .density = 1000.0},
           {.depth = 120.0,
            .soundSpeed = kSoundSpeed,
            .density = 1000.0}}),
      BoundaryModel::vacuum(0.0),
      BoundaryModel::rigid(BoundaryGeometry::piecewiseLinear(
          {{.range = 0.0, .depth = 100.0},
           {.range = 1000.0, .depth = 120.0},
           {.range = 2000.0, .depth = 90.0}},
          120.0, BoundaryOrientation::Lower)));
}

Environment makePiecewiseSurfaceAndBottomEnvironment() {
  return Environment(
      SoundSpeedProfile(
          {{.depth = 0.0,
            .soundSpeed = kSoundSpeed,
            .density = 1000.0},
           {.depth = 120.0,
            .soundSpeed = kSoundSpeed,
            .density = 1000.0}}),
      BoundaryModel::vacuum(BoundaryGeometry::piecewiseLinear(
          {{.range = 0.0, .depth = 0.0},
           {.range = 1000.0, .depth = 20.0},
           {.range = 2000.0, .depth = 0.0}},
          0.0, BoundaryOrientation::Upper)),
      BoundaryModel::rigid(BoundaryGeometry::piecewiseLinear(
          {{.range = 0.0, .depth = 100.0},
           {.range = 1000.0, .depth = 120.0},
           {.range = 2000.0, .depth = 90.0}},
          120.0, BoundaryOrientation::Lower)));
}

Environment makeCurvilinearSurfaceAndBottomEnvironment() {
  return Environment(
      SoundSpeedProfile(
          {{.depth = 0.0,
            .soundSpeed = kSoundSpeed,
            .density = 1000.0},
           {.depth = 130.0,
            .soundSpeed = kSoundSpeed,
            .density = 1000.0}}),
      BoundaryModel::vacuum(BoundaryGeometry::curvilinear(
          {{.range = 0.0, .depth = 4.0},
           {.range = 450.0, .depth = 15.0},
           {.range = 1050.0, .depth = 2.0},
           {.range = 1650.0, .depth = 19.0},
           {.range = 2300.0, .depth = 7.0}},
          0.0, BoundaryOrientation::Upper)),
      BoundaryModel::rigid(BoundaryGeometry::curvilinear(
          {{.range = 0.0, .depth = 111.0},
           {.range = 550.0, .depth = 93.0},
           {.range = 1100.0, .depth = 121.0},
           {.range = 1750.0, .depth = 98.0},
           {.range = 2300.0, .depth = 114.0}},
          130.0, BoundaryOrientation::Lower)));
}

IntegratorSettings makeSettings(double stepLength,
                                std::size_t maximumRayPoints,
                                double rangeLimit = 10000.0,
                                double depthLimit = 1000.0) {
  return IntegratorSettings{.stepLength = stepLength,
                            .rangeLimit = rangeLimit,
                            .depthLimit = depthLimit,
                            .maximumRayPoints = maximumRayPoints};
}

void checkPathInvariant(Context& context, const RayPath& path,
                        const char* message) {
  context.check(path.points.size() ==
                    path.steps.size() + path.events.size() + 1U,
                message);
  for (std::size_t index = 1U; index < path.events.size(); ++index) {
    context.check(
        path.events[index - 1U].rayPointIndex <
            path.events[index].rayPointIndex,
        "reflection-event indices are strictly increasing");
  }
}

void checkEventPair(Context& context, const RayPath& path,
                    std::size_t eventIndex) {
  const auto& event = path.events[eventIndex];
  const auto& incident = path.points[event.rayPointIndex];
  const auto& reflected = path.points[event.rayPointIndex + 1U];

  context.check(incident.position == reflected.position,
                "D-10 pre/post states retain exactly the same position");
  context.checkNear(incident.realTravelTime, reflected.realTravelTime, 0.0,
                    "D-10 pre/post states retain the same travel time");
  context.check(event.position == incident.position,
                "reflection event references its actual incident position");
  context.check(event.reflectedRayPointIndex == event.rayPointIndex + 1U,
                "reflection event explicitly references its post state");
  context.check(event.incidentSlowness == incident.slowness,
                "reflection event retains incident slowness");
  context.check(event.reflectedSlowness == reflected.slowness,
                "reflection event retains reflected slowness");
  context.checkNear(
      bellhop::dot(incident.slowness, event.boundaryTangent),
      bellhop::dot(reflected.slowness, event.boundaryTangent), 1.0e-15,
      "flat reflection preserves tangent slowness");
  context.checkNear(
      bellhop::dot(incident.slowness, event.outwardNormal),
      -bellhop::dot(reflected.slowness, event.outwardNormal), 1.0e-15,
      "flat reflection reverses outward-normal slowness");
}

void testSingleSeaSurfaceReflectionAndArrivalGradient(Context& context) {
  constexpr double angle = -0.25 * kPi;
  constexpr double stepLength = 10.0;
  const GeometryTracer tracer(makeSurfaceGradientEnvironment(),
                              makeSettings(stepLength, 4U));
  const RayPath path = tracer.trace(Source{.depth = 5.0}, angle);

  context.check(path.terminationReason == RayTerminationReason::PointLimit,
                "surface reflection fills the configured four-point path");
  context.check(path.points.size() == 4U && path.steps.size() == 2U &&
                    path.events.size() == 1U,
                "exact surface arrival is followed by one minimum step and "
                "a complete reflection pair");
  if (path.points.size() != 4U || path.steps.size() != 2U ||
      path.events.size() != 1U) {
    std::cerr << "surface reflection shape: points=" << path.points.size()
              << ", steps=" << path.steps.size()
              << ", events=" << path.events.size()
              << ", termination="
              << static_cast<int>(path.terminationReason)
              << ", last_depth=" << path.points.back().position.depth
              << '\n';
  }
  checkPathInvariant(
      context, path,
      "single surface reflection satisfies P = 1 + S + E");
  if (path.events.empty() || path.points.size() < 3U) {
    return;
  }

  const auto& event = path.events.front();
  context.check(event.rayPointIndex == 2U,
                "surface event indexes the outside incident point");
  context.checkNear(path.points[1U].position.depth, 0.0, 1.0e-14,
                    "exact surface arrival remains an integrated point");
  context.check(path.points[2U].position.depth < 0.0,
                "following minimum step crosses outside the surface");
  context.check(event.boundary == ReflectionBoundary::SeaSurface,
                "surface event records the sea-surface boundary");
  context.check(event.boundarySegmentIndex == 0U,
                "flat surface uses its sole boundary segment");
  context.check(event.boundaryTangent == Vec2{1.0, 0.0},
                "surface tangent points toward increasing range");
  context.check(event.outwardNormal == Vec2{0.0, -1.0},
                "surface normal points out of the water");
  checkEventPair(context, path, 0U);

  const auto expected = bellhop::reflectAtFlatBoundary(
      path.points[2U], ReflectionBoundary::SeaSurface,
      FlatBoundaryGeometry{
          .point = {0.0, 0.0},
          .tangent = {1.0, 0.0},
          .outwardNormal = {0.0, -1.0},
          .soundSpeedGradient = {0.0, 1.0e-5},
          .segmentIndex = 0U,
          .maximumIncidentPlaneDistance = 1.0e-3 * stepLength},
      2U, BoundaryCurvatureMode::Standard);
  context.checkNear(path.points[3U].dynamicP[0],
                    expected.reflectedState.dynamicP[0], 1.0e-14,
                    "surface reflection uses the arrival-side SSP gradient");
  context.checkNear(path.points[3U].dynamicP[1],
                    expected.reflectedState.dynamicP[1], 1.0e-14,
                    "both dynamic bases use Standard curvature correction");
}

void testSingleSeabedReflection(Context& context) {
  const GeometryTracer tracer(makeConstantEnvironment(),
                              makeSettings(10.0, 3U));
  const RayPath path =
      tracer.trace(Source{.depth = 95.0}, 0.5 * kPi);

  context.check(path.terminationReason == RayTerminationReason::PointLimit,
                "seabed reflection fills the configured three-point path");
  context.check(path.points.size() == 3U && path.steps.size() == 1U &&
                    path.events.size() == 1U,
                "seabed reflection emits one complete D-10 event");
  checkPathInvariant(context, path,
                     "single seabed reflection satisfies P = 1 + S + E");
  if (path.events.empty() || path.points.size() < 3U) {
    return;
  }

  const auto& event = path.events.front();
  context.check(event.boundary == ReflectionBoundary::Seabed,
                "seabed event records the lower boundary");
  context.check(event.boundarySegmentIndex == 0U,
                "flat seabed uses its sole boundary segment");
  context.check(event.boundaryTangent == Vec2{1.0, 0.0},
                "seabed tangent points toward increasing range");
  context.check(event.outwardNormal == Vec2{0.0, 1.0},
                "seabed normal points out of the water");
  checkEventPair(context, path, 0U);
}

void testPiecewiseLinearSeabedReflection(Context& context) {
  constexpr double angle = 20.0 * kPi / 180.0;
  constexpr double stepLength = 500.0;
  const GeometryTracer tracer(
      makePiecewiseBottomEnvironment(),
      makeSettings(stepLength, 5U, 5000.0, 500.0));
  const RayPath path = tracer.trace(Source{.depth = 50.0}, angle);

  context.check(path.terminationReason == RayTerminationReason::PointLimit,
                "piecewise seabed trace fills the bounded test path");
  context.check(path.events.size() == 1U,
                "piecewise seabed produces one reflection event");
  context.check(path.steps.size() == 3U && path.points.size() == 5U,
                "segment endpoint and boundary crossings retain the "
                "Fortran minimum-step transitions");
  if (path.events.empty()) {
    return;
  }

  const auto& event = path.events.front();
  const double segmentLength = std::hypot(1000.0, 20.0);
  context.check(event.boundary == ReflectionBoundary::Seabed,
                "sloping event records the seabed");
  context.check(event.boundarySegmentIndex == 1U,
                "sloping event records the active physical segment");
  context.checkNear(event.boundaryTangent.range,
                    1000.0 / segmentLength, 1.0e-15,
                    "sloping reflection uses the segment tangent");
  context.checkNear(event.boundaryTangent.depth,
                    20.0 / segmentLength, 1.0e-15,
                    "sloping reflection retains the tangent slope");
  context.checkNear(event.outwardNormal.range,
                    -20.0 / segmentLength, 1.0e-15,
                    "sloping reflection uses the lower outward normal");
  context.checkNear(event.outwardNormal.depth,
                    1000.0 / segmentLength, 1.0e-15,
                    "sloping lower normal points out of the water");
  context.check(path.steps.front().stepLength == 0.5,
                "source at a boundary node advances by the minimum step");
  checkEventPair(context, path, 0U);
  checkPathInvariant(
      context, path,
      "piecewise reflection satisfies P = 1 + S + E");
}

void testRayParallelToSlopeCrossesSegmentWithoutReflection(Context& context) {
  const double slopeAngle = std::atan2(20.0, 1000.0);
  const GeometryTracer tracer(
      makePiecewiseBottomEnvironment(),
      makeSettings(750.0, 5U, 3000.0, 500.0));
  const RayPath path =
      tracer.trace(Source{.depth = 50.0}, slopeAngle);

  context.check(path.events.empty(),
                "ray parallel to the first bottom slope does not reflect");
  context.check(path.points.size() == 5U && path.steps.size() == 4U,
                "parallel ray still resolves the cached segment endpoint");
  context.checkNear(path.points[3U].position.range, 1000.0, 1.0e-12,
                    "parallel ray lands on the shared segment endpoint");
  context.checkNear(path.steps[3U].stepLength, 0.75, 1.0e-15,
                    "endpoint landing is followed by the minimum step");
}

void testMinimumStepOvershootIsNotProjected(Context& context) {
  constexpr double sourceDepth = 0.0025;
  constexpr double stepLength = 10.0;
  const GeometryTracer tracer(makeConstantEnvironment(),
                              makeSettings(stepLength, 3U));
  const RayPath path =
      tracer.trace(Source{.depth = sourceDepth}, -0.5 * kPi);

  context.check(path.events.size() == 1U,
                "minimum-step surface overshoot still reflects");
  if (path.events.empty() || path.steps.empty() ||
      path.points.size() < 3U) {
    return;
  }
  context.checkNear(path.steps.front().stepLength, 0.01, 1.0e-15,
                    "boundary limiter retains the Fortran minimum step");
  const double expectedDepth = sourceDepth - 0.01;
  context.checkNear(path.points[1U].position.depth, expectedDepth, 1.0e-15,
                    "incident point retains its real outside depth");
  context.check(path.points[1U].position.depth < 0.0,
                "incident point is not projected onto the ideal plane");
  context.check(path.points[1U].position == path.points[2U].position,
                "overshoot reflection appends an identical-position post "
                "state");
  checkPathInvariant(
      context, path,
      "minimum-step overshoot satisfies P = 1 + S + E");
}

void testContinuousMultipleReflections(Context& context) {
  constexpr std::size_t maximumRayPoints = 40U;
  const GeometryTracer tracer(makeConstantEnvironment(0.0, 20.0),
                              makeSettings(7.0, maximumRayPoints));
  const RayPath path =
      tracer.trace(Source{.depth = 10.0}, kPi / 3.0);

  context.check(path.terminationReason == RayTerminationReason::PointLimit,
                "bounded multi-bounce trace terminates at its point budget");
  context.check(path.points.size() <= maximumRayPoints,
                "post-reflection points count against maximumRayPoints");
  context.check(path.events.size() >= 3U,
                "multi-bounce trace contains repeated reflections");
  checkPathInvariant(context, path,
                     "multi-bounce trace satisfies P = 1 + S + E");

  for (std::size_t index = 0U; index < path.events.size(); ++index) {
    const ReflectionBoundary expected =
        index % 2U == 0U ? ReflectionBoundary::Seabed
                         : ReflectionBoundary::SeaSurface;
    context.check(path.events[index].boundary == expected,
                  "multi-bounce flat-boundary events alternate");
    checkEventPair(context, path, index);
  }
}

void testPointLimitDoesNotLeaveHalfReflection(Context& context) {
  const GeometryTracer insufficient(
      makeConstantEnvironment(), makeSettings(10.0, 2U));
  const RayPath truncated =
      insufficient.trace(Source{.depth = 0.0025}, -0.5 * kPi);

  context.check(
      truncated.terminationReason == RayTerminationReason::PointLimit,
      "insufficient reflection capacity terminates as PointLimit");
  context.check(truncated.points.size() == 1U &&
                    truncated.steps.empty() &&
                    truncated.events.empty(),
                "insufficient capacity commits none of the reflection pair");
  checkPathInvariant(
      context, truncated,
      "atomic PointLimit result satisfies P = 1 + S + E");

  const GeometryTracer exact(makeConstantEnvironment(),
                             makeSettings(10.0, 3U));
  const RayPath complete =
      exact.trace(Source{.depth = 0.0025}, -0.5 * kPi);
  context.check(complete.points.size() == 3U &&
                    complete.steps.size() == 1U &&
                    complete.events.size() == 1U,
                "maximumRayPoints includes the reflected post point");
}

void testReflectedPathFreezesInCache(Context& context) {
  const GeometryTracer tracer(makeConstantEnvironment(),
                              makeSettings(10.0, 3U));
  RayPathCache cache;
  cache.append(tracer.trace(Source{.depth = 95.0}, 0.5 * kPi));
  cache.freeze();

  context.check(cache.frozen(),
                "reflected GeometryTracer path freezes in RayPathCache");
  context.check(cache.at(0U).events.size() == 1U,
                "frozen cache retains its reflection event");
  checkPathInvariant(
      context, cache.at(0U),
      "frozen reflected path satisfies P = 1 + S + E");
}

void testFusedBoundaryResidualMatchesFortran(Context& context) {
  constexpr double launchAngle = 5.23721137751083754e-1;
  const GeometryTracer tracer(
      makeConstantEnvironment(),
      makeSettings(10.0, 100000U, 5100.0, 101.0));
  const RayPath path =
      tracer.trace(Source{.depth = 50.0}, launchAngle);

  const std::size_t topBounceCount = static_cast<std::size_t>(
      std::count_if(path.events.begin(), path.events.end(),
                    [](const auto& event) {
                      return event.boundary ==
                             ReflectionBoundary::SeaSurface;
                    }));
  context.check(path.terminationReason == RayTerminationReason::ExitedDomain,
                "30-degree reflected ray exits through the range box");
  context.check(path.points.size() == 621U &&
                    path.steps.size() == 591U &&
                    path.events.size() == 29U,
                "fused boundary residual matches the full Fortran sequence");
  context.check(topBounceCount == 14U &&
                    path.events.size() - topBounceCount == 15U,
                "fused boundary residual preserves every top/bottom bounce");
  checkPathInvariant(
      context, path,
      "30-degree standard-case path satisfies P = 1 + S + E");
}

void testPiecewiseMultiBounceMatchesFortran(Context& context) {
  constexpr double launchAngle = -6.98131700797731804e-1;
  const GeometryTracer tracer(
      makePiecewiseSurfaceAndBottomEnvironment(),
      makeSettings(500.0, 10000U, 2100.0, 121.0));
  const RayPath path =
      tracer.trace(Source{.depth = 50.0}, launchAngle);

  const std::size_t topBounceCount = static_cast<std::size_t>(
      std::count_if(path.events.begin(), path.events.end(),
                    [](const auto& event) {
                      return event.boundary ==
                             ReflectionBoundary::SeaSurface;
                    }));
  context.check(path.terminationReason == RayTerminationReason::ExitedDomain,
                "piecewise oracle ray exits through the range box");
  context.check(path.points.size() == 84U &&
                    path.steps.size() == 62U &&
                    path.events.size() == 21U,
                "piecewise multi-bounce shape matches the Fortran oracle");
  context.check(topBounceCount == 11U &&
                    path.events.size() - topBounceCount == 10U,
                "piecewise oracle preserves every top/bottom bounce");
  if (!path.events.empty()) {
    context.check(path.events.front().rayPointIndex == 2U,
                  "first sloping-top landing reflects without an extra step");
  }
  checkPathInvariant(
      context, path,
      "piecewise oracle path satisfies P = 1 + S + E");
}

void testCurvilinearMultiBounceMatchesFortran(Context& context) {
  constexpr double launchAngle = -2.438888037721334e-1;
  const GeometryTracer tracer(
      makeCurvilinearSurfaceAndBottomEnvironment(),
      makeSettings(500.0, 10000U, 2100.0, 131.0));
  const RayPath path =
      tracer.trace(Source{.depth = 48.0}, launchAngle);

  const std::size_t topBounceCount = static_cast<std::size_t>(
      std::count_if(path.events.begin(), path.events.end(),
                    [](const auto& event) {
                      return event.boundary ==
                             ReflectionBoundary::SeaSurface;
                    }));
  context.check(path.terminationReason == RayTerminationReason::ExitedDomain,
                "curvilinear oracle ray exits through the range box");
  context.check(path.points.size() == 29U && path.steps.size() == 22U &&
                    path.events.size() == 6U,
                "curvilinear multi-bounce shape matches the Fortran oracle");
  context.check(topBounceCount == 3U &&
                    path.events.size() - topBounceCount == 3U,
                "curvilinear oracle preserves every top/bottom bounce");
  for (const auto& event : path.events) {
    context.check(event.boundaryCurvature != 0.0,
                  "curvilinear physical-segment reflections retain kappa");
    context.check(bellhop::norm(event.boundaryTangent) < 1.0,
                  "curvilinear reflection frame is not renormalized");
  }
  if (path.events.size() >= 2U) {
    const auto& top = path.events[0U];
    context.check(top.boundary == ReflectionBoundary::SeaSurface &&
                      top.boundarySegmentIndex == 1U,
                  "first curvilinear oracle event uses top physical segment");
    context.checkNear(top.position.range, 161.4882321836457, 1.0e-10,
                      "first curvilinear reflection range matches oracle");
    context.checkNear(top.position.depth, 7.814890518004077, 1.0e-12,
                      "first curvilinear reflection depth matches oracle");
    context.checkNear(top.boundaryTangent.range, 0.9998085833338034,
                      1.0e-15,
                      "first interpolated tangent range matches oracle");
    context.checkNear(top.boundaryTangent.depth, 0.008331882787290048,
                      1.0e-15,
                      "first interpolated tangent depth matches oracle");
    context.checkNear(top.boundaryCurvature, -0.0001023773616463405,
                      1.0e-18,
                      "first top curvature matches oracle");
    context.checkNear(top.reflectedSlowness.range,
                      0.0006441655711190538, 1.0e-18,
                      "first reflected range slowness matches oracle");
    context.checkNear(top.reflectedSlowness.depth,
                      0.00017164054349210126, 1.0e-18,
                      "first reflected depth slowness matches oracle");
    context.checkNear(path.points[top.reflectedRayPointIndex].dynamicP[0],
                      1.1365591001486675, 1.0e-14,
                      "first curvature jump p1 matches oracle");

    const auto& bottom = path.events[1U];
    context.check(bottom.boundary == ReflectionBoundary::Seabed &&
                      bottom.boundarySegmentIndex == 1U,
                  "second curvilinear oracle event uses bottom segment");
    context.checkNear(bottom.boundaryTangent.range, 0.9991572645683603,
                      1.0e-15,
                      "second interpolated tangent range matches oracle");
    context.checkNear(bottom.boundaryTangent.depth, 0.006256177145108277,
                      1.0e-15,
                      "second interpolated tangent depth matches oracle");
    context.checkNear(bottom.boundaryCurvature, 0.0001518221312076167,
                      1.0e-18,
                      "second bottom curvature matches oracle");
    context.checkNear(path.points[bottom.reflectedRayPointIndex].dynamicP[0],
                      1.803666459473942, 1.0e-13,
                      "second curvature jump p1 matches oracle");
  }

  RayPathCache cache;
  cache.append(path);
  cache.freeze();
  context.check(cache.frozen(),
                "curvilinear legacy reflection frames freeze in cache");
  checkPathInvariant(
      context, path,
      "curvilinear oracle path satisfies P = 1 + S + E");
}

void testTracerPropagatesCurvatureMode(Context& context) {
  constexpr double launchAngle = -2.438888037721334e-1;
  const Environment environment =
      makeCurvilinearSurfaceAndBottomEnvironment();
  const IntegratorSettings settings =
      makeSettings(500.0, 10000U, 2100.0, 131.0);
  const RayPath standard =
      GeometryTracer(environment, settings, BoundaryCurvatureMode::Standard)
          .trace(Source{.depth = 48.0}, launchAngle);
  const RayPath doubled =
      GeometryTracer(environment, settings, BoundaryCurvatureMode::Double)
          .trace(Source{.depth = 48.0}, launchAngle);
  const RayPath zeroed =
      GeometryTracer(environment, settings, BoundaryCurvatureMode::Zero)
          .trace(Source{.depth = 48.0}, launchAngle);
  context.check(!standard.events.empty() && !doubled.events.empty() &&
                    !zeroed.events.empty(),
                "all curvature modes retain the same reflection sequence");
  const std::size_t standardPoint =
      standard.events.front().reflectedRayPointIndex;
  const std::size_t doubledPoint =
      doubled.events.front().reflectedRayPointIndex;
  const std::size_t zeroedPoint =
      zeroed.events.front().reflectedRayPointIndex;
  context.checkNear(standard.points[standardPoint].dynamicP[0U],
                    1.1365591001486675, 1.0e-14,
                    "standard tracer curvature jump matches Origin");
  context.checkNear(doubled.points[doubledPoint].dynamicP[0U],
                    1.2731182002973349, 1.0e-14,
                    "double tracer curvature jump doubles the full RN");
  context.checkNear(zeroed.points[zeroedPoint].dynamicP[0U],
                    1.0, 0.0,
                    "zero tracer curvature mode suppresses the full RN");
  context.check(standard.points.size() == doubled.points.size() &&
                    standard.points.size() == zeroed.points.size() &&
                    standard.events.size() == doubled.events.size() &&
                    standard.events.size() == zeroed.events.size(),
                "curvature mode changes dynamics without retracing centers");
  context.check(standard.points[standardPoint].position ==
                    doubled.points[doubledPoint].position &&
                    standard.points[standardPoint].position ==
                    zeroed.points[zeroedPoint].position &&
                    standard.points[standardPoint].slowness ==
                    doubled.points[doubledPoint].slowness &&
                    standard.points[standardPoint].slowness ==
                    zeroed.points[zeroedPoint].slowness,
                "curvature modes preserve reflected center position/slowness");
}

void testCurvilinearTwoPointsOutsideTermination(Context& context) {
  constexpr double launchAngle = -6.097220094303335e-1;
  const GeometryTracer tracer(
      makeCurvilinearSurfaceAndBottomEnvironment(),
      makeSettings(500.0, 10000U, 2100.0, 131.0));
  const RayPath path =
      tracer.trace(Source{.depth = 48.0}, launchAngle);

  context.check(path.terminationReason == RayTerminationReason::ExitedDomain,
                "curvilinear outside-boundary escape is a normal exit");
  context.check(path.terminationDetail ==
                    "two consecutive points outside sea surface",
                "curvilinear escape records the legacy top-boundary cause");
  context.check(path.points.size() == 56U && path.steps.size() == 42U &&
                    path.events.size() == 13U,
                "curvilinear outside escape matches the Fortran oracle");
  checkPathInvariant(
      context, path,
      "curvilinear outside termination satisfies P = 1 + S + E");
}

void testUnreflectedDirectRayDoesNotDrift(Context& context) {
  constexpr std::size_t oneBasedAlphaIndex = 150U;
  constexpr std::size_t angleCount = 300U;
  constexpr double minimumDegrees = -5.0;
  constexpr double maximumDegrees = 5.0;
  const double degrees =
      minimumDegrees +
      static_cast<double>(oneBasedAlphaIndex - 1U) *
          (maximumDegrees - minimumDegrees) /
          static_cast<double>(angleCount - 1U);
  const double angle = degrees * kPi / 180.0;
  const GeometryTracer tracer(
      makeConstantEnvironment(0.0, 1000.0),
      makeSettings(10.0, 10000U, 5100.0, 1100.0));
  const RayPath path = tracer.trace(Source{.depth = 500.0}, angle);

  context.check(path.events.empty(),
                "standard direct ray remains reflection-free");
  context.check(path.points.size() == 512U && path.steps.size() == 511U,
                "reflection wiring does not change direct oracle shape");
  double arcLength = 0.0;
  for (const auto& step : path.steps) {
    arcLength += step.stepLength;
  }
  context.checkNear(path.points.back().position.range,
                    arcLength * std::cos(angle), 2.0e-10,
                    "direct endpoint range remains analytic");
  context.checkNear(path.points.back().position.depth,
                    500.0 + arcLength * std::sin(angle), 2.0e-10,
                    "direct endpoint depth remains analytic");
  checkPathInvariant(
      context, path,
      "unreflected direct path satisfies P = 1 + S + E");
}

}  // namespace

int main() {
  Context context;
  testSingleSeaSurfaceReflectionAndArrivalGradient(context);
  testSingleSeabedReflection(context);
  testPiecewiseLinearSeabedReflection(context);
  testRayParallelToSlopeCrossesSegmentWithoutReflection(context);
  testMinimumStepOvershootIsNotProjected(context);
  testContinuousMultipleReflections(context);
  testPointLimitDoesNotLeaveHalfReflection(context);
  testReflectedPathFreezesInCache(context);
  testFusedBoundaryResidualMatchesFortran(context);
  testPiecewiseMultiBounceMatchesFortran(context);
  testCurvilinearMultiBounceMatchesFortran(context);
  testTracerPropagatesCurvatureMode(context);
  testCurvilinearTwoPointsOutsideTermination(context);
  testUnreflectedDirectRayDoesNotDrift(context);

  if (context.failureCount() != 0) {
    std::cerr << context.failureCount()
              << " GeometryTracer reflection assertion(s) failed\n";
    return 1;
  }

  std::cout << "All GeometryTracer reflection tests passed\n";
  return 0;
}
