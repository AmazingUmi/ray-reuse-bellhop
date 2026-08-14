#pragma once

#include <complex>

namespace bellhop {

enum class BeamWidthMode {
  SpaceFilling,
  MinimumWidth,
  Wkb,
};

struct BeamEpsilon {
  double halfWidthMeters{};
  std::complex<double> value{};
};

// PickEpsilon for the three Cartesian/Ray-centered Cerveny width policies.
// The source gradient is used only by WKB, the launch-angle spacing only by
// space-filling, and the loop range only by minimum-width.  Keeping one
// explicit interface prevents a caller from silently substituting the M
// branch for F or W while retaining the legacy arithmetic order.
[[nodiscard]] BeamEpsilon pickBeamEpsilon(
    BeamWidthMode mode, double frequency, double sourceSoundSpeed,
    double sourceDepthGradient, double launchAngle,
    double launchAngleStep, double loopRangeMeters,
    double epsilonMultiplier);

// Compatibility wrapper for the minimum-width branch. loopRangeMeters is
// already in the internal SI unit; the parser is responsible for converting
// Bellhop's input kilometers.
[[nodiscard]] BeamEpsilon pickMinimumWidthEpsilon(
    double frequency, double sourceSoundSpeed,
    double loopRangeMeters, double epsilonMultiplier);

}  // namespace bellhop
