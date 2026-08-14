#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "bellhop/numerics/vec2.hpp"
#include "bellhop/model/environment.hpp"

namespace bellhop {

struct RayState {
  Vec2 position;
  Vec2 slowness;
  std::array<double, 2> dynamicP{};
  std::array<double, 2> dynamicQ{};
  double soundSpeed{};
  double realTravelTime{};
};

struct StepQuadrature {
  double stepLength{};
  double startWeight{};
  double midpointWeight{};
  Vec2 midpoint;
};

enum class ReflectionBoundary {
  SeaSurface,
  Seabed,
};

struct FrozenBoundaryMaterial {
  AcousticMaterial material;
  double attenuationEvaluationDepth{};
};

struct ReflectionEvent {
  // The incident and reflected states are distinct same-position points.
  // Keeping both indices explicit prevents consumers from reconstructing the
  // D-10 transition from vector adjacency.
  std::size_t rayPointIndex{};
  std::size_t reflectedRayPointIndex{};
  ReflectionBoundary boundary{ReflectionBoundary::SeaSurface};
  std::size_t boundarySegmentIndex{};
  double boundaryCurvature{};
  Vec2 position;
  Vec2 boundaryTangent;
  Vec2 outwardNormal;
  Vec2 incidentSlowness;
  Vec2 reflectedSlowness;
  double tangentSlowness{};
  double normalSlowness{};
  std::optional<FrozenBoundaryMaterial> longMaterialOverride;
};

enum class RayTerminationReason {
  ExitedDomain,
  NumericalFailure,
  PointLimit,
};

struct RayPath {
  double launchAngle{};
  std::vector<RayState> points;
  std::vector<StepQuadrature> steps;
  std::vector<ReflectionEvent> events;
  RayTerminationReason terminationReason{RayTerminationReason::ExitedDomain};
  std::string terminationDetail;
};

}  // namespace bellhop
