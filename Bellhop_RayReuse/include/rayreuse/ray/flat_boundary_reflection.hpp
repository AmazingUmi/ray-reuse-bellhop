#pragma once

#include <cstddef>

#include "rayreuse/numerics/vec2.hpp"
#include "rayreuse/ray/ray_path.hpp"

namespace rayreuse {

// ReflectMod.f90 allows the boundary-curvature contribution to the dynamic-ray
// jump to be doubled, retained, or suppressed.  Keep that choice explicit:
// it is a beam-model option rather than a property of the physical boundary.
enum class BoundaryCurvatureMode {
  Standard,
  Double,
  Zero,
};

struct FlatBoundaryGeometry {
  Vec2 point;
  Vec2 tangent;
  Vec2 outwardNormal;
  Vec2 soundSpeedGradient;
  std::size_t segmentIndex{};
  // Step2D enforces a minimum step and may leave the incident point slightly
  // beyond the ideal plane. The caller must state the accepted normal miss.
  double maximumIncidentPlaneDistance{};
};

struct FlatBoundaryReflection {
  RayState reflectedState;
  ReflectionEvent event;
};

// Applies frequency-independent mirror reflection and the flat-boundary
// CurvatureCorrection2 jump.  The incident state is the D-10 pre-reflection
// point; the returned state is the same-position, same-time post-reflection
// point.  Frequency-dependent amplitude, phase, and beam shift are excluded.
[[nodiscard]] FlatBoundaryReflection reflectAtFlatBoundary(
    const RayState& incidentState, ReflectionBoundary boundary,
    const FlatBoundaryGeometry& geometry, std::size_t rayPointIndex,
    BoundaryCurvatureMode curvatureMode);

}  // namespace rayreuse
