#include "bellhop/field/beam_epsilon.hpp"

#include <cmath>
#include <complex>
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
  requireFinitePositive(frequency, "frequency");
  requireFinitePositive(sourceSoundSpeed, "sourceSoundSpeed");
  requireFinitePositive(loopRangeMeters, "loopRangeMeters");
  requireFinitePositive(epsilonMultiplier, "epsilonMultiplier");

  const double angularFrequency =
      2.0 * std::numbers::pi * frequency;
  // Preserve the original PickEpsilon operation order instead of simplifying
  // epsilon to c*rLoop.  Squaring the rounded half width is observable in the
  // Fortran oracle at the final binary64 bit.
  const double halfWidth =
      std::sqrt(2.0 * sourceSoundSpeed * loopRangeMeters /
                angularFrequency);
  const double halfWidthSquared = halfWidth * halfWidth;
  if (!std::isfinite(angularFrequency) ||
      !std::isfinite(halfWidth) || halfWidth <= 0.0 ||
      !std::isfinite(halfWidthSquared) ||
      halfWidthSquared <= 0.0) {
    throw ValidationError(
        "minimum-width beam intermediate values must be finite "
        "and positive");
  }
  const std::complex<double> optimal{
      0.0,
      (0.5 * angularFrequency) * halfWidthSquared};
  const std::complex<double> value =
      epsilonMultiplier * optimal;
  if (!std::isfinite(value.real()) ||
      !std::isfinite(value.imag()) || value.imag() <= 0.0) {
    throw ValidationError(
        "minimum-width beam epsilon must be finite");
  }

  return BeamEpsilon{
      .halfWidthMeters = halfWidth,
      .value = value};
}

}  // namespace bellhop
