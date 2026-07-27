#include "bellhop/acoustics/attenuation.hpp"

#include <cmath>
#include <numbers>
#include <string>
#include <string_view>

#include "bellhop/error.hpp"

namespace bellhop {
namespace {

constexpr double kDecibelsPerNeper = 8.6858896;
constexpr double kHertzPerKilohertz = 1000.0;
// AttenMod.f90 spells these two Thorp constants without a D exponent.  The
// legacy calculation therefore promotes their binary32 values to REAL(8).
constexpr double kThorpRelaxationCoefficient =
    static_cast<double>(0.11F);
constexpr double kThorpDecibelsPerKilometerPerNeper =
    static_cast<double>(8685.8896F);

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

void validateRawAttenuation(const RawAttenuation& attenuation) {
  requireFinite(attenuation.value, "attenuation.value");
  requireFinitePositive(attenuation.referenceFrequency,
                        "attenuation.referenceFrequency");
  requireFinite(attenuation.powerLawExponent,
                "attenuation.powerLawExponent");
  requireFinitePositive(attenuation.transitionFrequency,
                        "attenuation.transitionFrequency");
  if (attenuation.value < 0.0) {
    throw ValidationError("attenuation.value must be non-negative");
  }
}

double baseAttenuationNpPerMeter(const RawAttenuation& attenuation,
                                double frequency, double soundSpeed) {
  switch (attenuation.unit) {
    case AttenuationUnit::NepersPerMeter:
      return attenuation.value;

    case AttenuationUnit::DecibelsPerMeter:
      return attenuation.value / kDecibelsPerNeper;

    case AttenuationUnit::DecibelsPerMeterPowerLaw: {
      const double attenuationAtReference =
          attenuation.value / kDecibelsPerNeper;
      double frequencyFactor = 0.0;
      if (frequency < attenuation.transitionFrequency) {
        frequencyFactor =
            std::pow(frequency / attenuation.referenceFrequency,
                     attenuation.powerLawExponent);
      } else {
        frequencyFactor =
            (frequency / attenuation.referenceFrequency) *
            std::pow(attenuation.transitionFrequency /
                         attenuation.referenceFrequency,
                     attenuation.powerLawExponent - 1.0);
      }
      if (!std::isfinite(frequencyFactor)) {
        throw ValidationError(
            "power-law attenuation frequency factor must be finite");
      }
      return attenuationAtReference * frequencyFactor;
    }

    case AttenuationUnit::DecibelsPerMeterKilohertz:
      // Legacy Bellhop F: value [dB/(m*kHz)] * frequency [Hz].
      return attenuation.value * frequency /
             (kHertzPerKilohertz * kDecibelsPerNeper);

    case AttenuationUnit::DecibelsPerWavelength:
      return attenuation.value * frequency /
             (kDecibelsPerNeper * soundSpeed);

    case AttenuationUnit::QualityFactor:
      if (attenuation.value == 0.0) {
        return 0.0;
      }
      return (2.0 * std::numbers::pi * frequency) /
             (2.0 * soundSpeed * attenuation.value);

    case AttenuationUnit::LossParameter:
      return attenuation.value * 2.0 * std::numbers::pi * frequency /
             soundSpeed;
  }

  throw ValidationError("unsupported attenuation unit");
}

double volumeAttenuationNpPerMeter(VolumeAttenuationModel model,
                                   double frequency) {
  switch (model) {
    case VolumeAttenuationModel::None:
      return 0.0;
    case VolumeAttenuationModel::Thorp:
      return thorpAttenuationNpPerMeter(frequency);
    case VolumeAttenuationModel::FrancoisGarrison:
      throw ValidationError(
          "Francois-Garrison volume attenuation is not supported");
    case VolumeAttenuationModel::Biological:
      throw ValidationError("biological volume attenuation is not supported");
  }

  throw ValidationError("unsupported volume attenuation model");
}

}  // namespace

double thorpAttenuationNpPerMeter(double frequency) {
  requireFinitePositive(frequency, "frequency");

  const double frequencyKilohertz = frequency / kHertzPerKilohertz;
  const double frequencySquared =
      frequencyKilohertz * frequencyKilohertz;
  const double decibelsPerKilometer =
      3.3e-3 +
      kThorpRelaxationCoefficient * frequencySquared /
          (1.0 + frequencySquared) +
      44.0 * frequencySquared / (4100.0 + frequencySquared) +
      3.0e-4 * frequencySquared;
  const double result =
      decibelsPerKilometer /
      kThorpDecibelsPerKilometerPerNeper;
  if (!std::isfinite(result) || result < 0.0) {
    throw ValidationError("Thorp attenuation must be finite and non-negative");
  }
  return result;
}

double attenuationNpPerMeter(const RawAttenuation& attenuation,
                             double frequency, double soundSpeed) {
  validateRawAttenuation(attenuation);
  requireFinitePositive(frequency, "frequency");
  requireFinitePositive(soundSpeed, "soundSpeed");

  const double base =
      baseAttenuationNpPerMeter(attenuation, frequency, soundSpeed);
  const double volume =
      volumeAttenuationNpPerMeter(attenuation.volumeModel, frequency);
  const double result = base + volume;
  if (!std::isfinite(result) || result < 0.0) {
    throw ValidationError(
        "converted attenuation must be finite and non-negative");
  }
  return result;
}

double imaginarySoundSpeedFromAttenuation(double attenuation,
                                          double frequency,
                                          double soundSpeed) {
  requireFinite(attenuation, "attenuationNpPerMeter");
  requireFinitePositive(frequency, "frequency");
  requireFinitePositive(soundSpeed, "soundSpeed");
  if (attenuation < 0.0) {
    throw ValidationError(
        "attenuationNpPerMeter must be non-negative");
  }

  const double angularFrequency =
      2.0 * std::numbers::pi * frequency;
  const double result =
      attenuation * soundSpeed * soundSpeed / angularFrequency;
  if (!std::isfinite(result)) {
    throw ValidationError("imaginary sound speed must be finite");
  }
  if (result > soundSpeed) {
    throw ValidationError(
        "imaginary sound speed must not exceed real sound speed");
  }
  return result;
}

AttenuationConversion convertAttenuation(
    const RawAttenuation& attenuation, double frequency,
    double soundSpeed) {
  const double converted =
      attenuationNpPerMeter(attenuation, frequency, soundSpeed);
  return AttenuationConversion{
      .attenuationNpPerMeter = converted,
      .imaginarySoundSpeed = imaginarySoundSpeedFromAttenuation(
          converted, frequency, soundSpeed),
  };
}

}  // namespace bellhop
