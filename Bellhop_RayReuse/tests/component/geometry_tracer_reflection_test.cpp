#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <string>
#include <vector>

#include "rayreuse/cache/ray_path_cache.hpp"
#include "rayreuse/model/environment.hpp"
#include "rayreuse/model/simulation_case.hpp"
#include "rayreuse/ray/flat_boundary_reflection.hpp"
#include "rayreuse/ray/geometry_tracer.hpp"
#include "support/test_harness.hpp"

namespace {

using rayreuse::BoundaryCurvatureMode;
using rayreuse::BoundaryModel;
using rayreuse::Environment;
using rayreuse::FlatBoundaryGeometry;
using rayreuse::GeometryTracer;
using rayreuse::IntegratorSettings;
using rayreuse::RayPath;
using rayreuse::RayPathCache;
using rayreuse::RayTerminationReason;
using rayreuse::ReflectionBoundary;
using rayreuse::SoundSpeedPoint;
using rayreuse::SoundSpeedProfile;
using rayreuse::Source;
using rayreuse::Vec2;
using rayreuse::test::Context;

constexpr double kPi = 3.141592653589793238462643383279502884;
constexpr double kSoundSpeed = 1500.0;

Environment makeConstantEnvironment(double surfaceDepth = 0.0,
                                    double seabedDepth = 100.0) {
  return Environment(SoundSpeedProfile({{.depth = surfaceDepth,
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
           {.depth = 100.0, .soundSpeed = 1500.001, .density = 1000.0}}),
      BoundaryModel::vacuum(0.0), BoundaryModel::rigid(100.0));
}

IntegratorSettings makeSettings(double stepLength, std::size_t maximumRayPoints,
                                double rangeLimit = 10000.0,
                                double depthLimit = 1000.0) {
  return IntegratorSettings{.stepLength = stepLength,
                            .rangeLimit = rangeLimit,
                            .depthLimit = depthLimit,
                            .maximumRayPoints = maximumRayPoints};
}

void checkPathInvariant(Context& context, const RayPath& path,
                        const char* message) {
  context.check(
      path.points.size() == path.steps.size() + path.events.size() + 1U,
      message);
  for (std::size_t index = 1U; index < path.events.size(); ++index) {
    context.check(path.events[index - 1U].rayPointIndex <
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
  context.check(event.incidentSlowness == incident.slowness,
                "reflection event retains incident slowness");
  context.check(event.reflectedSlowness == reflected.slowness,
                "reflection event retains reflected slowness");
  context.checkNear(rayreuse::dot(incident.slowness, event.boundaryTangent),
                    rayreuse::dot(reflected.slowness, event.boundaryTangent),
                    1.0e-15, "flat reflection preserves tangent slowness");
  context.checkNear(rayreuse::dot(incident.slowness, event.outwardNormal),
                    -rayreuse::dot(reflected.slowness, event.outwardNormal),
                    1.0e-15,
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
              << ", termination=" << static_cast<int>(path.terminationReason)
              << ", last_depth=" << path.points.back().position.depth << '\n';
  }
  checkPathInvariant(context, path,
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

  const auto expected = rayreuse::reflectAtFlatBoundary(
      path.points[2U], ReflectionBoundary::SeaSurface,
      FlatBoundaryGeometry{.point = {0.0, 0.0},
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

void testTracerPropagatesCurvatureMode(Context& context) {
  constexpr double angle = -0.25 * kPi;
  const Environment environment = makeSurfaceGradientEnvironment();
  const IntegratorSettings settings = makeSettings(10.0, 4U);
  const RayPath standard =
      GeometryTracer(environment, settings, BoundaryCurvatureMode::Standard)
          .trace(Source{.depth = 5.0}, angle);
  const RayPath doubled =
      GeometryTracer(environment, settings, BoundaryCurvatureMode::Double)
          .trace(Source{.depth = 5.0}, angle);
  const RayPath zeroed =
      GeometryTracer(environment, settings, BoundaryCurvatureMode::Zero)
          .trace(Source{.depth = 5.0}, angle);

  context.check(standard.events.size() == 1U &&
                    doubled.events.size() == 1U &&
                    zeroed.events.size() == 1U &&
                    standard.points.size() == doubled.points.size() &&
                    standard.points.size() == zeroed.points.size(),
                "D/S/Z retain the same center-ray reflection topology");
  if (standard.events.empty() || doubled.events.empty() ||
      zeroed.events.empty()) {
    return;
  }
  const std::size_t incidentIndex = standard.events.front().rayPointIndex;
  const std::size_t reflectedIndex =
      standard.events.front().reflectedRayPointIndex;
  context.check(
      standard.points[incidentIndex].position ==
              doubled.points[incidentIndex].position &&
          standard.points[incidentIndex].position ==
              zeroed.points[incidentIndex].position &&
          standard.points[reflectedIndex].slowness ==
              doubled.points[reflectedIndex].slowness &&
          standard.points[reflectedIndex].slowness ==
              zeroed.points[reflectedIndex].slowness,
      "D/S/Z change dynamic bases without changing center trajectory");
  for (std::size_t basis = 0U; basis < 2U; ++basis) {
    const double standardJump =
        standard.points[reflectedIndex].dynamicP[basis] -
        standard.points[incidentIndex].dynamicP[basis];
    const double doubledJump =
        doubled.points[reflectedIndex].dynamicP[basis] -
        doubled.points[incidentIndex].dynamicP[basis];
    context.check(standardJump != 0.0,
                  "gradient reflection produces a nonzero standard RN jump");
    context.checkNear(doubledJump, 2.0 * standardJump, 1.0e-14,
                      "Double mode doubles the complete RN jump");
    context.checkNear(zeroed.points[reflectedIndex].dynamicP[basis],
                      zeroed.points[incidentIndex].dynamicP[basis], 0.0,
                      "Zero mode suppresses the complete RN jump");
    context.checkNear(standard.points[reflectedIndex].dynamicQ[basis],
                      doubled.points[reflectedIndex].dynamicQ[basis], 0.0,
                      "D/S/Z preserve reflected dynamic q");
  }
}

void testSingleSeabedReflection(Context& context) {
  const GeometryTracer tracer(makeConstantEnvironment(),
                              makeSettings(10.0, 3U));
  const RayPath path = tracer.trace(Source{.depth = 95.0}, 0.5 * kPi);

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

void testMinimumStepOvershootIsNotProjected(Context& context) {
  constexpr double sourceDepth = 0.0025;
  constexpr double stepLength = 10.0;
  const GeometryTracer tracer(makeConstantEnvironment(),
                              makeSettings(stepLength, 3U));
  const RayPath path = tracer.trace(Source{.depth = sourceDepth}, -0.5 * kPi);

  context.check(path.events.size() == 1U,
                "minimum-step surface overshoot still reflects");
  if (path.events.empty() || path.steps.empty() || path.points.size() < 3U) {
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
  checkPathInvariant(context, path,
                     "minimum-step overshoot satisfies P = 1 + S + E");
}

void testContinuousMultipleReflections(Context& context) {
  constexpr std::size_t maximumRayPoints = 40U;
  const GeometryTracer tracer(makeConstantEnvironment(0.0, 20.0),
                              makeSettings(7.0, maximumRayPoints));
  const RayPath path = tracer.trace(Source{.depth = 10.0}, kPi / 3.0);

  context.check(path.terminationReason == RayTerminationReason::PointLimit,
                "bounded multi-bounce trace terminates at its point budget");
  context.check(path.points.size() <= maximumRayPoints,
                "post-reflection points count against maximumRayPoints");
  context.check(path.events.size() >= 3U,
                "multi-bounce trace contains repeated reflections");
  checkPathInvariant(context, path,
                     "multi-bounce trace satisfies P = 1 + S + E");

  for (std::size_t index = 0U; index < path.events.size(); ++index) {
    const ReflectionBoundary expected = index % 2U == 0U
                                            ? ReflectionBoundary::Seabed
                                            : ReflectionBoundary::SeaSurface;
    context.check(path.events[index].boundary == expected,
                  "multi-bounce flat-boundary events alternate");
    checkEventPair(context, path, index);
  }
}

void testPointLimitDoesNotLeaveHalfReflection(Context& context) {
  const GeometryTracer insufficient(makeConstantEnvironment(),
                                    makeSettings(10.0, 2U));
  const RayPath truncated =
      insufficient.trace(Source{.depth = 0.0025}, -0.5 * kPi);

  context.check(truncated.terminationReason == RayTerminationReason::PointLimit,
                "insufficient reflection capacity terminates as PointLimit");
  context.check(truncated.points.size() == 1U && truncated.steps.empty() &&
                    truncated.events.empty(),
                "insufficient capacity commits none of the reflection pair");
  checkPathInvariant(context, truncated,
                     "atomic PointLimit result satisfies P = 1 + S + E");

  const GeometryTracer exact(makeConstantEnvironment(), makeSettings(10.0, 3U));
  const RayPath complete = exact.trace(Source{.depth = 0.0025}, -0.5 * kPi);
  context.check(complete.points.size() == 3U && complete.steps.size() == 1U &&
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
  checkPathInvariant(context, cache.at(0U),
                     "frozen reflected path satisfies P = 1 + S + E");
}

void testFusedBoundaryResidualMatchesFortran(Context& context) {
  constexpr double launchAngle = 5.23721137751083754e-1;
  const GeometryTracer tracer(makeConstantEnvironment(),
                              makeSettings(10.0, 100000U, 5100.0, 101.0));
  const RayPath path = tracer.trace(Source{.depth = 50.0}, launchAngle);

  const std::size_t topBounceCount = static_cast<std::size_t>(std::count_if(
      path.events.begin(), path.events.end(), [](const auto& event) {
        return event.boundary == ReflectionBoundary::SeaSurface;
      }));
  context.check(path.terminationReason == RayTerminationReason::ExitedDomain,
                "30-degree reflected ray exits through the range box");
  context.check(path.points.size() == 621U && path.steps.size() == 591U &&
                    path.events.size() == 29U,
                "fused boundary residual matches the full Fortran sequence");
  context.check(
      topBounceCount == 14U && path.events.size() - topBounceCount == 15U,
      "fused boundary residual preserves every top/bottom bounce");
  checkPathInvariant(context, path,
                     "30-degree standard-case path satisfies P = 1 + S + E");
}

void testUnreflectedDirectRayDoesNotDrift(Context& context) {
  constexpr std::size_t oneBasedAlphaIndex = 150U;
  constexpr std::size_t angleCount = 300U;
  constexpr double minimumDegrees = -5.0;
  constexpr double maximumDegrees = 5.0;
  const double degrees =
      minimumDegrees + static_cast<double>(oneBasedAlphaIndex - 1U) *
                           (maximumDegrees - minimumDegrees) /
                           static_cast<double>(angleCount - 1U);
  const double angle = degrees * kPi / 180.0;
  const GeometryTracer tracer(makeConstantEnvironment(0.0, 1000.0),
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
  checkPathInvariant(context, path,
                     "unreflected direct path satisfies P = 1 + S + E");
}

}  // namespace

int main() {
  Context context;
  testSingleSeaSurfaceReflectionAndArrivalGradient(context);
  testTracerPropagatesCurvatureMode(context);
  testSingleSeabedReflection(context);
  testMinimumStepOvershootIsNotProjected(context);
  testContinuousMultipleReflections(context);
  testPointLimitDoesNotLeaveHalfReflection(context);
  testReflectedPathFreezesInCache(context);
  testFusedBoundaryResidualMatchesFortran(context);
  testUnreflectedDirectRayDoesNotDrift(context);

  if (context.failureCount() != 0) {
    std::cerr << context.failureCount()
              << " GeometryTracer reflection assertion(s) failed\n";
    return 1;
  }

  std::cout << "All GeometryTracer reflection tests passed\n";
  return 0;
}
