#pragma once

#include <cstddef>

#include "rayreuse/model/beam_curvature.hpp"
#include "rayreuse/numerics/vec2.hpp"
#include "rayreuse/ray/ray_path.hpp"

namespace rayreuse {

struct FlatBoundaryGeometry {
  Vec2 point;
  Vec2 tangent;
  Vec2 outwardNormal;
  Vec2 soundSpeedGradient;
  std::size_t segmentIndex{};
  double curvature{};
  // Step2D enforces a minimum step and may leave the incident point slightly
  // beyond the ideal plane. The caller must state the accepted normal miss.
  double maximumIncidentPlaneDistance{};
};

// Reflect2D uses two related but distinct boundary frames for curvilinear
// boundaries. Step2D and Distances2D land on and test the piecewise-linear
// chord plane, while reflection uses an interpolated legacy tangent/normal
// frame. The latter is orthogonal but intentionally is not renormalized.
struct BoundaryReflectionGeometry {
  Vec2 collisionPlanePoint;
  Vec2 collisionPlaneOutwardNormal;
  Vec2 reflectionTangent;
  Vec2 reflectionOutwardNormal;
  Vec2 soundSpeedGradient;
  std::size_t segmentIndex{};
  double curvature{};
  double maximumIncidentPlaneDistance{};
};

struct FlatBoundaryReflection {
  RayState reflectedState;
  ReflectionEvent event;
};

// Applies the legacy Reflect2D mirror and CurvatureCorrection2 formulas. The
// reflection frame may have a common norm other than one, as produced by the
// curvilinear Nodet/Noden interpolation in BdryMod.f90.
[[nodiscard]] FlatBoundaryReflection reflectAtBoundary(
    const RayState& incidentState, ReflectionBoundary boundary,
    const BoundaryReflectionGeometry& geometry, std::size_t rayPointIndex,
    BoundaryCurvatureMode curvatureMode);

// Applies frequency-independent mirror reflection and the flat-boundary
// CurvatureCorrection2 jump.  The incident state is the D-10 pre-reflection
// point; the returned state is the same-position, same-time post-reflection
// point.  Frequency-dependent amplitude, phase, and beam shift are excluded.
[[nodiscard]] FlatBoundaryReflection reflectAtFlatBoundary(
    const RayState& incidentState, ReflectionBoundary boundary,
    const FlatBoundaryGeometry& geometry, std::size_t rayPointIndex,
    BoundaryCurvatureMode curvatureMode);

}  // namespace rayreuse
