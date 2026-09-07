#pragma once

#include <vector>

#include "broadband/model/beam_curvature.hpp"
#include "broadband/model/environment.hpp"
#include "broadband/model/simulation_case.hpp"
#include "broadband/model/sound_speed_evaluator.hpp"
#include "broadband/ray/ray_path.hpp"

namespace broadband {

// Traces the frequency-independent centre ray and its two dynamic-ray
// fundamental solutions.
//
// SSP depth-node interfaces (C-linear and N²-linear) are aligned with the same
// reduced-step rule as the Fortran tracer; PCHIP nodes share the rule without a
// gradient jump. The segment hint remains on the arrival side at an exact
// node; the following minimum forward step moves into the adjacent segment and
// updates the hint. Flat sea-surface and seabed crossings retain the integrated
// incident point and append a same-position reflected point, so reflection
// transitions remain distinct from integrated StepQuadrature transitions.
class GeometryTracer {
 public:
  GeometryTracer(
      const Environment& environment, IntegratorSettings integrator,
      BoundaryCurvatureMode curvatureMode = BoundaryCurvatureMode::Standard);
  explicit GeometryTracer(const SimulationCase& simulation);

  [[nodiscard]] RayPath trace(const Source& source, double launchAngle) const;

 private:
  GeometrySspEvaluator soundSpeedProfile_;
  IntegratorSettings integrator_;
  std::vector<double> profileDepths_;
  BoundaryModel seaSurfaceBoundary_;
  BoundaryModel seabedBoundary_;
  BoundaryCurvatureMode curvatureMode_{BoundaryCurvatureMode::Standard};
};

}  // namespace broadband
