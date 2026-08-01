#pragma once

#include <vector>

#include "rayreuse/model/c_linear_ssp.hpp"
#include "rayreuse/model/environment.hpp"
#include "rayreuse/model/simulation_case.hpp"
#include "rayreuse/ray/ray_path.hpp"

namespace rayreuse {

// Traces the frequency-independent centre ray and its two dynamic-ray
// fundamental solutions.
//
// C-linear SSP interfaces are aligned with the same reduced-step rule as the
// Fortran tracer. The segment hint remains on the arrival side at an exact
// node; the following minimum forward step moves into the adjacent segment and
// updates the hint. Flat sea-surface and seabed crossings retain the integrated
// incident point and append a same-position reflected point, so reflection
// transitions remain distinct from integrated StepQuadrature transitions.
class GeometryTracer {
 public:
  GeometryTracer(const Environment& environment, IntegratorSettings integrator);
  explicit GeometryTracer(const SimulationCase& simulation);

  [[nodiscard]] RayPath trace(const Source& source, double launchAngle) const;

 private:
  CLinearSsp soundSpeedProfile_;
  IntegratorSettings integrator_;
  std::vector<double> profileDepths_;
  double seaSurfaceDepth_{};
  double seabedDepth_{};
};

}  // namespace rayreuse
