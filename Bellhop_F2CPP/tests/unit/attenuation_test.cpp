#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <memory>
#include <numbers>

#include "bellhop/acoustics/attenuation.hpp"
#include "bellhop/error.hpp"
#include "support/test_harness.hpp"

namespace {

using bellhop::AttenuationConversion;
using bellhop::AttenuationUnit;
using bellhop::BiologicalAttenuationLayer;
using bellhop::BiologicalAttenuationLayers;
using bellhop::FrancoisGarrisonParameters;
using bellhop::RawAttenuation;
using bellhop::ValidationError;
using bellhop::VolumeAttenuation;
using bellhop::VolumeAttenuationModel;
using bellhop::attenuationNpPerMeter;
using bellhop::biologicalAttenuationNpPerMeter;
using bellhop::convertAttenuation;
using bellhop::francoisGarrisonAttenuationNpPerMeter;
using bellhop::imaginarySoundSpeedFromAttenuation;
using bellhop::test::Context;
using bellhop::thorpAttenuationNpPerMeter;

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
  };
}

void testRequiredUnits(Context& context) {
  constexpr double frequency = 2000.0;
  constexpr double soundSpeed = 1500.0;

  const RawAttenuation nepers =
      rawAttenuation(2.0e-4, AttenuationUnit::NepersPerMeter);
  context.checkNear(
      attenuationNpPerMeter(nepers, frequency, soundSpeed), 2.0e-4,
      0.0, "N preserves attenuation already expressed in Np/m");

  const RawAttenuation wavelength =
      rawAttenuation(0.5, AttenuationUnit::DecibelsPerWavelength);
  const double expectedWavelength =
      0.5 * frequency / (kDecibelsPerNeper * soundSpeed);
  context.checkNear(
      attenuationNpPerMeter(wavelength, frequency, soundSpeed),
      expectedWavelength, 1.0e-17,
      "W converts dB per wavelength using the current frequency");

  const RawAttenuation frequencyDependent =
      rawAttenuation(0.25,
                     AttenuationUnit::DecibelsPerMeterKilohertz);
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

  context.checkNear(thorpAttenuationNpPerMeter(frequency),
                    expectedThorp, 1.0e-20,
                    "T matches the updated AttenMod Thorp formula");
  context.checkNear(
      thorpAttenuationNpPerMeter(1000.0),
      7.98180646701957771e-6, 1.0e-21,
      "T matches the 1 kHz Fortran oracle anchor");
  context.checkNear(
      thorpAttenuationNpPerMeter(10000.0),
      1.36984233771845050e-4, 1.0e-20,
      "T matches the 10 kHz Fortran oracle anchor");

  const RawAttenuation withThorp =
      rawAttenuation(0.0, AttenuationUnit::DecibelsPerWavelength);
  const VolumeAttenuation thorp{
      .model = VolumeAttenuationModel::Thorp};
  const AttenuationConversion converted =
      convertAttenuation(
          withThorp, thorp, frequency, soundSpeed, 0.0);
  context.checkNear(converted.attenuationNpPerMeter, expectedThorp,
                    1.0e-20,
                    "zero explicit loss plus T yields pure Thorp loss");
  context.checkNear(converted.imaginarySoundSpeed, expectedImaginary,
                    1.0e-18,
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
  constexpr RawAttenuation noBaseLoss{};
  const VolumeAttenuation volume{
      .model = VolumeAttenuationModel::FrancoisGarrison,
      .parameters = canonical,
  };
  struct Anchor {
    double frequency;
    double decibelsPerKilometer;
    double imaginarySoundSpeed;
  };
  constexpr std::array anchors{
      Anchor{10.0, 7.01348825486111722e-6,
             2.89149480459668903e-5},
      Anchor{100.0, 6.98801686474176636e-4,
             2.88099355478780683e-4},
      Anchor{1000.0, 5.17084696772871361e-2,
             2.13181752064518044e-3},
      Anchor{10000.0, 7.36946806223103268e-1,
             3.03825683315473447e-3},
      Anchor{50000.0, 1.29959477197854376e1,
             1.07158418028359282e-2},
  };
  for (const Anchor& anchor : anchors) {
    const double expectedNpPerMeter =
        anchor.decibelsPerKilometer /
        kLegacyDecibelsPerKilometerPerNeper;
    context.checkNear(
        francoisGarrisonAttenuationNpPerMeter(
            canonical, anchor.frequency),
        expectedNpPerMeter, 2.0e-20,
        "Francois-Garrison Np/m matches the gfortran oracle");
    context.checkNear(
        convertAttenuation(noBaseLoss, volume, anchor.frequency,
                           1500.0, 500.0)
            .imaginarySoundSpeed,
        anchor.imaginarySoundSpeed, 3.0e-18,
        "Francois-Garrison imaginary speed matches the gfortran oracle");
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
      8.45396087874079272e-1 /
          kLegacyDecibelsPerKilometerPerNeper,
      2.0e-20, "Francois-Garrison cold-water branch matches Origin");
  context.checkNear(
      francoisGarrisonAttenuationNpPerMeter(warm, 10000.0),
      5.93874271560436817e-1 /
          kLegacyDecibelsPerKilometerPerNeper,
      2.0e-20, "Francois-Garrison warm-water branch matches Origin");
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

  context.checkNear(
      biologicalAttenuationNpPerMeter(layers, 99.0, 1000.0),
      0.0, 0.0, "biological loss is zero above all layers");
  context.checkNear(
      biologicalAttenuationNpPerMeter(layers, 100.0, 1000.0),
      2.87823136280544820e-3, 0.0,
      "biological top endpoint matches the gfortran oracle");
  context.checkNear(
      biologicalAttenuationNpPerMeter(layers, 199.0, 1000.0),
      2.87823136280544820e-3, 0.0,
      "biological layer interior matches the gfortran oracle");

  context.checkNear(
      biologicalAttenuationNpPerMeter(layers, 200.0, 1000.0),
      2.97377431260811885e-3, 0.0,
      "shared biological endpoint matches the gfortran accumulation order");
  context.checkNear(
      biologicalAttenuationNpPerMeter(layers, 300.0, 1000.0),
      9.55429498026704745e-5, 0.0,
      "biological bottom endpoint matches the gfortran oracle");
  context.checkNear(
      biologicalAttenuationNpPerMeter(layers, 301.0, 1000.0),
      0.0, 0.0, "biological loss is zero below all layers");

  const RawAttenuation base =
      rawAttenuation(1.0e-5, AttenuationUnit::NepersPerMeter);
  const VolumeAttenuation biological{
      .model = VolumeAttenuationModel::Biological,
      .parameters =
          std::make_shared<const BiologicalAttenuationLayers>(layers),
  };
  context.checkNear(
      attenuationNpPerMeter(base, biological, 1000.0, 1500.0, 200.0),
      1.0e-5 + 2.97377431260811885e-3,
      0.0, "base and biological attenuation add before cImag");
  context.checkNear(
      convertAttenuation(
          RawAttenuation{},
          VolumeAttenuation{
              .model = VolumeAttenuationModel::Biological,
              .parameters =
                  std::make_shared<const BiologicalAttenuationLayers>(
                      BiologicalAttenuationLayers{first})},
          1000.0, 1500.0, 100.0)
          .imaginarySoundSpeed,
      1.03069068469337122, 0.0,
      "biological resonance cImag matches the gfortran CRCI oracle");
  context.checkNear(
      biologicalAttenuationNpPerMeter({}, 200.0, 1000.0),
      0.0, 0.0, "zero biological layers are a supported no-op");
}

void testLegacyAdditionalUnits(Context& context) {
  constexpr double frequency = 500.0;
  constexpr double soundSpeed = 1600.0;

  const RawAttenuation decibels =
      rawAttenuation(0.2, AttenuationUnit::DecibelsPerMeter);
  context.checkNear(
      attenuationNpPerMeter(decibels, frequency, soundSpeed),
      0.2 / kDecibelsPerNeper, 1.0e-17,
      "M converts dB/m to Np/m");

  RawAttenuation powerLaw =
      rawAttenuation(0.2,
                     AttenuationUnit::DecibelsPerMeterPowerLaw);
  const double belowTransition =
      0.2 / kDecibelsPerNeper * std::pow(500.0 / 100.0, 1.5);
  context.checkNear(
      attenuationNpPerMeter(powerLaw, frequency, soundSpeed),
      belowTransition, 1.0e-16,
      "m applies the power law below the transition frequency");

  const double aboveTransition =
      0.2 / kDecibelsPerNeper * (2000.0 / 100.0) *
      std::pow(1000.0 / 100.0, 0.5);
  context.checkNear(
      attenuationNpPerMeter(powerLaw, 2000.0, soundSpeed),
      aboveTransition, 1.0e-16,
      "m becomes linear above the transition frequency");

  const RawAttenuation quality =
      rawAttenuation(100.0, AttenuationUnit::QualityFactor);
  context.checkNear(
      attenuationNpPerMeter(quality, frequency, soundSpeed),
      2.0 * std::numbers::pi * frequency /
          (2.0 * soundSpeed * 100.0),
      1.0e-17, "Q follows the legacy quality-factor conversion");

  const RawAttenuation loss =
      rawAttenuation(1.0e-5, AttenuationUnit::LossParameter);
  context.checkNear(
      attenuationNpPerMeter(loss, frequency, soundSpeed),
      1.0e-5 * 2.0 * std::numbers::pi * frequency / soundSpeed,
      1.0e-17, "L follows the legacy loss-parameter conversion");
}

void testRawInputIsImmutable(Context& context) {
  RawAttenuation attenuation =
      rawAttenuation(0.375,
                     AttenuationUnit::DecibelsPerMeterKilohertz);
  attenuation.referenceFrequency = 37.0;
  const RawAttenuation original = attenuation;

  static_cast<void>(convertAttenuation(attenuation, 250.0, 1500.0));

  context.check(attenuation.value == original.value,
                "conversion does not overwrite the raw attenuation value");
  context.check(attenuation.unit == original.unit,
                "conversion does not overwrite the raw attenuation unit");
  context.check(
      attenuation.referenceFrequency == original.referenceFrequency,
      "conversion does not overwrite the reference frequency");
  context.check(
      attenuation.powerLawExponent == original.powerLawExponent,
      "conversion does not overwrite the power-law exponent");
  context.check(
      attenuation.transitionFrequency == original.transitionFrequency,
      "conversion does not overwrite the transition frequency");
  RawAttenuation otherReference = attenuation;
  otherReference.referenceFrequency = 3700.0;
  context.checkNear(
      attenuationNpPerMeter(attenuation, 250.0, 1500.0),
      attenuationNpPerMeter(otherReference, 250.0, 1500.0), 0.0,
      "F does not misuse the power-law reference frequency");
}

void testInvalidAndUnsupportedInputs(Context& context) {
  const RawAttenuation valid =
      rawAttenuation(0.0, AttenuationUnit::NepersPerMeter);

  context.expectThrows<ValidationError>(
      [&valid] {
        static_cast<void>(convertAttenuation(valid, 0.0, 1500.0));
      },
      "non-positive frequency is rejected");
  context.expectThrows<ValidationError>(
      [&valid] {
        static_cast<void>(convertAttenuation(valid, 50.0, 0.0));
      },
      "non-positive sound speed is rejected");

  RawAttenuation invalid = valid;
  invalid.value = -1.0;
  context.expectThrows<ValidationError>(
      [&invalid] {
        static_cast<void>(convertAttenuation(invalid, 50.0, 1500.0));
      },
      "negative raw attenuation is rejected");

  invalid = valid;
  invalid.referenceFrequency =
      std::numeric_limits<double>::quiet_NaN();
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

  const VolumeAttenuation mismatched{
      .model = VolumeAttenuationModel::FrancoisGarrison};
  context.expectThrows<ValidationError>(
      [&valid, &mismatched] {
        static_cast<void>(convertAttenuation(
            valid, mismatched, 50.0, 1500.0, 0.0));
      },
      "mismatched volume parameters fail explicitly");

  const VolumeAttenuation invalidVolume{
      .model = static_cast<VolumeAttenuationModel>(999)};
  context.expectThrows<ValidationError>(
      [&valid, &invalidVolume] {
        static_cast<void>(convertAttenuation(
            valid, invalidVolume, 50.0, 1500.0, 0.0));
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
  testLegacyAdditionalUnits(context);
  testRawInputIsImmutable(context);
  testInvalidAndUnsupportedInputs(context);

  if (context.failureCount() != 0) {
    std::cerr << context.failureCount() << " test assertion(s) failed\n";
    return 1;
  }
  std::cout << "All Bellhop F2CPP attenuation tests passed\n";
  return 0;
}
