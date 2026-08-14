#include "bellhop/field/beam_epsilon.hpp"

#include <cmath>
#include <complex>
#include <limits>
#include <numbers>
#include <string>
#include <string_view>

#include "bellhop/error.hpp"

namespace bellhop {
namespace {

void requireFinitePositive(double value, std::string_view name) {
  if (!std::isfinite(value) || value <= 0.0) {
    throw ValidationError(
        std::string(name) + " must be finite and positive");
  }
}

}  // namespace

BeamEpsilon pickMinimumWidthEpsilon(
    double frequency, double sourceSoundSpeed,
    double loopRangeMeters, double epsilonMultiplier) {
  return pickBeamEpsilon(
      BeamWidthMode::MinimumWidth, frequency, sourceSoundSpeed,
      0.0, 0.0, 1.0, loopRangeMeters, epsilonMultiplier);
}

BeamEpsilon pickBeamEpsilon(
    BeamWidthMode mode, double frequency, double sourceSoundSpeed,
    double sourceDepthGradient, double launchAngle,
    double launchAngleStep, double loopRangeMeters,
    double epsilonMultiplier) {
  requireFinitePositive(frequency, "frequency");
  requireFinitePositive(sourceSoundSpeed, "sourceSoundSpeed");
  requireFinitePositive(epsilonMultiplier, "epsilonMultiplier");
  if (!std::isfinite(sourceDepthGradient) ||
      !std::isfinite(launchAngle) ||
      !std::isfinite(launchAngleStep) ||
      !std::isfinite(loopRangeMeters)) {
    throw ValidationError("beam epsilon inputs must be finite");
  }

  const double angularFrequency =
      2.0 * std::numbers::pi * frequency;
  double halfWidth = 0.0;
  std::complex<double> optimal{};
  switch (mode) {
    case BeamWidthMode::SpaceFilling: {
      if (launchAngleStep <= 0.0) {
        throw ValidationError(
            "space-filling beam launch-angle step must be positive");
      }
      halfWidth =
          2.0 / ((angularFrequency / sourceSoundSpeed) * launchAngleStep);
      const double halfWidthSquared = halfWidth * halfWidth;
      optimal = {0.0, (0.5 * angularFrequency) * halfWidthSquared};
      break;
    }
    case BeamWidthMode::MinimumWidth: {
      requireFinitePositive(loopRangeMeters, "loopRangeMeters");
      // Preserve the original PickEpsilon operation order instead of
      // simplifying epsilon to c*rLoop. Squaring the rounded half width is
      // observable in the Fortran oracle at the final binary64 bit.
      halfWidth =
          std::sqrt(2.0 * sourceSoundSpeed * loopRangeMeters /
                    angularFrequency);
      const double halfWidthSquared = halfWidth * halfWidth;
      optimal = {0.0, (0.5 * angularFrequency) * halfWidthSquared};
      break;
    }
    case BeamWidthMode::Wkb:
      halfWidth = std::numeric_limits<double>::max();
      if (sourceDepthGradient == 0.0) {
        optimal = {1.0e10, 0.0};
      } else {
        optimal = {
            (-std::sin(launchAngle) /
             std::cos(launchAngle * launchAngle)) *
                sourceSoundSpeed * sourceSoundSpeed /
                sourceDepthGradient,
            0.0};
      }
      break;
    default:
      throw ValidationError("unknown beam-width mode");
  }

  if (!std::isfinite(angularFrequency) ||
      !std::isfinite(halfWidth) || halfWidth <= 0.0 ||
      !std::isfinite(optimal.real()) ||
      !std::isfinite(optimal.imag())) {
    throw ValidationError("beam epsilon intermediate values are invalid");
  }
  const std::complex<double> value =
      epsilonMultiplier * optimal;
  if (!std::isfinite(value.real()) ||
      !std::isfinite(value.imag())) {
    throw ValidationError("beam epsilon must be finite");
  }
  if (mode != BeamWidthMode::Wkb &&
      (value.real() != 0.0 || value.imag() <= 0.0)) {
    throw ValidationError(
        "space-filling/minimum-width epsilon must be positive imaginary");
  }

  return BeamEpsilon{
      .halfWidthMeters = halfWidth,
      .value = value};
}

}  // namespace bellhop
