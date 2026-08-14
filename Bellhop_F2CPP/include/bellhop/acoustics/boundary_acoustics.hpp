#pragma once

#include <complex>
#include <cstddef>

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

[[nodiscard]] BoundaryAcousticsResult evaluateFluidHalfSpaceAcoustics(
    const AcousticMaterial& material, double attenuationEvaluationDepth,
    const VolumeAttenuation& volumeAttenuation, double frequency,
    double waterDensity, double tangentSlowness,
    double outwardNormalSlowness);

[[nodiscard]] BoundaryAcousticsResult evaluateAcousticHalfSpaceAcoustics(
    const AcousticMaterial& material, double attenuationEvaluationDepth,
    const VolumeAttenuation& volumeAttenuation, double frequency,
    double waterDensity, double tangentSlowness,
    double outwardNormalSlowness);

// Expands Origin's `G` boundary into its local, fluid half-space at each
// reflection.  The effective compressional speed depends on the water sound
// speed at the frozen event, so this conversion must remain in the frequency
// projection stage rather than in the geometry cache.
[[nodiscard]] BoundaryAcousticsResult evaluateGrainSizeHalfSpaceAcoustics(
    const GrainSizeMaterial& material, double frequency,
    double waterSoundSpeed, double waterDensity, double tangentSlowness,
    double outwardNormalSlowness);

// Reproduces Origin's bottom `F` table semantics. Magnitude and unwrapped
// phase are interpolated independently against the folded grazing angle.
// Keeping phase explicit is significant when the interpolated magnitude is
// zero: Origin still adds the table phase to the ray state.
[[nodiscard]] BoundaryAcousticsResult evaluateTabulatedReflectionAcoustics(
    const TabulatedReflectionTable& table, double tangentSlowness,
    double outwardNormalSlowness);

// Evaluates the per-frequency coefficient for a frozen geometry event.
// Slowness components are projections on the boundary tangent and outward
// normal. Vacuum, rigid, tabulated, fluid, and elastic boundaries are
// evaluated without changing the frozen geometry event.
[[nodiscard]] BoundaryAcousticsResult evaluateBoundaryAcoustics(
    const BoundaryModel& boundary, double frequency,
    double waterDensity, double tangentSlowness,
    double outwardNormalSlowness);
[[nodiscard]] BoundaryAcousticsResult evaluateBoundaryAcoustics(
    const BoundaryModel& boundary,
    const VolumeAttenuation& volumeAttenuation, double frequency,
    double waterDensity, double tangentSlowness,
    double outwardNormalSlowness);
[[nodiscard]] BoundaryAcousticsResult evaluateBoundaryAcoustics(
    const BoundaryModel& boundary, std::size_t boundarySegmentIndex,
    const VolumeAttenuation& volumeAttenuation, double frequency,
    double waterDensity, double tangentSlowness,
    double outwardNormalSlowness);

}  // namespace bellhop
