#pragma once

#include "rayreuse/model/sound_speed_types.hpp"
#include "rayreuse/numerics/vec2.hpp"

namespace rayreuse {

// Returns c_nn / c^2 for a ray whose slowness is (xi, zeta).
//
// The c^2 denominator is already represented by the two slowness factors:
// the unit ray normal is c * (-zeta, xi). Keeping this Fortran Step.f90 form
// avoids an unnecessary sound-speed argument and is directly testable.
[[nodiscard]] constexpr double soundSpeedNormalSecondDerivativeOverSquaredSpeed(
    const SoundSpeedHessian& hessian, Vec2 slowness) noexcept {
  const double xi = slowness.range;
  const double zeta = slowness.depth;
  return hessian.rangeRange * zeta * zeta -
         2.0 * hessian.rangeDepth * xi * zeta + hessian.depthDepth * xi * xi;
}

}  // namespace rayreuse
