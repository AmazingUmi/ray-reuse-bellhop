#include "rayreuse/ray/flat_boundary_reflection.hpp"

#include <cmath>
#include <cstddef>
#include <limits>

#include "rayreuse/error.hpp"
#include "support/test_harness.hpp"

namespace {

using rayreuse::BoundaryCurvatureMode;
using rayreuse::FlatBoundaryGeometry;
using rayreuse::FlatBoundaryReflection;
using rayreuse::RayState;
using rayreuse::ReflectionBoundary;
using rayreuse::ValidationError;
using rayreuse::Vec2;
using rayreuse::test::Context;

constexpr double kPi = 3.141592653589793238462643383279502884;
constexpr double kSoundSpeed = 1500.0;

RayState makeState(double angleRadians, Vec2 position) {
  return RayState{.position = position,
                  .slowness = {std::cos(angleRadians) / kSoundSpeed,
                               std::sin(angleRadians) / kSoundSpeed},
                  .dynamicP = {2.0, -3.0},
                  .dynamicQ = {5.0, 7.0},
                  .soundSpeed = kSoundSpeed,
                  .realTravelTime = 0.25};
}

FlatBoundaryGeometry makeSurface(Vec2 gradient = {}) {
  return FlatBoundaryGeometry{.point = {0.0, 0.0},
                              .tangent = {1.0, 0.0},
                              .outwardNormal = {0.0, -1.0},
                              .soundSpeedGradient = gradient,
                              .segmentIndex = 4U,
                              .maximumIncidentPlaneDistance = 1.0e-8};
}

FlatBoundaryGeometry makeSeabed(Vec2 gradient = {}) {
  return FlatBoundaryGeometry{.point = {0.0, 100.0},
                              .tangent = {1.0, 0.0},
                              .outwardNormal = {0.0, 1.0},
                              .soundSpeedGradient = gradient,
                              .segmentIndex = 9U,
                              .maximumIncidentPlaneDistance = 1.0e-8};
}

void checkVectorNear(Context& context, Vec2 actual, Vec2 expected,
                     double tolerance, const char* message) {
  context.checkNear(actual.range, expected.range, tolerance, message);
  context.checkNear(actual.depth, expected.depth, tolerance, message);
}

void testSeaSurfaceMirrorAndEvent(Context& context) {
  const RayState incident = makeState(-kPi / 6.0, {25.0, 0.0});
  const FlatBoundaryReflection result = rayreuse::reflectAtFlatBoundary(
      incident, ReflectionBoundary::SeaSurface, makeSurface(), 12U,
      BoundaryCurvatureMode::Standard);

  context.checkNear(result.reflectedState.slowness.range,
                    incident.slowness.range, 1.0e-15,
                    "surface reflection preserves tangent slowness");
  context.checkNear(result.reflectedState.slowness.depth,
                    -incident.slowness.depth, 1.0e-15,
                    "surface reflection reverses normal slowness");
  checkVectorNear(context, result.reflectedState.position, incident.position,
                  0.0, "D-10 reflection keeps the same position");
  context.checkNear(result.reflectedState.realTravelTime,
                    incident.realTravelTime, 0.0,
                    "D-10 reflection keeps the same travel time");
  context.checkNear(
      kSoundSpeed * rayreuse::norm(result.reflectedState.slowness), 1.0,
      1.0e-14, "surface reflection preserves the sound-speed slowness norm");
  context.check(result.reflectedState.dynamicP == incident.dynamicP,
                "zero-gradient flat reflection preserves dynamic p");
  context.check(result.reflectedState.dynamicQ == incident.dynamicQ,
                "flat reflection preserves dynamic q");

  const auto& event = result.event;
  context.check(event.rayPointIndex == 12U,
                "event retains caller-provided pre-reflection index");
  context.check(event.boundary == ReflectionBoundary::SeaSurface,
                "event identifies sea surface");
  context.check(event.boundarySegmentIndex == 4U,
                "event retains boundary segment");
  checkVectorNear(context, event.position, incident.position, 0.0,
                  "event position is the incident boundary point");
  checkVectorNear(context, event.boundaryTangent, {1.0, 0.0}, 0.0,
                  "event stores boundary tangent");
  checkVectorNear(context, event.outwardNormal, {0.0, -1.0}, 0.0,
                  "event stores surface outward normal");
  checkVectorNear(context, event.incidentSlowness, incident.slowness, 0.0,
                  "event stores incident slowness");
  checkVectorNear(context, event.reflectedSlowness,
                  result.reflectedState.slowness, 0.0,
                  "event stores reflected slowness");
  context.checkNear(event.tangentSlowness,
                    rayreuse::dot(incident.slowness, {1.0, 0.0}), 1.0e-15,
                    "event stores tangent component");
  context.checkNear(event.normalSlowness,
                    rayreuse::dot(incident.slowness, {0.0, -1.0}), 1.0e-15,
                    "event stores outward incident normal component");
}

void testSeabedDynamicJump(Context& context) {
  const RayState incident = makeState(kPi / 4.0, {40.0, 100.0});
  const Vec2 gradient{0.25, 1.75};
  const FlatBoundaryGeometry geometry = makeSeabed(gradient);
  const FlatBoundaryReflection result = rayreuse::reflectAtFlatBoundary(
      incident, ReflectionBoundary::Seabed, geometry, 7U,
      BoundaryCurvatureMode::Standard);

  const Vec2 incidentUnit = kSoundSpeed * incident.slowness;
  const Vec2 reflectedUnit = kSoundSpeed * result.reflectedState.slowness;
  const Vec2 incidentRayNormal{-incidentUnit.depth, incidentUnit.range};
  const Vec2 reflectedRayNormal{reflectedUnit.depth, -reflectedUnit.range};
  const double cnJump =
      -rayreuse::dot(gradient, reflectedRayNormal - incidentRayNormal);
  const double csJump = -rayreuse::dot(gradient, reflectedUnit - incidentUnit);
  const double tangent = rayreuse::dot(incident.slowness, geometry.tangent);
  const double normal =
      rayreuse::dot(incident.slowness, geometry.outwardNormal);
  const double ratio = tangent / normal;
  const double expectedJump =
      ratio * (2.0 * cnJump - ratio * csJump) / (kSoundSpeed * kSoundSpeed);

  for (std::size_t index = 0; index < incident.dynamicP.size(); ++index) {
    context.checkNear(
        result.reflectedState.dynamicP[index],
        incident.dynamicP[index] + incident.dynamicQ[index] * expectedJump,
        1.0e-14,
        "seabed Standard mode applies CurvatureCorrection2 to dynamic p");
    context.checkNear(result.reflectedState.dynamicQ[index],
                      incident.dynamicQ[index], 0.0,
                      "seabed reflection preserves dynamic q");
  }
  context.check(result.event.boundary == ReflectionBoundary::Seabed,
                "event identifies seabed");
  context.check(result.event.boundarySegmentIndex == 9U,
                "seabed event retains segment index");
}

void testSurfaceSignAndCurvatureModes(Context& context) {
  const RayState incident = makeState(-kPi / 4.0, {40.0, 0.0});
  const FlatBoundaryGeometry geometry = makeSurface({0.25, 1.75});
  const FlatBoundaryReflection standard = rayreuse::reflectAtFlatBoundary(
      incident, ReflectionBoundary::SeaSurface, geometry, 2U,
      BoundaryCurvatureMode::Standard);
  const FlatBoundaryReflection doubled = rayreuse::reflectAtFlatBoundary(
      incident, ReflectionBoundary::SeaSurface, geometry, 2U,
      BoundaryCurvatureMode::Double);
  const FlatBoundaryReflection zeroed = rayreuse::reflectAtFlatBoundary(
      incident, ReflectionBoundary::SeaSurface, geometry, 2U,
      BoundaryCurvatureMode::Zero);

  const Vec2 incidentUnit = kSoundSpeed * incident.slowness;
  const Vec2 reflectedUnit = kSoundSpeed * standard.reflectedState.slowness;
  const Vec2 incidentRayNormal{-incidentUnit.depth, incidentUnit.range};
  const Vec2 reflectedRayNormal{reflectedUnit.depth, -reflectedUnit.range};
  const double topCnJump = rayreuse::dot(
      geometry.soundSpeedGradient, reflectedRayNormal - incidentRayNormal);
  const double csJump =
      -rayreuse::dot(geometry.soundSpeedGradient, reflectedUnit - incidentUnit);
  const double tangent = rayreuse::dot(incident.slowness, geometry.tangent);
  const double normal =
      rayreuse::dot(incident.slowness, geometry.outwardNormal);
  const double ratio = tangent / normal;
  const double expectedStandardJump =
      ratio * (2.0 * topCnJump - ratio * csJump) / (kSoundSpeed * kSoundSpeed);

  for (std::size_t index = 0; index < incident.dynamicP.size(); ++index) {
    const double standardDelta =
        standard.reflectedState.dynamicP[index] - incident.dynamicP[index];
    const double doubledDelta =
        doubled.reflectedState.dynamicP[index] - incident.dynamicP[index];
    context.checkNear(doubledDelta, 2.0 * standardDelta, 1.0e-14,
                      "Double mode doubles the dynamic reflection jump");
    context.checkNear(
        standardDelta, incident.dynamicQ[index] * expectedStandardJump, 1.0e-14,
        "surface Standard mode applies the top-boundary sign convention");
    context.checkNear(zeroed.reflectedState.dynamicP[index],
                      incident.dynamicP[index], 0.0,
                      "Zero mode suppresses the dynamic reflection jump");
  }
}

void testInvalidGeometry(Context& context) {
  const RayState surfaceIncident = makeState(-kPi / 6.0, {25.0, 0.0});

  FlatBoundaryGeometry nonUnit = makeSurface();
  nonUnit.tangent = {2.0, 0.0};
  context.expectThrows<ValidationError>(
      [&] {
        (void)rayreuse::reflectAtFlatBoundary(
            surfaceIncident, ReflectionBoundary::SeaSurface, nonUnit, 0U,
            BoundaryCurvatureMode::Standard);
      },
      "non-unit boundary tangent is rejected");

  FlatBoundaryGeometry nonOrthogonal = makeSurface();
  nonOrthogonal.outwardNormal = {0.6, -0.8};
  context.expectThrows<ValidationError>(
      [&] {
        (void)rayreuse::reflectAtFlatBoundary(
            surfaceIncident, ReflectionBoundary::SeaSurface, nonOrthogonal, 0U,
            BoundaryCurvatureMode::Standard);
      },
      "non-orthogonal boundary frame is rejected");

  FlatBoundaryGeometry wrongOrientation = makeSurface();
  wrongOrientation.outwardNormal = {0.0, 1.0};
  context.expectThrows<ValidationError>(
      [&] {
        (void)rayreuse::reflectAtFlatBoundary(
            surfaceIncident, ReflectionBoundary::SeaSurface, wrongOrientation,
            0U, BoundaryCurvatureMode::Standard);
      },
      "surface normal pointing into the water is rejected");

  FlatBoundaryGeometry nonFinite = makeSurface();
  nonFinite.point.range = std::numeric_limits<double>::quiet_NaN();
  context.expectThrows<ValidationError>(
      [&] {
        (void)rayreuse::reflectAtFlatBoundary(
            surfaceIncident, ReflectionBoundary::SeaSurface, nonFinite, 0U,
            BoundaryCurvatureMode::Standard);
      },
      "non-finite boundary geometry is rejected");

  const RayState offBoundary = makeState(-kPi / 6.0, {25.0, 1.0});
  context.expectThrows<ValidationError>(
      [&] {
        (void)rayreuse::reflectAtFlatBoundary(
            offBoundary, ReflectionBoundary::SeaSurface, makeSurface(), 0U,
            BoundaryCurvatureMode::Standard);
      },
      "incident point away from the boundary is rejected");

  const RayState minimumStepOvershoot = makeState(-kPi / 6.0, {25.0, -0.0025});
  FlatBoundaryGeometry overshootGeometry = makeSurface();
  overshootGeometry.maximumIncidentPlaneDistance = 0.01;
  const FlatBoundaryReflection overshoot = rayreuse::reflectAtFlatBoundary(
      minimumStepOvershoot, ReflectionBoundary::SeaSurface, overshootGeometry,
      1U, BoundaryCurvatureMode::Standard);
  context.check(
      overshoot.reflectedState.position == minimumStepOvershoot.position,
      "accepted minimum-step overshoot is reflected without projection");

  RayState accumulatedDrift = makeState(-kPi / 6.0, {25.0, 0.0});
  accumulatedDrift.slowness = 1.00005 * accumulatedDrift.slowness;
  const FlatBoundaryReflection drifted = rayreuse::reflectAtFlatBoundary(
      accumulatedDrift, ReflectionBoundary::SeaSurface, makeSurface(), 1U,
      BoundaryCurvatureMode::Standard);
  context.check(
      drifted.reflectedState.slowness.range == accumulatedDrift.slowness.range,
      "reflection accepts bounded modified-box slowness drift");

  RayState corruptSlowness = makeState(-kPi / 6.0, {25.0, 0.0});
  corruptSlowness.slowness = 1.001 * corruptSlowness.slowness;
  context.expectThrows<ValidationError>(
      [&] {
        (void)rayreuse::reflectAtFlatBoundary(
            corruptSlowness, ReflectionBoundary::SeaSurface, makeSurface(), 1U,
            BoundaryCurvatureMode::Standard);
      },
      "reflection still rejects materially inconsistent slowness");
}

}  // namespace

int main() {
  Context context;
  testSeaSurfaceMirrorAndEvent(context);
  testSeabedDynamicJump(context);
  testSurfaceSignAndCurvatureModes(context);
  testInvalidGeometry(context);
  return context.failureCount() == 0 ? 0 : 1;
}
