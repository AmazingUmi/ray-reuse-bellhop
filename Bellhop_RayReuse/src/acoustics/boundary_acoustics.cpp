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

void validateRawAttenuation(const RawAttenuation& attenuation,
                            std::string_view name) {
  requireFinite(attenuation.value, std::string(name) + ".value");
  requireFinite(attenuation.referenceFrequency,
                std::string(name) + ".referenceFrequency");
  requireFinite(attenuation.powerLawExponent,
                std::string(name) + ".powerLawExponent");
  requireFinite(attenuation.transitionFrequency,
                std::string(name) + ".transitionFrequency");
  if (attenuation.value < 0.0 || attenuation.referenceFrequency <= 0.0 ||
      attenuation.transitionFrequency <= 0.0) {
    throw ValidationError(std::string(name) + " is invalid");
  }
  switch (attenuation.unit) {
    case AttenuationUnit::NepersPerMeter:
    case AttenuationUnit::DecibelsPerMeter:
    case AttenuationUnit::DecibelsPerMeterPowerLaw:
    case AttenuationUnit::DecibelsPerMeterKilohertz:
    case AttenuationUnit::DecibelsPerWavelength:
    case AttenuationUnit::QualityFactor:
    case AttenuationUnit::LossParameter:
      break;
    default:
      throw ValidationError(std::string(name) + ".unit is invalid");
  }
}

[[nodiscard]] bool finiteComplex(std::complex<double> value) noexcept {
  return std::isfinite(value.real()) && std::isfinite(value.imag());
}

[[nodiscard]] std::complex<double> legacyVerticalRoot(
    std::complex<double> squaredWavenumber,
    bool explicitlyCorrectNegativeImaginaryBranch, bool lossless = false) {
  std::complex<double> root = std::sqrt(squaredWavenumber);
  if (explicitlyCorrectNegativeImaginaryBranch &&
      (root.real() == 0.0 || lossless) && root.imag() < 0.0) {
    root = -root;
  }
  return root;
}

