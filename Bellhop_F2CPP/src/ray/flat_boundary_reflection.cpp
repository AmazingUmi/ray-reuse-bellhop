#include "bellhop/ray/flat_boundary_reflection.hpp"

#include <array>
#include <cmath>

#include "bellhop/error.hpp"

namespace bellhop {
namespace {

constexpr double kFrameTolerance = 1.0e-10;
// Modified-box integration does not explicitly renormalize slowness.  Long
// refracted paths can therefore reach a valid boundary with O(1e-6) drift in
// c*|t|, as the original Fortran Munk cases do.  Keep a guard against corrupt
// states without rejecting that accumulated integration error.
constexpr double kSlownessNormTolerance = 1.0e-4;
constexpr double kPositionTolerance = 1.0e-8;
constexpr double kGrazingTolerance = 1.0e-12;

[[nodiscard]] bool finiteArray(
    const std::array<double, 2>& values) noexcept {
  return std::isfinite(values[0]) && std::isfinite(values[1]);
}

void validateState(const RayState& state) {
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
  if (std::abs(state.soundSpeed * norm(state.slowness) - 1.0) >
      kSlownessNormTolerance) {
    throw ValidationError(
        "incident slowness norm must be consistent with sound speed");
  }
}

void validateGeometry(ReflectionBoundary boundary,
                      const FlatBoundaryGeometry& geometry,
                      Vec2 incidentPosition) {
  if (!isFinite(geometry.point) || !isFinite(geometry.tangent) ||
      !isFinite(geometry.outwardNormal) ||
      !isFinite(geometry.soundSpeedGradient) ||
      !std::isfinite(geometry.maximumIncidentPlaneDistance)) {
    throw ValidationError(
        "flat-boundary geometry must contain only finite values");
  }
  if (geometry.maximumIncidentPlaneDistance < 0.0) {
    throw ValidationError(
        "flat-boundary incident-plane tolerance must be non-negative");
  }
  if (std::abs(norm(geometry.tangent) - 1.0) > kFrameTolerance ||
      std::abs(norm(geometry.outwardNormal) - 1.0) >
          kFrameTolerance ||
      std::abs(dot(geometry.tangent, geometry.outwardNormal)) >
          kFrameTolerance) {
    throw ValidationError(
        "flat-boundary tangent and outward normal must be orthonormal");
  }
  if (geometry.tangent.range <= 0.0) {
    throw ValidationError(
        "flat-boundary tangent must point toward increasing range");
  }
  if ((boundary == ReflectionBoundary::SeaSurface &&
       geometry.outwardNormal.depth >= 0.0) ||
      (boundary == ReflectionBoundary::Seabed &&
       geometry.outwardNormal.depth <= 0.0)) {
    throw ValidationError(
        "flat-boundary outward normal has the wrong vertical orientation");
  }

  const double planeDistance =
      std::abs(dot(incidentPosition - geometry.point,
                   geometry.outwardNormal));
  if (planeDistance >
      geometry.maximumIncidentPlaneDistance + kPositionTolerance) {
    throw ValidationError(
        "incident reflection state is too far from the flat boundary");
  }
}

[[nodiscard]] double dynamicJump(
    const RayState& incidentState, const RayState& reflectedState,
    ReflectionBoundary boundary, const FlatBoundaryGeometry& geometry,
    double tangentSlowness, double normalSlowness,
    BoundaryCurvatureMode curvatureMode) {
  const Vec2 incidentUnitTangent =
      incidentState.soundSpeed * incidentState.slowness;
  const Vec2 reflectedUnitTangent =
      reflectedState.soundSpeed * reflectedState.slowness;

  const Vec2 incidentRayNormal{
      .range = -incidentUnitTangent.depth,
      .depth = incidentUnitTangent.range};
  // ReflectMod.f90 reverses the orientation of the reflected ray's (t,n)
  // frame before evaluating the jump.
  const Vec2 reflectedRayNormal{
      .range = reflectedUnitTangent.depth,
      .depth = -reflectedUnitTangent.range};

  double normalGradientJump =
      -dot(geometry.soundSpeedGradient,
           reflectedRayNormal - incidentRayNormal);
  const double tangentGradientJump =
      -dot(geometry.soundSpeedGradient,
           reflectedUnitTangent - incidentUnitTangent);
  if (boundary == ReflectionBoundary::SeaSurface) {
    normalGradientJump = -normalGradientJump;
  }

  const double incidenceRatio = tangentSlowness / normalSlowness;
  double jump =
      incidenceRatio *
      (2.0 * normalGradientJump -
       incidenceRatio * tangentGradientJump) /
      (incidentState.soundSpeed * incidentState.soundSpeed);

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
      throw ValidationError(
          "unknown dynamic reflection curvature mode");
  }

  if (!std::isfinite(jump)) {
    throw ValidationError(
        "flat-boundary dynamic reflection jump is non-finite");
  }
  return jump;
}

}  // namespace

FlatBoundaryReflection reflectAtFlatBoundary(
    const RayState& incidentState, ReflectionBoundary boundary,
    const FlatBoundaryGeometry& geometry, std::size_t rayPointIndex,
    BoundaryCurvatureMode curvatureMode) {
  validateState(incidentState);
  validateGeometry(boundary, geometry, incidentState.position);

  const double tangentSlowness =
      dot(incidentState.slowness, geometry.tangent);
  const double normalSlowness =
      dot(incidentState.slowness, geometry.outwardNormal);
  const double unitNormalSlowness =
      incidentState.soundSpeed * normalSlowness;
  if (unitNormalSlowness <= kGrazingTolerance) {
    throw ValidationError(
        "incident ray must cross the flat boundary toward its exterior");
  }

  RayState reflectedState = incidentState;
  reflectedState.slowness =
      incidentState.slowness -
      2.0 * normalSlowness * geometry.outwardNormal;

  const double jump =
      dynamicJump(incidentState, reflectedState, boundary, geometry,
                  tangentSlowness, normalSlowness, curvatureMode);
  for (std::size_t index = 0; index < reflectedState.dynamicP.size();
       ++index) {
    reflectedState.dynamicP[index] =
        incidentState.dynamicP[index] +
        incidentState.dynamicQ[index] * jump;
  }

  return FlatBoundaryReflection{
      .reflectedState = reflectedState,
      .event =
          ReflectionEvent{
              .rayPointIndex = rayPointIndex,
              .boundary = boundary,
              .boundarySegmentIndex = geometry.segmentIndex,
              .position = incidentState.position,
              .boundaryTangent = geometry.tangent,
              .outwardNormal = geometry.outwardNormal,
              .incidentSlowness = incidentState.slowness,
              .reflectedSlowness = reflectedState.slowness,
              .tangentSlowness = tangentSlowness,
              .normalSlowness = normalSlowness,
          }};
}

}  // namespace bellhop
