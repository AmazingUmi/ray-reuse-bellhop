#pragma once

#include <complex>
#include <cstddef>

#include "rayreuse/model/environment.hpp"

namespace rayreuse {

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
    std::complex<double> rawCoefficient, bool suppressSmallAcousticCoefficient);

[[nodiscard]] BoundaryAcousticsResult evaluateFluidHalfSpaceAcoustics(
    const AcousticMaterial& material, double frequency, double waterDensity,
    double tangentSlowness, double outwardNormalSlowness);
[[nodiscard]] BoundaryAcousticsResult evaluateFluidHalfSpaceAcoustics(
    const AcousticMaterial& material, double attenuationEvaluationDepth,
    const VolumeAttenuation& volumeAttenuation, double frequency,
    double waterDensity, double tangentSlowness, double outwardNormalSlowness);

[[nodiscard]] BoundaryAcousticsResult evaluateAcousticHalfSpaceAcoustics(
    const AcousticMaterial& material, double frequency, double waterDensity,
    double tangentSlowness, double outwardNormalSlowness);
[[nodiscard]] BoundaryAcousticsResult evaluateAcousticHalfSpaceAcoustics(
    const AcousticMaterial& material, double attenuationEvaluationDepth,
    const VolumeAttenuation& volumeAttenuation, double frequency,
    double waterDensity, double tangentSlowness, double outwardNormalSlowness);

[[nodiscard]] BoundaryAcousticsResult evaluateGrainSizeHalfSpaceAcoustics(
    const GrainSizeMaterial& material, double frequency, double waterSoundSpeed,
    double waterDensity, double tangentSlowness, double outwardNormalSlowness);

[[nodiscard]] BoundaryAcousticsResult evaluateTabulatedReflectionAcoustics(
    const TabulatedReflectionTable& table, double tangentSlowness,
    double outwardNormalSlowness);

// Evaluates the per-frequency coefficient for a frozen geometry event.
// Slowness components are projections on the boundary tangent and outward
// normal. Reflection coefficients remain frequency-local and never mutate the
// boundary model or frozen ReflectionEvent.
[[nodiscard]] BoundaryAcousticsResult evaluateBoundaryAcoustics(
    const BoundaryModel& boundary, double frequency, double waterDensity,
    double tangentSlowness, double outwardNormalSlowness);
[[nodiscard]] BoundaryAcousticsResult evaluateBoundaryAcoustics(
    const BoundaryModel& boundary, const VolumeAttenuation& volumeAttenuation,
    double frequency, double waterDensity, double tangentSlowness,
    double outwardNormalSlowness);
[[nodiscard]] BoundaryAcousticsResult evaluateBoundaryAcoustics(
    const BoundaryModel& boundary, std::size_t boundarySegmentIndex,
    double frequency, double waterDensity, double tangentSlowness,
    double outwardNormalSlowness);
[[nodiscard]] BoundaryAcousticsResult evaluateBoundaryAcoustics(
    const BoundaryModel& boundary, std::size_t boundarySegmentIndex,
    const VolumeAttenuation& volumeAttenuation, double frequency,
    double waterDensity, double tangentSlowness,
    double outwardNormalSlowness);

}  // namespace rayreuse
