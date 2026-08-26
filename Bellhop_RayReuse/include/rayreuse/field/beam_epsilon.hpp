#pragma once

#include <complex>

#include "rayreuse/model/beam_width.hpp"

namespace rayreuse {

struct BeamEpsilon {
  double halfWidthMeters{};
  std::complex<double> value{};
};

[[nodiscard]] BeamEpsilon pickBeamEpsilon(
    BeamWidthMode widthMode, double frequency, double sourceSoundSpeed,
    double sourceDepthGradient, double launchAngleRadians,
    double launchAngleStep, double loopRangeMeters, double epsilonMultiplier);

// Compatibility wrapper for the minimum-width branch. loopRangeMeters is
// already in the internal SI unit; the parser converts Bellhop input km.
[[nodiscard]] BeamEpsilon pickMinimumWidthEpsilon(double frequency,
                                                  double sourceSoundSpeed,
                                                  double loopRangeMeters,
                                                  double epsilonMultiplier);

}  // namespace rayreuse
