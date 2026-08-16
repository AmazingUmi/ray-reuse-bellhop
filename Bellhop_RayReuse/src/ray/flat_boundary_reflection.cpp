#include "rayreuse/ray/flat_boundary_reflection.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <string>

#include "rayreuse/error.hpp"

namespace rayreuse {
namespace {

constexpr double kFrameTolerance = 1.0e-10;
// Modified-box integration does not explicitly renormalize slowness.  Long
// refracted paths can therefore reach a valid boundary with O(1e-4) drift in
// c*|t|, especially with a curved PCHIP SSP. Keep a guard against corrupt
// states without rejecting that accumulated integration error.
constexpr double kSlownessNormTolerance = 2.0e-4;
constexpr double kPositionTolerance = 1.0e-8;
constexpr double kGrazingTolerance = 1.0e-12;

[[nodiscard]] bool finiteArray(const std::array<double, 2>& values) noexcept {
  return std::isfinite(values[0]) && std::isfinite(values[1]);
}

void validateState(const RayState& state, bool requireUnitSlowness) {
  if (!isFinite(state.position) || !isFinite(state.slowness) ||
      !finiteArray(state.dynamicP) || !finiteArray(state.dynamicQ) ||
      !std::isfinite(state.soundSpeed) ||
      !std::isfinite(state.realTravelTime)) {
    throw ValidationError(
        "incident reflection state must contain only finite values");
  }
  if (state.soundSpeed <= 0.0) {
    throw ValidationError(
        "incident reflection state sound speed must be positive");
  }
  if (state.realTravelTime < 0.0) {
    throw ValidationError(
        "incident reflection state travel time must be non-negative");
  }
  if (requireUnitSlowness) {
    const double slownessNormError =
        std::abs(state.soundSpeed * norm(state.slowness) - 1.0);
    if (slownessNormError > kSlownessNormTolerance) {
      throw ValidationError(
          "incident slowness norm must be consistent with sound speed; "
          "error=" +
          std::to_string(slownessNormError));
    }
  }
}

void validateGeometry(ReflectionBoundary boundary,
                      const BoundaryReflectionGeometry& geometry,
                      Vec2 incidentPosition) {
  if (!isFinite(geometry.collisionPlanePoint) ||
      !isFinite(geometry.collisionPlaneOutwardNormal) ||
      !isFinite(geometry.reflectionTangent) ||
      !isFinite(geometry.reflectionOutwardNormal) ||
      !isFinite(geometry.soundSpeedGradient) ||
      !std::isfinite(geometry.curvature) ||
      !std::isfinite(geometry.maximumIncidentPlaneDistance)) {
    throw ValidationError(
        "boundary-reflection geometry must contain only finite values");
  }
  if (geometry.maximumIncidentPlaneDistance < 0.0) {
    throw ValidationError(
        "boundary incident-plane tolerance must be non-negative");
  }

  const double collisionNormalNorm = norm(geometry.collisionPlaneOutwardNormal);
  if (std::abs(collisionNormalNorm - 1.0) > kFrameTolerance) {
    throw ValidationError("collision-plane outward normal must be unit length");
  }

  const double tangentNorm = norm(geometry.reflectionTangent);
  const double normalNorm = norm(geometry.reflectionOutwardNormal);
  if (tangentNorm <= std::numeric_limits<double>::min() ||
      normalNorm <= std::numeric_limits<double>::min()) {
    throw ValidationError("boundary-reflection frame vectors must be non-zero");
  }
  const double frameScale = std::max({1.0, tangentNorm, normalNorm});
  if (std::abs(tangentNorm - normalNorm) > kFrameTolerance * frameScale ||
      std::abs(fortranDotProduct2D(geometry.reflectionTangent,
                                   geometry.reflectionOutwardNormal)) >
          kFrameTolerance * tangentNorm * normalNorm) {
    throw ValidationError(
        "boundary-reflection tangent and normal must be orthogonal with "
        "equal norm");
  }
  if (geometry.reflectionTangent.range <= 0.0) {
    throw ValidationError(
        "boundary-reflection tangent must point toward increasing range");
  }

  const Vec2 expectedReflectionNormal =
      boundary == ReflectionBoundary::SeaSurface
          ? Vec2{.range = geometry.reflectionTangent.depth,
                 .depth = -geometry.reflectionTangent.range}
          : Vec2{.range = -geometry.reflectionTangent.depth,
                 .depth = geometry.reflectionTangent.range};
  if (norm(geometry.reflectionOutwardNormal - expectedReflectionNormal) >
      kFrameTolerance * frameScale) {
    throw ValidationError(
        "boundary-reflection normal has the wrong orientation");
  }

  if ((boundary == ReflectionBoundary::SeaSurface &&
       geometry.collisionPlaneOutwardNormal.depth >= 0.0) ||
      (boundary == ReflectionBoundary::Seabed &&
       geometry.collisionPlaneOutwardNormal.depth <= 0.0)) {
    throw ValidationError(
        "collision-plane outward normal has the wrong vertical orientation");
  }

  const double planeDistance = std::abs(
      fortranDotProduct2D(incidentPosition - geometry.collisionPlanePoint,
                          geometry.collisionPlaneOutwardNormal));
  if (planeDistance >
      geometry.maximumIncidentPlaneDistance + kPositionTolerance) {
    throw ValidationError(
        "incident reflection state is too far from the collision plane");
  }
}

[[nodiscard]] double dynamicJump(const RayState& incidentState,
                                 const RayState& reflectedState,
                                 ReflectionBoundary boundary,
                                 const BoundaryReflectionGeometry& geometry,
                                 double tangentSlowness, double normalSlowness,
                                 BoundaryCurvatureMode curvatureMode) {
  const Vec2 incidentUnitTangent =
      incidentState.soundSpeed * incidentState.slowness;
  const Vec2 reflectedUnitTangent =
      reflectedState.soundSpeed * reflectedState.slowness;

  const Vec2 incidentRayNormal{.range = -incidentUnitTangent.depth,
                               .depth = incidentUnitTangent.range};
  // ReflectMod.f90 reverses the orientation of the reflected ray's (t,n)
  // frame before evaluating the jump.
  const Vec2 reflectedRayNormal{.range = reflectedUnitTangent.depth,
                                .depth = -reflectedUnitTangent.range};

  double normalGradientJump = -fortranDotProduct2D(
      geometry.soundSpeedGradient, reflectedRayNormal - incidentRayNormal);
  const double tangentGradientJump = -fortranDotProduct2D(
      geometry.soundSpeedGradient, reflectedUnitTangent - incidentUnitTangent);
  if (boundary == ReflectionBoundary::SeaSurface) {
    normalGradientJump = -normalGradientJump;
  }

  const double soundSpeedSquared =
      incidentState.soundSpeed * incidentState.soundSpeed;
  double curvatureJump =
      (2.0 * geometry.curvature / soundSpeedSquared) / normalSlowness;
  if (boundary == ReflectionBoundary::SeaSurface) {
    curvatureJump = -curvatureJump;
  }

  const double incidenceRatio = tangentSlowness / normalSlowness;
  double jump = curvatureJump + incidenceRatio *
                                    (2.0 * normalGradientJump -
                                     incidenceRatio * tangentGradientJump) /
                                    soundSpeedSquared;

  switch (curvatureMode) {
    case BoundaryCurvatureMode::Standard:
      break;
    case BoundaryCurvatureMode::Double:
      jump *= 2.0;
      break;
    case BoundaryCurvatureMode::Zero:
      jump = 0.0;
      break;
    default:
      throw ValidationError("unknown dynamic reflection curvature mode");
  }

  if (!std::isfinite(jump)) {
    throw ValidationError("boundary dynamic reflection jump is non-finite");
  }
  return jump;
}

}  // namespace

FlatBoundaryReflection reflectAtBoundary(
    const RayState& incidentState, ReflectionBoundary boundary,
    const BoundaryReflectionGeometry& geometry, std::size_t rayPointIndex,
    BoundaryCurvatureMode curvatureMode) {
  const bool unitReflectionFrame =
      std::abs(norm(geometry.reflectionTangent) - 1.0) <= kFrameTolerance &&
      std::abs(norm(geometry.reflectionOutwardNormal) - 1.0) <= kFrameTolerance;
  validateState(incidentState, unitReflectionFrame);
  validateGeometry(boundary, geometry, incidentState.position);

  const double tangentSlowness =
      fortranDotProduct2D(incidentState.slowness, geometry.reflectionTangent);
  const double normalSlowness = fortranDotProduct2D(
      incidentState.slowness, geometry.reflectionOutwardNormal);
  const double unitNormalSlowness = incidentState.soundSpeed * normalSlowness;
  if (unitNormalSlowness <= kGrazingTolerance) {
    throw ValidationError(
        "incident ray must cross the boundary toward its exterior");
  }

  RayState reflectedState = incidentState;
  const double twiceNormalSlowness = 2.0 * normalSlowness;
  // Reflect2D is lowered to one fused subtract per component.  Keeping that
  // rounding point is necessary because the next boundary landing is tested
  // with a strict signed-distance comparison.
  reflectedState.slowness = {
      .range =
          std::fma(-twiceNormalSlowness, geometry.reflectionOutwardNormal.range,
                   incidentState.slowness.range),
      .depth =
          std::fma(-twiceNormalSlowness, geometry.reflectionOutwardNormal.depth,
                   incidentState.slowness.depth)};

  const double jump =
      dynamicJump(incidentState, reflectedState, boundary, geometry,
                  tangentSlowness, normalSlowness, curvatureMode);
  for (std::size_t index = 0; index < reflectedState.dynamicP.size(); ++index) {
    reflectedState.dynamicP[index] =
        incidentState.dynamicP[index] + incidentState.dynamicQ[index] * jump;
  }

  return FlatBoundaryReflection{
      .reflectedState = reflectedState,
      .event = ReflectionEvent{
          .rayPointIndex = rayPointIndex,
          .reflectedRayPointIndex = rayPointIndex + 1U,
          .boundary = boundary,
          .boundarySegmentIndex = geometry.segmentIndex,
          .boundaryCurvature = geometry.curvature,
          .position = incidentState.position,
          .boundaryTangent = geometry.reflectionTangent,
          .outwardNormal = geometry.reflectionOutwardNormal,
          .incidentSlowness = incidentState.slowness,
          .reflectedSlowness = reflectedState.slowness,
          .tangentSlowness = tangentSlowness,
          .normalSlowness = normalSlowness,
          .longMaterialOverride = std::nullopt,
      }};
}

FlatBoundaryReflection reflectAtFlatBoundary(
    const RayState& incidentState, ReflectionBoundary boundary,
    const FlatBoundaryGeometry& geometry, std::size_t rayPointIndex,
    BoundaryCurvatureMode curvatureMode) {
  return reflectAtBoundary(
      incidentState, boundary,
      BoundaryReflectionGeometry{
          .collisionPlanePoint = geometry.point,
          .collisionPlaneOutwardNormal = geometry.outwardNormal,
          .reflectionTangent = geometry.tangent,
          .reflectionOutwardNormal = geometry.outwardNormal,
          .soundSpeedGradient = geometry.soundSpeedGradient,
          .segmentIndex = geometry.segmentIndex,
          .curvature = geometry.curvature,
          .maximumIncidentPlaneDistance =
              geometry.maximumIncidentPlaneDistance},
      rayPointIndex, curvatureMode);
}

}  // namespace rayreuse
