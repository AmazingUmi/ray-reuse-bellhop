#include "rayreuse/field/beam_epsilon.hpp"

#include <cmath>
#include <complex>
#include <limits>
#include <numbers>
#include <string>
#include <string_view>

#include "rayreuse/error.hpp"

namespace rayreuse {
namespace {

void requireFinitePositive(double value, std::string_view name) {
  if (!std::isfinite(value) || value <= 0.0) {
    throw ValidationError(std::string(name) + " must be finite and positive");
  }
}

}  // namespace

BeamEpsilon pickMinimumWidthEpsilon(double frequency, double sourceSoundSpeed,
                                    double loopRangeMeters,
                                    double epsilonMultiplier) {
  return pickBeamEpsilon(BeamWidthMode::MinimumWidth, frequency,
                         sourceSoundSpeed, 0.0, 0.0, 1.0, loopRangeMeters,
                         epsilonMultiplier);
}

BeamEpsilon pickBeamEpsilon(BeamWidthMode widthMode, double frequency,
                            double sourceSoundSpeed, double sourceDepthGradient,
                            double launchAngleRadians, double launchAngleStep,
                            double loopRangeMeters, double epsilonMultiplier) {
  requireFinitePositive(frequency, "frequency");
  requireFinitePositive(sourceSoundSpeed, "sourceSoundSpeed");
  requireFinitePositive(epsilonMultiplier, "epsilonMultiplier");
  if (!std::isfinite(sourceDepthGradient) ||
      !std::isfinite(launchAngleRadians) || !std::isfinite(launchAngleStep) ||
      !std::isfinite(loopRangeMeters)) {
    throw ValidationError("beam epsilon inputs must be finite");
  }

  const double angularFrequency = 2.0 * std::numbers::pi * frequency;
  double halfWidth{};
  std::complex<double> optimal{};
  switch (widthMode) {
    case BeamWidthMode::SpaceFilling: {
      if (launchAngleStep <= 0.0) {
        throw ValidationError(
            "space-filling launch-angle step must be positive");
      }
      halfWidth =
          2.0 / ((angularFrequency / sourceSoundSpeed) * launchAngleStep);
      const double halfWidthSquared = halfWidth * halfWidth;
      optimal = {0.0, (0.5 * angularFrequency) * halfWidthSquared};
      break;
    }
    case BeamWidthMode::MinimumWidth: {
      requireFinitePositive(loopRangeMeters, "loopRangeMeters");
      // Preserve PickEpsilon's rounded sqrt -> square sequence. Simplifying
      // this to c*rLoop changes the final binary64 bit in existing oracles.
      halfWidth = std::sqrt(2.0 * sourceSoundSpeed * loopRangeMeters /
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
        // Preserve Origin's observable legacy expression: cos(alpha**2),
        // rather than the theoretically tempting cos(alpha)**2 rewrite.
        optimal = {(-std::sin(launchAngleRadians) /
                    std::cos(launchAngleRadians * launchAngleRadians)) *
                       sourceSoundSpeed * sourceSoundSpeed /
                       sourceDepthGradient,
                   0.0};
      }
      break;
    default:
      throw ValidationError("unknown beam-width mode");
  }
  if (!std::isfinite(angularFrequency) || !std::isfinite(halfWidth) ||
      halfWidth <= 0.0 || !std::isfinite(optimal.real()) ||
      !std::isfinite(optimal.imag())) {
    throw ValidationError("beam epsilon intermediate values are invalid");
  }
  const std::complex<double> value = epsilonMultiplier * optimal;
  if (!std::isfinite(value.real()) || !std::isfinite(value.imag())) {
    throw ValidationError("beam epsilon must be finite");
  }
  if (widthMode != BeamWidthMode::Wkb &&
      (value.real() != 0.0 || value.imag() <= 0.0)) {
    throw ValidationError(
        "space-filling/minimum-width epsilon must be positive imaginary");
  }

  return BeamEpsilon{.halfWidthMeters = halfWidth, .value = value};
}

}  // namespace rayreuse
