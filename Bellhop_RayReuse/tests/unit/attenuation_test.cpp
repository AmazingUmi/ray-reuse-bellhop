#include "rayreuse/acoustics/attenuation.hpp"

#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <memory>
#include <numbers>

#include "rayreuse/error.hpp"
#include "support/test_harness.hpp"

namespace {

using rayreuse::AttenuationConversion;
using rayreuse::attenuationNpPerMeter;
using rayreuse::AttenuationUnit;
using rayreuse::BiologicalAttenuationLayer;
using rayreuse::BiologicalAttenuationLayers;
using rayreuse::biologicalAttenuationNpPerMeter;
using rayreuse::convertAttenuation;
using rayreuse::FrancoisGarrisonParameters;
using rayreuse::francoisGarrisonAttenuationNpPerMeter;
using rayreuse::imaginarySoundSpeedFromAttenuation;
using rayreuse::RawAttenuation;
using rayreuse::thorpAttenuationNpPerMeter;
using rayreuse::ValidationError;
using rayreuse::VolumeAttenuation;
using rayreuse::VolumeAttenuationModel;
using rayreuse::test::Context;

constexpr double kDecibelsPerNeper = 8.6858896;
constexpr double kLegacyDecibelsPerKilometerPerNeper =
    static_cast<double>(8685.8896F);

RawAttenuation rawAttenuation(double value, AttenuationUnit unit) {
  return RawAttenuation{
      .value = value,
      .unit = unit,
      .referenceFrequency = 100.0,
      .powerLawExponent = 1.5,
      .transitionFrequency = 1000.0,
      .volumeModel = VolumeAttenuationModel::None,
  };
}

void testRequiredUnits(Context& context) {
  constexpr double frequency = 2000.0;
  constexpr double soundSpeed = 1500.0;

  const RawAttenuation nepers =
      rawAttenuation(2.0e-4, AttenuationUnit::NepersPerMeter);
  context.checkNear(attenuationNpPerMeter(nepers, frequency, soundSpeed),
                    2.0e-4, 0.0,
                    "N preserves attenuation already expressed in Np/m");

  const RawAttenuation wavelength =
      rawAttenuation(0.5, AttenuationUnit::DecibelsPerWavelength);
  const double expectedWavelength =
      0.5 * frequency / (kDecibelsPerNeper * soundSpeed);
  context.checkNear(attenuationNpPerMeter(wavelength, frequency, soundSpeed),
                    expectedWavelength, 1.0e-17,
                    "W converts dB per wavelength using the current frequency");

  const RawAttenuation frequencyDependent =
      rawAttenuation(0.25, AttenuationUnit::DecibelsPerMeterKilohertz);
  const double expectedFrequencyDependent =
      0.25 * (frequency / 1000.0) / kDecibelsPerNeper;
  context.checkNear(
      attenuationNpPerMeter(frequencyDependent, frequency, soundSpeed),
      expectedFrequencyDependent, 1.0e-17,
      "F is legacy dB per meter-kHz, not dB per kilometer-kHz");
}

void testThorpAndImaginarySoundSpeed(Context& context) {
  constexpr double frequency = 5000.0;
  constexpr double soundSpeed = 1500.0;
  // Independent gfortran AttenMod/ray-oracle anchors. The source formula
  // contains two default-REAL constants, so a pure-double restatement is not
  // bit-compatible with the executable.
  constexpr double expectedThorp = 4.41216631081326193e-5;
  constexpr double expectedImaginary = 3.15998135149258121e-3;

  context.checkNear(thorpAttenuationNpPerMeter(frequency), expectedThorp,
                    1.0e-20, "T matches the updated AttenMod Thorp formula");
  context.checkNear(thorpAttenuationNpPerMeter(1000.0), 7.98180646701957771e-6,
                    1.0e-21, "T matches the 1 kHz Fortran oracle anchor");
  context.checkNear(thorpAttenuationNpPerMeter(10000.0), 1.36984233771845050e-4,
                    1.0e-20, "T matches the 10 kHz Fortran oracle anchor");

  RawAttenuation withThorp =
      rawAttenuation(0.0, AttenuationUnit::DecibelsPerWavelength);
  withThorp.volumeModel = VolumeAttenuationModel::Thorp;
  const AttenuationConversion converted =
      convertAttenuation(withThorp, frequency, soundSpeed);
  context.checkNear(converted.attenuationNpPerMeter, expectedThorp, 1.0e-20,
                    "zero explicit loss plus T yields pure Thorp loss");
  context.checkNear(converted.imaginarySoundSpeed, expectedImaginary, 1.0e-18,
                    "Np/m converts to Bellhop's positive imaginary speed");

  const RawAttenuation noLoss =
      rawAttenuation(0.0, AttenuationUnit::DecibelsPerWavelength);
  const AttenuationConversion zero =
      convertAttenuation(noLoss, frequency, soundSpeed);
  context.check(zero.attenuationNpPerMeter == 0.0,
                "the high-frequency no-attenuation case stays exactly zero");
  context.check(zero.imaginarySoundSpeed == 0.0,
                "zero attenuation produces exactly zero imaginary speed");
}

void testFrancoisGarrisonOracle(Context& context) {
  constexpr FrancoisGarrisonParameters canonical{
      .temperatureCelsius = 20.0,
      .salinityPsu = 35.0,
      .pH = 8.0,
      .meanDepthMeters = 0.0,
  };
  struct Anchor {
    double frequency;
    double decibelsPerKilometer;
  };
  constexpr std::array anchors{
      Anchor{10.0, 7.01348825486111722e-6},
      Anchor{100.0, 6.98801686474176636e-4},
      Anchor{1000.0, 5.17084696772871361e-2},
      Anchor{10000.0, 7.36946806223103268e-1},
      Anchor{50000.0, 1.29959477197854376e1},
  };
  for (const Anchor& anchor : anchors) {
    context.checkNear(
        francoisGarrisonAttenuationNpPerMeter(canonical, anchor.frequency),
        anchor.decibelsPerKilometer /
            kLegacyDecibelsPerKilometerPerNeper,
        2.0e-20, "Francois-Garrison matches the gfortran oracle");
  }

  constexpr FrancoisGarrisonParameters cold{
      .temperatureCelsius = 10.0,
      .salinityPsu = 35.0,
      .pH = 8.0,
      .meanDepthMeters = 1000.0,
  };
  constexpr FrancoisGarrisonParameters warm{
      .temperatureCelsius = 25.0,
      .salinityPsu = 35.0,
      .pH = 8.0,
      .meanDepthMeters = 1000.0,
  };
  context.checkNear(
      francoisGarrisonAttenuationNpPerMeter(cold, 10000.0),
      8.45396087874079272e-1 / kLegacyDecibelsPerKilometerPerNeper,
      2.0e-20, "Francois-Garrison cold branch matches Origin");
  context.checkNear(
      francoisGarrisonAttenuationNpPerMeter(warm, 10000.0),
      5.93874271560436817e-1 / kLegacyDecibelsPerKilometerPerNeper,
      2.0e-20, "Francois-Garrison warm branch matches Origin");
}

void testBiologicalOracle(Context& context) {
  const BiologicalAttenuationLayer first{
      .minimumDepth = 100.0,
      .maximumDepth = 200.0,
      .resonanceFrequency = 1000.0,
      .qualityFactor = 10.0,
      .attenuationCoefficientDecibelsPerKilometer = 0.25,
  };
  const BiologicalAttenuationLayer second{
      .minimumDepth = 200.0,
      .maximumDepth = 300.0,
      .resonanceFrequency = 500.0,
      .qualityFactor = 5.0,
      .attenuationCoefficientDecibelsPerKilometer = 0.5,
  };
  const BiologicalAttenuationLayers layers{first, second};
  struct Anchor {
    double depth;
    double expected;
    const char* message;
  };
  constexpr std::array anchors{
      Anchor{99.0, 0.0, "above biological layers is lossless"},
      Anchor{100.0, 2.87823136280544820e-3, "top endpoint is inclusive"},
      Anchor{199.0, 2.87823136280544820e-3, "layer interior matches"},
      Anchor{200.0, 2.97377431260811885e-3,
             "shared endpoint accumulates both layers"},
      Anchor{300.0, 9.55429498026704745e-5,
             "bottom endpoint is inclusive"},
      Anchor{301.0, 0.0, "below biological layers is lossless"},
  };
  for (const Anchor& anchor : anchors) {
    context.checkNear(
        biologicalAttenuationNpPerMeter(layers, anchor.depth, 1000.0),
        anchor.expected, 0.0, anchor.message);
  }
  context.checkNear(biologicalAttenuationNpPerMeter({}, 200.0, 1000.0),
                    0.0, 0.0, "zero biological layers are supported");

  const BiologicalAttenuationLayers maximumLayers(200U, first);
  context.checkNear(
      biologicalAttenuationNpPerMeter(maximumLayers, 100.0, 1000.0),
      200.0 * 2.87823136280544820e-3, 2.0e-15,
      "200 biological layers are supported and additive");
  const BiologicalAttenuationLayers tooManyLayers(201U, first);
  context.expectThrows<ValidationError>(
      [&tooManyLayers] {
        static_cast<void>(biologicalAttenuationNpPerMeter(
            tooManyLayers, 100.0, 1000.0));
      },
      "201 biological layers are rejected");
}

void testCompatibilityResolution(Context& context) {
  constexpr double frequency = 1000.0;
  constexpr double soundSpeed = 1500.0;
  constexpr double depth = 100.0;
  const double base = 1.0e-5;
  const double thorp = thorpAttenuationNpPerMeter(frequency);
  const FrancoisGarrisonParameters fgParameters{};
  const double fg =
      francoisGarrisonAttenuationNpPerMeter(fgParameters, frequency);
  const BiologicalAttenuationLayers layers{{
      .minimumDepth = 0.0,
      .maximumDepth = 200.0,
      .resonanceFrequency = 500.0,
      .qualityFactor = 5.0,
      .attenuationCoefficientDecibelsPerKilometer = 0.5,
  }};
  const double biological =
      biologicalAttenuationNpPerMeter(layers, depth, frequency);
  const VolumeAttenuation none{};
  const VolumeAttenuation thorpEnvironment{
      .model = VolumeAttenuationModel::Thorp};
  const VolumeAttenuation fgEnvironment{
      .model = VolumeAttenuationModel::FrancoisGarrison,
      .parameters = fgParameters};
  const VolumeAttenuation biologicalEnvironment{
      .model = VolumeAttenuationModel::Biological,
      .parameters =
          std::make_shared<const BiologicalAttenuationLayers>(layers)};

  auto raw = rawAttenuation(base, AttenuationUnit::NepersPerMeter);
  context.checkNear(attenuationNpPerMeter(raw, none, frequency, soundSpeed,
                                         depth),
                    base, 0.0, "raw None and environment None use base only");
  raw.volumeModel = VolumeAttenuationModel::Thorp;
  context.checkNear(attenuationNpPerMeter(raw, none, frequency, soundSpeed,
                                         depth),
                    base + thorp, 0.0,
                    "raw Thorp and environment None preserve legacy Thorp");
  raw.volumeModel = VolumeAttenuationModel::None;
  context.checkNear(attenuationNpPerMeter(raw, thorpEnvironment, frequency,
                                         soundSpeed, depth),
                    base + thorp, 0.0,
                    "raw None and environment Thorp add Thorp");
  context.checkNear(attenuationNpPerMeter(raw, fgEnvironment, frequency,
                                         soundSpeed, depth),
                    base + fg, 0.0,
                    "raw None and environment Francois-Garrison add FG");
  context.checkNear(attenuationNpPerMeter(raw, biologicalEnvironment,
                                         frequency, soundSpeed, depth),
                    base + biological, 0.0,
                    "raw None and environment biological add biological");
  raw.volumeModel = VolumeAttenuationModel::Thorp;
  context.checkNear(attenuationNpPerMeter(raw, thorpEnvironment, frequency,
                                         soundSpeed, depth),
                    base + thorp, 0.0,
                    "matching Thorp models add volume exactly once");
  context.expectThrows<ValidationError>(
      [&] {
        static_cast<void>(attenuationNpPerMeter(
            raw, fgEnvironment, frequency, soundSpeed, depth));
      },
      "raw Thorp conflicts with environment Francois-Garrison");
  context.expectThrows<ValidationError>(
      [&] {
        static_cast<void>(attenuationNpPerMeter(
            raw, biologicalEnvironment, frequency, soundSpeed, depth));
      },
      "raw Thorp conflicts with environment biological");
  raw.volumeModel = VolumeAttenuationModel::FrancoisGarrison;
  context.expectThrows<ValidationError>(
      [&] {
        static_cast<void>(attenuationNpPerMeter(
            raw, none, frequency, soundSpeed, depth));
      },
      "raw Francois-Garrison without environment parameters is rejected");
  context.checkNear(attenuationNpPerMeter(raw, fgEnvironment, frequency,
                                         soundSpeed, depth),
                    base + fg, 0.0,
                    "matching Francois-Garrison adds volume exactly once");
  context.expectThrows<ValidationError>(
      [&] {
        static_cast<void>(attenuationNpPerMeter(
            raw, thorpEnvironment, frequency, soundSpeed, depth));
      },
      "raw Francois-Garrison conflicts with environment Thorp");
  context.expectThrows<ValidationError>(
      [&] {
        static_cast<void>(attenuationNpPerMeter(
            raw, biologicalEnvironment, frequency, soundSpeed, depth));
      },
      "raw Francois-Garrison conflicts with environment biological");
  raw.volumeModel = VolumeAttenuationModel::Biological;
  context.expectThrows<ValidationError>(
      [&] {
        static_cast<void>(attenuationNpPerMeter(
            raw, none, frequency, soundSpeed, depth));
      },
      "raw biological without environment parameters is rejected");
  context.checkNear(attenuationNpPerMeter(raw, biologicalEnvironment,
                                         frequency, soundSpeed, depth),
                    base + biological, 0.0,
                    "matching biological adds volume exactly once");
  context.expectThrows<ValidationError>(
      [&] {
        static_cast<void>(attenuationNpPerMeter(
            raw, thorpEnvironment, frequency, soundSpeed, depth));
      },
      "raw biological conflicts with environment Thorp");
  context.expectThrows<ValidationError>(
      [&] {
        static_cast<void>(attenuationNpPerMeter(
            raw, fgEnvironment, frequency, soundSpeed, depth));
      },
      "raw biological conflicts with environment Francois-Garrison");
}

void testLegacyAdditionalUnits(Context& context) {
  constexpr double frequency = 500.0;
  constexpr double soundSpeed = 1600.0;

  const RawAttenuation decibels =
      rawAttenuation(0.2, AttenuationUnit::DecibelsPerMeter);
  context.checkNear(attenuationNpPerMeter(decibels, frequency, soundSpeed),
                    0.2 / kDecibelsPerNeper, 1.0e-17,
                    "M converts dB/m to Np/m");

  RawAttenuation powerLaw =
      rawAttenuation(0.2, AttenuationUnit::DecibelsPerMeterPowerLaw);
  const double belowTransition =
      0.2 / kDecibelsPerNeper * std::pow(500.0 / 100.0, 1.5);
  context.checkNear(attenuationNpPerMeter(powerLaw, frequency, soundSpeed),
                    belowTransition, 1.0e-16,
                    "m applies the power law below the transition frequency");

  const double aboveTransition = 0.2 / kDecibelsPerNeper * (2000.0 / 100.0) *
                                 std::pow(1000.0 / 100.0, 0.5);
  context.checkNear(attenuationNpPerMeter(powerLaw, 2000.0, soundSpeed),
                    aboveTransition, 1.0e-16,
                    "m becomes linear above the transition frequency");

  const RawAttenuation quality =
      rawAttenuation(100.0, AttenuationUnit::QualityFactor);
  context.checkNear(
      attenuationNpPerMeter(quality, frequency, soundSpeed),
      2.0 * std::numbers::pi * frequency / (2.0 * soundSpeed * 100.0), 1.0e-17,
      "Q follows the legacy quality-factor conversion");

  const RawAttenuation loss =
      rawAttenuation(1.0e-5, AttenuationUnit::LossParameter);
  context.checkNear(attenuationNpPerMeter(loss, frequency, soundSpeed),
                    1.0e-5 * 2.0 * std::numbers::pi * frequency / soundSpeed,
                    1.0e-17, "L follows the legacy loss-parameter conversion");
}

void testRawInputIsImmutable(Context& context) {
  RawAttenuation attenuation =
      rawAttenuation(0.375, AttenuationUnit::DecibelsPerMeterKilohertz);
  attenuation.referenceFrequency = 37.0;
  const RawAttenuation original = attenuation;

  static_cast<void>(convertAttenuation(attenuation, 250.0, 1500.0));

  context.check(attenuation.value == original.value,
                "conversion does not overwrite the raw attenuation value");
  context.check(attenuation.unit == original.unit,
                "conversion does not overwrite the raw attenuation unit");
  context.check(attenuation.referenceFrequency == original.referenceFrequency,
                "conversion does not overwrite the reference frequency");
  context.check(attenuation.powerLawExponent == original.powerLawExponent,
                "conversion does not overwrite the power-law exponent");
  context.check(attenuation.transitionFrequency == original.transitionFrequency,
                "conversion does not overwrite the transition frequency");
  context.check(attenuation.volumeModel == original.volumeModel,
                "conversion does not overwrite the volume model");

  RawAttenuation otherReference = attenuation;
  otherReference.referenceFrequency = 3700.0;
  context.checkNear(attenuationNpPerMeter(attenuation, 250.0, 1500.0),
                    attenuationNpPerMeter(otherReference, 250.0, 1500.0), 0.0,
                    "F does not misuse the power-law reference frequency");
}

void testInvalidAndUnsupportedInputs(Context& context) {
  const RawAttenuation valid =
      rawAttenuation(0.0, AttenuationUnit::NepersPerMeter);

  context.expectThrows<ValidationError>(
      [&valid] { static_cast<void>(convertAttenuation(valid, 0.0, 1500.0)); },
      "non-positive frequency is rejected");
  context.expectThrows<ValidationError>(
      [&valid] { static_cast<void>(convertAttenuation(valid, 50.0, 0.0)); },
      "non-positive sound speed is rejected");

  RawAttenuation invalid = valid;
  invalid.value = -1.0;
  context.expectThrows<ValidationError>(
      [&invalid] {
        static_cast<void>(convertAttenuation(invalid, 50.0, 1500.0));
      },
      "negative raw attenuation is rejected");

  invalid = valid;
  invalid.referenceFrequency = std::numeric_limits<double>::quiet_NaN();
  context.expectThrows<ValidationError>(
      [&invalid] {
        static_cast<void>(convertAttenuation(invalid, 50.0, 1500.0));
      },
      "non-finite reference frequency is rejected");

  invalid = valid;
  invalid.unit = static_cast<AttenuationUnit>(999);
  context.expectThrows<ValidationError>(
      [&invalid] {
        static_cast<void>(convertAttenuation(invalid, 50.0, 1500.0));
      },
      "unknown attenuation unit fails explicitly");

  invalid = valid;
  invalid.volumeModel = VolumeAttenuationModel::FrancoisGarrison;
  context.expectThrows<ValidationError>(
      [&invalid] {
        static_cast<void>(convertAttenuation(invalid, 50.0, 1500.0));
      },
      "unsupported volume model fails explicitly");

  invalid = valid;
  invalid.volumeModel = static_cast<VolumeAttenuationModel>(999);
  context.expectThrows<ValidationError>(
      [&invalid] {
        static_cast<void>(convertAttenuation(invalid, 50.0, 1500.0));
      },
      "unknown volume model fails explicitly");

  context.expectThrows<ValidationError>(
      [] {
        static_cast<void>(
            imaginarySoundSpeedFromAttenuation(10.0, 1.0, 1500.0));
      },
      "an imaginary sound speed greater than the real speed is rejected");
}

}  // namespace

int main() {
  Context context;
  testRequiredUnits(context);
  testThorpAndImaginarySoundSpeed(context);
  testFrancoisGarrisonOracle(context);
  testBiologicalOracle(context);
  testCompatibilityResolution(context);
  testLegacyAdditionalUnits(context);
  testRawInputIsImmutable(context);
  testInvalidAndUnsupportedInputs(context);

  if (context.failureCount() != 0) {
    std::cerr << context.failureCount() << " test assertion(s) failed\n";
    return 1;
  }
  std::cout << "All Bellhop RayReuse attenuation tests passed\n";
  return 0;
}
