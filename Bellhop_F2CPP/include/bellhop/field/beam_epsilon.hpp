#pragma once

#include <complex>

namespace bellhop {

struct BeamEpsilon {
  double halfWidthMeters{};
  std::complex<double> value{};
};

// PickEpsilon's minimum-width branch for the supported Cartesian Cerveny
// "CM" beam type.  loopRangeMeters is already in the internal SI unit; the
// parser is responsible for converting Bellhop's input kilometers.
[[nodiscard]] BeamEpsilon pickMinimumWidthEpsilon(
    double frequency, double sourceSoundSpeed,
    double loopRangeMeters, double epsilonMultiplier);

}  // namespace bellhop