[[nodiscard]] std::complex<double> halfSpaceCoefficient(
    const AcousticMaterial& material, double frequency, double waterDensity,
    double tangentSlowness, double outwardNormalSlowness) {
  if (material.shearSoundSpeed == 0.0 &&
      material.shearAttenuation.value != 0.0) {
    throw ValidationError("zero shear speed requires zero shear attenuation");
  }

  const AttenuationConversion compressionalAttenuation =
      convertAttenuation(material.compressionalAttenuation, frequency,
                         material.compressionalSoundSpeed);
  const std::complex<double> compressionalSoundSpeed{
      material.compressionalSoundSpeed,
      compressionalAttenuation.imaginarySoundSpeed};

  const double angularFrequency = 2.0 * std::numbers::pi * frequency;
  const std::complex<double> tangentWavenumber{
      angularFrequency * tangentSlowness, 0.0};
  const double normalWavenumber = angularFrequency * outwardNormalSlowness;
  const std::complex<double> tangentWavenumberSquared =
      tangentWavenumber * tangentWavenumber;

  std::complex<double> f;
  std::complex<double> g;
  if (material.shearSoundSpeed > 0.0) {
    const AttenuationConversion shearAttenuation = convertAttenuation(
        material.shearAttenuation, frequency, material.shearSoundSpeed);
    const std::complex<double> shearSoundSpeed{
        material.shearSoundSpeed, shearAttenuation.imaginarySoundSpeed};
    const std::complex<double> shearWavenumber =
        angularFrequency / shearSoundSpeed;
    const std::complex<double> compressionalWavenumber =
        angularFrequency / compressionalSoundSpeed;
    const std::complex<double> shearVerticalSquared =
        tangentWavenumberSquared - shearWavenumber * shearWavenumber;
    const std::complex<double> compressionalVerticalSquared =
        tangentWavenumberSquared -
        compressionalWavenumber * compressionalWavenumber;
    const std::complex<double> shearVertical =
        legacyVerticalRoot(shearVerticalSquared, false);
    const std::complex<double> compressionalVertical =
        legacyVerticalRoot(compressionalVerticalSquared, false);
    const std::complex<double> shearModulus =
        material.density * (shearSoundSpeed * shearSoundSpeed);
    const std::complex<double> verticalSum =
        shearVerticalSquared + tangentWavenumberSquared;
    const std::complex<double> y2 =
        (verticalSum * verticalSum - 4.0 * shearVertical *
                                         compressionalVertical *
                                         tangentWavenumberSquared) *
        shearModulus;
    const std::complex<double> y4 =
        compressionalVertical *
        (tangentWavenumberSquared - shearVerticalSquared);
    f = angularFrequency * angularFrequency * y4;
    g = y2;
  } else {
    const std::complex<double> materialWavenumber =
        angularFrequency / compressionalSoundSpeed;
    f = legacyVerticalRoot(
        tangentWavenumberSquared - materialWavenumber * materialWavenumber,
        true, compressionalAttenuation.imaginarySoundSpeed == 0.0);
    g = material.density;
  }

  const std::complex<double> imaginaryNormalWavenumber{0.0, normalWavenumber};
  const std::complex<double> waterTerm = waterDensity * f;
  const std::complex<double> solidTerm = imaginaryNormalWavenumber * g;
  const std::complex<double> denominator = waterTerm + solidTerm;
  if (denominator == std::complex<double>{}) {
    throw ValidationError(
        "acoustic half-space reflection denominator must not be zero");
  }

  const std::complex<double> result = -(waterTerm - solidTerm) / denominator;
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

BoundaryAcousticsResult evaluateFluidHalfSpaceAcoustics(
    const AcousticMaterial& material, double frequency, double waterDensity,
    double tangentSlowness, double outwardNormalSlowness) {
  if (material.shearSoundSpeed != 0.0 ||
      material.shearAttenuation.value != 0.0) {
    throw ValidationError(
        "fluid half-space acoustics requires zero shear properties");
  }
  return evaluateAcousticHalfSpaceAcoustics(material, frequency, waterDensity,
                                            tangentSlowness,
                                            outwardNormalSlowness);
}

BoundaryAcousticsResult evaluateAcousticHalfSpaceAcoustics(
    const AcousticMaterial& material, double frequency, double waterDensity,
    double tangentSlowness, double outwardNormalSlowness) {
  requireFinitePositive(frequency, "frequency");
  requireFinitePositive(waterDensity, "waterDensity");
  requireFinitePositive(material.compressionalSoundSpeed,
                        "material.compressionalSoundSpeed");
  requireFinite(material.shearSoundSpeed, "material.shearSoundSpeed");
  if (material.shearSoundSpeed < 0.0) {
    throw ValidationError("material.shearSoundSpeed must be non-negative");
  }
  requireFinitePositive(material.density, "material.density");
  validateRawAttenuation(material.compressionalAttenuation,
                         "material.compressionalAttenuation");
  validateRawAttenuation(material.shearAttenuation,
                         "material.shearAttenuation");
  requireFinite(tangentSlowness, "tangentSlowness");
  requireFinite(outwardNormalSlowness, "outwardNormalSlowness");
  if (outwardNormalSlowness <= 0.0) {
    throw ValidationError(
        "outwardNormalSlowness must be positive at a reflection");
  }
  return classifyBoundaryCoefficient(
      halfSpaceCoefficient(material, frequency, waterDensity, tangentSlowness,
                           outwardNormalSlowness),
      true);
}

BoundaryAcousticsResult evaluateGrainSizeHalfSpaceAcoustics(
    const GrainSizeMaterial& material, double frequency, double waterSoundSpeed,
    double waterDensity, double tangentSlowness, double outwardNormalSlowness) {
  requireFinite(material.meanGrainSize, "material.meanGrainSize");
  requireFinitePositive(material.soundSpeedRatio, "material.soundSpeedRatio");
  requireFinitePositive(material.densityRatio, "material.densityRatio");
  requireFinite(material.attenuationCoefficient,
                "material.attenuationCoefficient");
  if (material.attenuationCoefficient < 0.0) {
    throw ValidationError(
        "material.attenuationCoefficient must be non-negative");
  }
  requireFinitePositive(waterSoundSpeed, "waterSoundSpeed");

  // ReflectMod evaluates LOG(10.0) in default REAL before the result enters
  // this binary64 expression.  Keep that promoted REAL(4) value explicit.
  const double legacyLogTen = static_cast<double>(std::log(10.0F));
  const double lossParameter = material.attenuationCoefficient *
                               (material.soundSpeedRatio / 1000.0) * 1500.0 *
                               legacyLogTen / (40.0 * std::numbers::pi);
  const AcousticMaterial effectiveMaterial{
      .compressionalSoundSpeed = material.soundSpeedRatio * waterSoundSpeed,
      .shearSoundSpeed = 0.0,
      // Origin stores the handbook density ratio directly as g/cm^3.
      .density = 1000.0 * material.densityRatio,
      .compressionalAttenuation = {.value = lossParameter,
                                   .unit = AttenuationUnit::LossParameter},
      .shearAttenuation = {}};
  return evaluateFluidHalfSpaceAcoustics(effectiveMaterial, frequency,
                                         waterDensity, tangentSlowness,
                                         outwardNormalSlowness);
}

BoundaryAcousticsResult evaluateTabulatedReflectionAcoustics(
    const TabulatedReflectionTable& table, double tangentSlowness,
    double outwardNormalSlowness) {
  requireFinite(tangentSlowness, "tangentSlowness");
  requireFinite(outwardNormalSlowness, "outwardNormalSlowness");
  if (outwardNormalSlowness <= 0.0) {
    throw ValidationError(
        "outwardNormalSlowness must be positive at a reflection");
  }
  if (table.size() < 2U) {
    throw ValidationError(
        "tabulated reflection requires at least two table points");
  }
  for (std::size_t index = 0U; index < table.size(); ++index) {
    const TabulatedReflectionPoint& point = table[index];
    requireFinite(point.angleDegrees, "reflectionTable.angleDegrees");
    requireFinite(point.magnitude, "reflectionTable.magnitude");
    requireFinite(point.phaseRadians, "reflectionTable.phaseRadians");
    if (point.magnitude < 0.0) {
      throw ValidationError("reflectionTable.magnitude must be non-negative");
    }
    if (index > 0U && table[index - 1U].angleDegrees >= point.angleDegrees) {
      throw ValidationError(
          "reflectionTable angles must be strictly increasing");
    }
  }

  double grazingAngleDegrees =
      180.0 / std::numbers::pi *
      std::abs(std::atan2(outwardNormalSlowness, tangentSlowness));
  if (grazingAngleDegrees > 90.0) {
    grazingAngleDegrees = 180.0 - grazingAngleDegrees;
  }
  // RefCoef.f90 assigns the binary64 query to default REAL before its domain
  // checks and binary search, then uses the original binary64 theta when it
  // forms alpha. Preserve that mixed-precision behavior, including the tiny
  // endpoint extrapolation possible inside half a REAL(4) ULP.
  const double decisionAngle =
      static_cast<double>(static_cast<float>(grazingAngleDegrees));
  if (decisionAngle < table.front().angleDegrees ||
      decisionAngle > table.back().angleDegrees) {
    return BoundaryAcousticsResult{.rawCoefficient = {},
                                   .amplitudeMultiplier = 0.0,
                                   .phaseIncrement = 0.0,
                                   .coefficientSuppressed = false};
  }

  std::size_t leftIndex = 0U;
  std::size_t rightIndex = table.size() - 1U;
  while (leftIndex != rightIndex - 1U) {
    const std::size_t middleIndex = (leftIndex + rightIndex) / 2U;
    if (table[middleIndex].angleDegrees > decisionAngle) {
      rightIndex = middleIndex;
    } else {
      leftIndex = middleIndex;
    }
  }
  const TabulatedReflectionPoint& left = table[leftIndex];
  const TabulatedReflectionPoint& right = table[rightIndex];
  const double weight = (grazingAngleDegrees - left.angleDegrees) /
                        (right.angleDegrees - left.angleDegrees);
  const double magnitude =
      (1.0 - weight) * left.magnitude + weight * right.magnitude;
  const double phase =
      (1.0 - weight) * left.phaseRadians + weight * right.phaseRadians;
  return BoundaryAcousticsResult{
      .rawCoefficient =
          magnitude * std::complex<double>{std::cos(phase), std::sin(phase)},
      .amplitudeMultiplier = magnitude,
      .phaseIncrement = phase,
      .coefficientSuppressed = false};
}

BoundaryAcousticsResult evaluateBoundaryAcoustics(
    const BoundaryModel& boundary, double frequency, double waterDensity,
    double tangentSlowness, double outwardNormalSlowness) {
  return evaluateBoundaryAcoustics(boundary, 0U, frequency, waterDensity,
                                   tangentSlowness, outwardNormalSlowness);
}

BoundaryAcousticsResult evaluateBoundaryAcoustics(
    const BoundaryModel& boundary, std::size_t boundarySegmentIndex,
    double frequency, double waterDensity, double tangentSlowness,
    double outwardNormalSlowness) {
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
      return evaluateAcousticHalfSpaceAcoustics(
          boundary.materialAtSegment(boundarySegmentIndex), frequency,
          waterDensity, tangentSlowness, outwardNormalSlowness);
    case BoundaryKind::GrainSizeHalfSpace:
      throw ValidationError(
          "grain-size acoustics requires the local water sound speed");
    case BoundaryKind::TabulatedReflection:
      if (!boundary.reflectionTable()) {
        throw ValidationError(
            "tabulated-reflection boundary is missing its table");
      }
      return evaluateTabulatedReflectionAcoustics(
          *boundary.reflectionTable(), tangentSlowness, outwardNormalSlowness);
  }

  throw ValidationError("unsupported boundary kind");
}

}  // namespace rayreuse
