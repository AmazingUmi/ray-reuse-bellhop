#include "rayreuse/acoustics/boundary_acoustics.hpp"

#include <cmath>
#include <complex>
#include <numbers>
#include <string>
#include <string_view>

#include "rayreuse/acoustics/attenuation.hpp"
#include "rayreuse/error.hpp"

namespace rayreuse {
namespace {

constexpr double kLegacyCoefficientKillThreshold = static_cast<double>(1.0e-5F);

void requireFinite(double value, std::string_view name) {
  if (!std::isfinite(value)) {
    throw ValidationError(std::string(name) + " must be finite");
  }
}

void requireFinitePositive(double value, std::string_view name) {
  requireFinite(value, name);
  if (value <= 0.0) {
    throw ValidationError(std::string(name) + " must be positive");
  }
}

[[nodiscard]] bool finiteComplex(std::complex<double> value) noexcept {
  return std::isfinite(value.real()) && std::isfinite(value.imag());
}

[[nodiscard]] std::complex<double> fluidHalfSpaceCoefficient(
    const AcousticMaterial& material, double frequency, double waterDensity,
    double tangentSlowness, double outwardNormalSlowness) {
  if (material.shearSoundSpeed != 0.0) {
    throw ValidationError(
        "elastic acoustic half-spaces are not supported by M2-02");
  }

  const AttenuationConversion compressionalAttenuation =
      convertAttenuation(material.compressionalAttenuation, frequency,
                         material.compressionalSoundSpeed);
  const std::complex<double> compressionalSoundSpeed{
      material.compressionalSoundSpeed,
      compressionalAttenuation.imaginarySoundSpeed};

  const double angularFrequency = 2.0 * std::numbers::pi * frequency;
  const double tangentWavenumber = angularFrequency * tangentSlowness;
  const double normalWavenumber = angularFrequency * outwardNormalSlowness;
  const std::complex<double> materialWavenumber =
      angularFrequency / compressionalSoundSpeed;

  std::complex<double> verticalWavenumber =
      std::sqrt(tangentWavenumber * tangentWavenumber -
                materialWavenumber * materialWavenumber);
  // Retain the explicit correction in ReflectMod.f90 for compiler branch
  // differences on a negative-real square-root argument. std::complex can
  // preserve a signed zero and produce a tiny real part for the lossless
  // form, so the exact-lossless case selects the same positive-imaginary
  // branch explicitly.
  if ((verticalWavenumber.real() == 0.0 ||
       compressionalAttenuation.imaginarySoundSpeed == 0.0) &&
      verticalWavenumber.imag() < 0.0) {
    verticalWavenumber = -verticalWavenumber;
  }

  const std::complex<double> imaginaryNormalImpedance{
      0.0, normalWavenumber * material.density};
  const std::complex<double> waterTerm = waterDensity * verticalWavenumber;
  const std::complex<double> denominator = waterTerm + imaginaryNormalImpedance;
  if (denominator == std::complex<double>{}) {
    throw ValidationError(
        "acoustic half-space reflection denominator must not be zero");
  }

  const std::complex<double> result =
      -(waterTerm - imaginaryNormalImpedance) / denominator;
  if (!finiteComplex(result)) {
    throw ValidationError(
        "acoustic half-space reflection coefficient must be finite");
  }
  return result;
}

}  // namespace

BoundaryAcousticsResult classifyBoundaryCoefficient(
    std::complex<double> rawCoefficient,
    bool suppressSmallAcousticCoefficient) {
  if (!finiteComplex(rawCoefficient)) {
    throw ValidationError(
        "raw reflection coefficient must contain only finite values");
  }

  const double magnitude = std::abs(rawCoefficient);
  const bool suppressed = suppressSmallAcousticCoefficient &&
                          magnitude < kLegacyCoefficientKillThreshold;
  if (suppressed) {
    return BoundaryAcousticsResult{.rawCoefficient = rawCoefficient,
                                   .amplitudeMultiplier = 0.0,
                                   .phaseIncrement = 0.0,
                                   .coefficientSuppressed = true};
  }

  return BoundaryAcousticsResult{
      .rawCoefficient = rawCoefficient,
      .amplitudeMultiplier = magnitude,
      .phaseIncrement =
          std::atan2(rawCoefficient.imag(), rawCoefficient.real()),
      .coefficientSuppressed = false};
}

BoundaryAcousticsResult evaluateBoundaryAcoustics(
    const BoundaryModel& boundary, double frequency, double waterDensity,
    double tangentSlowness, double outwardNormalSlowness) {
  requireFinitePositive(frequency, "frequency");
  requireFinitePositive(waterDensity, "waterDensity");
  requireFinite(tangentSlowness, "tangentSlowness");
  requireFinite(outwardNormalSlowness, "outwardNormalSlowness");
  if (outwardNormalSlowness <= 0.0) {
    throw ValidationError(
        "outwardNormalSlowness must be positive at a reflection");
  }

  switch (boundary.kind()) {
    case BoundaryKind::Vacuum:
      return classifyBoundaryCoefficient({-1.0, 0.0}, false);
    case BoundaryKind::Rigid:
      return classifyBoundaryCoefficient({1.0, 0.0}, false);
    case BoundaryKind::AcousticHalfSpace:
      if (!boundary.material().has_value()) {
        throw ValidationError(
            "acoustic half-space is missing material properties");
      }
      return classifyBoundaryCoefficient(
          fluidHalfSpaceCoefficient(*boundary.material(), frequency,
                                    waterDensity, tangentSlowness,
                                    outwardNormalSlowness),
          true);
  }

  throw ValidationError("unsupported boundary kind");
}

}  // namespace rayreuse
