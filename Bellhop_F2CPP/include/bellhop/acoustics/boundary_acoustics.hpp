#pragma once

#include <complex>

#include "bellhop/model/environment.hpp"

namespace bellhop {

struct BoundaryAcousticsResult {
  // The unsuppressed pressure reflection coefficient remains available even
  // when the legacy small-coefficient rule kills the propagated ray.
  std::complex<double> rawCoefficient{};
  double amplitudeMultiplier{};
  double phaseIncrement{};
  bool coefficientSuppressed{};
};

// Applies the legacy amplitude/phase semantics to an already computed raw
// coefficient. Small-coefficient suppression is enabled only for acoustic
// half-spaces; vacuum and rigid coefficients are never suppressed.
[[nodiscard]] BoundaryAcousticsResult classifyBoundaryCoefficient(
    std::complex<double> rawCoefficient,
    bool suppressSmallAcousticCoefficient);

// Evaluates the per-frequency coefficient for a frozen geometry event.
// Slowness components are projections on the boundary tangent and outward
// normal. M2-02 accepts vacuum, rigid, and fluid acoustic half-spaces;
// elastic half-spaces fail explicitly.
[[nodiscard]] BoundaryAcousticsResult evaluateBoundaryAcoustics(
    const BoundaryModel& boundary, double frequency,
    double waterDensity, double tangentSlowness,
    double outwardNormalSlowness);

}  // namespace bellhop
