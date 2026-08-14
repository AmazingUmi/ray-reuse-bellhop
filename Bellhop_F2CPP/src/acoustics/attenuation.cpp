#include "bellhop/acoustics/attenuation.hpp"

#include <cmath>
#include <numbers>
#include <string>
#include <string_view>
#include <variant>

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
constexpr std::size_t kMaximumBiologicalLayers = 200U;

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
  const double angularFrequency =
      2.0 * std::numbers::pi * frequency;
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
      return angularFrequency /
             (2.0 * soundSpeed * attenuation.value);

    case AttenuationUnit::LossParameter:
      return attenuation.value * angularFrequency / soundSpeed;
  }

  throw ValidationError("unsupported attenuation unit");
}

double volumeAttenuationNpPerMeter(
    const VolumeAttenuation& attenuation, double depth,
    double frequency) {
  switch (attenuation.model) {
    case VolumeAttenuationModel::None:
      if (!std::holds_alternative<std::monostate>(
              attenuation.parameters)) {
        throw ValidationError(
            "None volume attenuation must not carry parameters");
      }
      return 0.0;
    case VolumeAttenuationModel::Thorp:
      if (!std::holds_alternative<std::monostate>(
              attenuation.parameters)) {
        throw ValidationError(
            "Thorp volume attenuation must not carry parameters");
      }
      return thorpAttenuationNpPerMeter(frequency);
    case VolumeAttenuationModel::FrancoisGarrison:
      if (!std::holds_alternative<FrancoisGarrisonParameters>(
              attenuation.parameters)) {
        throw ValidationError(
            "Francois-Garrison attenuation requires matching parameters");
      }
      return francoisGarrisonAttenuationNpPerMeter(
          std::get<FrancoisGarrisonParameters>(
              attenuation.parameters),
          frequency);
    case VolumeAttenuationModel::Biological:
      if (!std::holds_alternative<SharedBiologicalAttenuationLayers>(
              attenuation.parameters)) {
        throw ValidationError(
            "biological attenuation requires matching layers");
      }
      const auto& sharedLayers =
          std::get<SharedBiologicalAttenuationLayers>(
              attenuation.parameters);
      if (!sharedLayers) {
        throw ValidationError(
            "biological attenuation requires non-null layers");
      }
      return biologicalAttenuationNpPerMeter(
          *sharedLayers, depth, frequency);
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

double francoisGarrisonAttenuationNpPerMeter(
    const FrancoisGarrisonParameters& parameters, double frequency) {
  requireFinitePositive(frequency, "frequency");
  requireFinite(parameters.temperatureCelsius,
                "francoisGarrison.temperatureCelsius");
  requireFinite(parameters.salinityPsu,
                "francoisGarrison.salinityPsu");
  requireFinite(parameters.pH, "francoisGarrison.pH");
  requireFinite(parameters.meanDepthMeters,
                "francoisGarrison.meanDepthMeters");
  if (parameters.temperatureCelsius <= -273.0) {
    throw ValidationError(
        "francoisGarrison.temperatureCelsius must exceed -273 C");
  }
  if (parameters.salinityPsu < 0.0 || parameters.meanDepthMeters < 0.0) {
    throw ValidationError(
        "Francois-Garrison salinity and mean depth must be non-negative");
  }

  const double temperature = parameters.temperatureCelsius;
  const double salinity = parameters.salinityPsu;
  const double meanDepth = parameters.meanDepthMeters;
  const double frequencyKilohertz = frequency / 1000.0;
  const double frequencySquared =
      frequencyKilohertz * frequencyKilohertz;
  const double absoluteTemperature = temperature + 273.0;
  const double soundSpeed =
      1412.0 + static_cast<double>(3.21F) * temperature +
      static_cast<double>(1.19F) * salinity +
      static_cast<double>(0.0167F) * meanDepth;

  const double boricCoefficient =
      static_cast<double>(8.86F) / soundSpeed *
      std::pow(10.0, static_cast<double>(0.78F) * parameters.pH - 5.0);
  const double boricRelaxation =
      static_cast<double>(2.8F) * std::sqrt(salinity / 35.0) *
      std::pow(10.0, 4.0 - 1245.0 / absoluteTemperature);

  const double magnesiumCoefficient =
      static_cast<double>(21.44F) * salinity / soundSpeed *
      (1.0 + static_cast<double>(0.025F) * temperature);
  const double meanDepthSquared = meanDepth * meanDepth;
  const double magnesiumPressure =
      std::fma(6.2e-9, meanDepthSquared,
               std::fma(-1.37e-4, meanDepth, 1.0));
  const double magnesiumRelaxation =
      static_cast<double>(8.17F) *
      std::pow(10.0, 8.0 - 1990.0 / absoluteTemperature) /
      (1.0 + static_cast<double>(0.0018F) * (salinity - 35.0));

  const double viscosityPressure =
      std::fma(4.9e-10, meanDepthSquared,
               std::fma(-3.83e-5, meanDepth, 1.0));
  const double temperatureSquared = temperature * temperature;
  const double temperatureCubed = temperature * temperatureSquared;
  const double viscosityCoefficient =
      temperature < 20.0
          ? std::fma(
                -1.5e-8, temperatureCubed,
                std::fma(9.11e-7, temperatureSquared,
                         std::fma(-2.59e-5, temperature, 4.937e-4)))
          : std::fma(
                -6.5e-10, temperatureCubed,
                std::fma(1.45e-7, temperatureSquared,
                         std::fma(-1.146e-5, temperature, 3.964e-4)));

  const double decibelsPerKilometer =
      boricCoefficient *
          (boricRelaxation * frequencySquared) /
          (boricRelaxation * boricRelaxation + frequencySquared) +
      magnesiumCoefficient * magnesiumPressure *
          (magnesiumRelaxation * frequencySquared) /
          (magnesiumRelaxation * magnesiumRelaxation + frequencySquared) +
      viscosityCoefficient * viscosityPressure * frequencySquared;
  const double result =
      decibelsPerKilometer / kThorpDecibelsPerKilometerPerNeper;
  if (!std::isfinite(result) || result < 0.0) {
    throw ValidationError(
        "Francois-Garrison attenuation must be finite and non-negative");
  }
  return result;
}

double biologicalAttenuationNpPerMeter(
    const BiologicalAttenuationLayers& layers, double depth,
    double frequency) {
  requireFinite(depth, "depth");
  requireFinitePositive(frequency, "frequency");
  if (layers.size() > kMaximumBiologicalLayers) {
    throw ValidationError(
        "biological attenuation supports at most 200 layers");
  }

  double nepersPerMeter = 0.0;
  for (const BiologicalAttenuationLayer& layer : layers) {
    requireFinite(layer.minimumDepth, "biologicalLayer.minimumDepth");
    requireFinite(layer.maximumDepth, "biologicalLayer.maximumDepth");
    requireFinitePositive(layer.resonanceFrequency,
                          "biologicalLayer.resonanceFrequency");
    requireFinitePositive(layer.qualityFactor,
                          "biologicalLayer.qualityFactor");
    requireFinite(
        layer.attenuationCoefficientDecibelsPerKilometer,
        "biologicalLayer.attenuationCoefficientDecibelsPerKilometer");
    if (layer.minimumDepth > layer.maximumDepth) {
      throw ValidationError(
          "biological layer minimum depth must not exceed maximum depth");
    }
    if (layer.attenuationCoefficientDecibelsPerKilometer < 0.0) {
      throw ValidationError(
          "biological layer attenuation coefficient must be non-negative");
    }
    if (depth >= layer.minimumDepth && depth <= layer.maximumDepth) {
      const double resonanceRatio =
          layer.resonanceFrequency / frequency;
      const double detuning = 1.0 - resonanceRatio * resonanceRatio;
      const double inverseQualitySquared =
          1.0 / (layer.qualityFactor * layer.qualityFactor);
      const double decibelsPerKilometer =
          layer.attenuationCoefficientDecibelsPerKilometer /
          (detuning * detuning + inverseQualitySquared);
      // AttenMod divides each matching layer before adding it to alphaT.
      // Keeping the division inside the loop preserves the shared-endpoint
      // last bit when multiple layers overlap.
      nepersPerMeter +=
          decibelsPerKilometer /
          kThorpDecibelsPerKilometerPerNeper;
    }
  }
  if (!std::isfinite(nepersPerMeter) || nepersPerMeter < 0.0) {
    throw ValidationError(
        "biological attenuation must be finite and non-negative");
  }
  return nepersPerMeter;
}

double attenuationNpPerMeter(const RawAttenuation& attenuation,
                             double frequency, double soundSpeed) {
  return attenuationNpPerMeter(
      attenuation, VolumeAttenuation{}, frequency, soundSpeed, 0.0);
}

double attenuationNpPerMeter(const RawAttenuation& attenuation,
                             const VolumeAttenuation& volumeAttenuation,
                             double frequency, double soundSpeed,
                             double depth) {
  validateRawAttenuation(attenuation);
  requireFinitePositive(frequency, "frequency");
  requireFinitePositive(soundSpeed, "soundSpeed");
  requireFinite(depth, "depth");

  const double base =
      baseAttenuationNpPerMeter(attenuation, frequency, soundSpeed);
  const double volume =
      volumeAttenuationNpPerMeter(
          volumeAttenuation, depth, frequency);
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
  return convertAttenuation(
      attenuation, VolumeAttenuation{}, frequency, soundSpeed, 0.0);
}

AttenuationConversion convertAttenuation(
    const RawAttenuation& attenuation,
    const VolumeAttenuation& volumeAttenuation,
    double frequency, double soundSpeed, double depth) {
  const double converted =
      attenuationNpPerMeter(
          attenuation, volumeAttenuation, frequency, soundSpeed, depth);
  return AttenuationConversion{
      .attenuationNpPerMeter = converted,
      .imaginarySoundSpeed = imaginarySoundSpeedFromAttenuation(
          converted, frequency, soundSpeed),
  };
}

}  // namespace bellhop
