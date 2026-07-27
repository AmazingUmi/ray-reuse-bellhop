#pragma once

#include <array>
#include <complex>
#include <string_view>

namespace bellhop::test {

// Production-type-independent inputs for M2-02.  Keeping these POD fixtures
// separate from BoundaryModel and the M2-01 attenuation API lets their tests
// remain stable while those interfaces settle.
struct HalfSpaceCoefficientFixture {
  std::string_view name;
  double frequencyHz;
  double waterDensity;
  double compressionalSoundSpeed;
  double attenuationDecibelsPerWavelength;
  double compressionalImaginarySoundSpeed;
  double halfSpaceDensity;
  double tangentSlowness;
  double outwardNormalSlowness;
  std::complex<double> expectedRawCoefficient;
  bool expectedSuppressed;
};

// The first three rows reproduce the first acoustic-bottom event of
// constant_speed_acoustic_bottom at 100/250/500 Hz.  The input attenuation is
// 0.5 dB/wavelength, whose Bellhop CRCI conversion gives the frequency-
// independent imaginary speed below.  The 250 Hz expected coefficient is
// independently recorded by Fortran ray-oracle schema v2.
inline constexpr double kAcousticBottomImaginarySpeed =
    1.4567095091567458e+1;
inline constexpr double kAcousticBottomTangentSlowness =
    1.16349376248555840e-5;
inline constexpr double kAcousticBottomNormalSlowness =
    6.66565130104260846e-4;
inline constexpr std::complex<double> kAcousticBottomRawCoefficient{
    1.19750743411546456e-1, 4.51660947385581959e-3};

inline constexpr std::array<HalfSpaceCoefficientFixture, 7>
    kHalfSpaceCoefficientFixtures{{
        {
            .name = "lossy_acoustic_bottom_100_hz",
            .frequencyHz = 100.0,
            .waterDensity = 1000.0,
            .compressionalSoundSpeed = 1590.0,
            .attenuationDecibelsPerWavelength = 0.5,
            .compressionalImaginarySoundSpeed =
                kAcousticBottomImaginarySpeed,
            .halfSpaceDensity = 1200.0,
            .tangentSlowness = kAcousticBottomTangentSlowness,
            .outwardNormalSlowness =
                kAcousticBottomNormalSlowness,
            .expectedRawCoefficient =
                kAcousticBottomRawCoefficient,
            .expectedSuppressed = false,
        },
        {
            .name = "lossy_acoustic_bottom_250_hz_fortran_oracle",
            .frequencyHz = 250.0,
            .waterDensity = 1000.0,
            .compressionalSoundSpeed = 1590.0,
            .attenuationDecibelsPerWavelength = 0.5,
            .compressionalImaginarySoundSpeed =
                kAcousticBottomImaginarySpeed,
            .halfSpaceDensity = 1200.0,
            .tangentSlowness = kAcousticBottomTangentSlowness,
            .outwardNormalSlowness =
                kAcousticBottomNormalSlowness,
            .expectedRawCoefficient =
                kAcousticBottomRawCoefficient,
            .expectedSuppressed = false,
        },
        {
            .name = "lossy_acoustic_bottom_500_hz",
            .frequencyHz = 500.0,
            .waterDensity = 1000.0,
            .compressionalSoundSpeed = 1590.0,
            .attenuationDecibelsPerWavelength = 0.5,
            .compressionalImaginarySoundSpeed =
                kAcousticBottomImaginarySpeed,
            .halfSpaceDensity = 1200.0,
            .tangentSlowness = kAcousticBottomTangentSlowness,
            .outwardNormalSlowness =
                kAcousticBottomNormalSlowness,
            .expectedRawCoefficient =
                kAcousticBottomRawCoefficient,
            .expectedSuppressed = false,
        },
        {
            .name = "lossy_acoustic_bottom_positive_45_degrees",
            .frequencyHz = 250.0,
            .waterDensity = 1000.0,
            .compressionalSoundSpeed = 1590.0,
            .attenuationDecibelsPerWavelength = 0.5,
            .compressionalImaginarySoundSpeed =
                kAcousticBottomImaginarySpeed,
            .halfSpaceDensity = 1200.0,
            .tangentSlowness = 4.71404520791031639e-4,
            .outwardNormalSlowness =
                4.71404520791031639e-4,
            .expectedRawCoefficient = {
                1.5203227482134690e-1, 1.0210756168578604e-2},
            .expectedSuppressed = false,
        },
        {
            .name = "lossy_acoustic_bottom_negative_45_degrees",
            .frequencyHz = 250.0,
            .waterDensity = 1000.0,
            .compressionalSoundSpeed = 1590.0,
            .attenuationDecibelsPerWavelength = 0.5,
            .compressionalImaginarySoundSpeed =
                kAcousticBottomImaginarySpeed,
            .halfSpaceDensity = 1200.0,
            .tangentSlowness = -4.71404520791031639e-4,
            .outwardNormalSlowness =
                4.71404520791031639e-4,
            .expectedRawCoefficient = {
                1.5203227482134690e-1, 1.0210756168578604e-2},
            .expectedSuppressed = false,
        },
        {
            .name = "matched_lossless_half_space_is_killed",
            .frequencyHz = 250.0,
            .waterDensity = 1000.0,
            .compressionalSoundSpeed = 1500.0,
            .attenuationDecibelsPerWavelength = 0.0,
            .compressionalImaginarySoundSpeed = 0.0,
            .halfSpaceDensity = 1000.0,
            .tangentSlowness = 0.0,
            .outwardNormalSlowness = 1.0 / 1500.0,
            .expectedRawCoefficient = {0.0, 0.0},
            .expectedSuppressed = true,
        },
        {
            .name = "munk_50_hz_alpha_1000_fortran_oracle",
            .frequencyHz = 50.0,
            .waterDensity = 1000.0,
            .compressionalSoundSpeed = 1600.0,
            .attenuationDecibelsPerWavelength = 0.8,
            .compressionalImaginarySoundSpeed =
                2.3453939266926216e+1,
            .halfSpaceDensity = 1800.0,
            .tangentSlowness = 6.24684579927731513e-4,
            .outwardNormalSlowness = 1.58041869239525804e-4,
            .expectedRawCoefficient = {
                5.12048537181218011e-1, 3.15260767533924291e-1},
            .expectedSuppressed = false,
        },
    }};

struct RawCoefficientApplicationFixture {
  std::string_view name;
  std::complex<double> rawCoefficient;
  bool acousticHalfSpace;
  double incomingAmplitude;
  double incomingUnwrappedPhase;
  double expectedAmplitude;
  double expectedUnwrappedPhase;
  double expectedWrappedPhase;
  bool expectedSuppressed;
};

inline constexpr double kPi = 3.14159265358979323846;
// ReflectMod.f90 spells the cutoff as default-real 1.0E-5.  With the
// repository's normal gfortran flags, comparison promotes this binary32 value
// to double rather than comparing against the nearest binary64 1e-5.
inline constexpr double kLegacyCoefficientKillThreshold =
    9.9999997473787516e-6;

// Fortran keeps cumulative reflection phase unwrapped.  Tests compare phase
// modulo 2*pi using atan2(sin(delta), cos(delta)); expectedWrappedPhase records
// that comparison form without changing the stored cumulative value.
inline constexpr std::array<RawCoefficientApplicationFixture, 6>
    kRawCoefficientApplicationFixtures{{
        {
            .name = "rigid_keeps_amplitude_and_phase",
            .rawCoefficient = {1.0, 0.0},
            .acousticHalfSpace = false,
            .incomingAmplitude = 2.0,
            .incomingUnwrappedPhase = 3.0,
            .expectedAmplitude = 2.0,
            .expectedUnwrappedPhase = 3.0,
            .expectedWrappedPhase = 3.0,
            .expectedSuppressed = false,
        },
        {
            .name = "vacuum_adds_positive_pi_without_state_wrapping",
            .rawCoefficient = {-1.0, 0.0},
            .acousticHalfSpace = false,
            .incomingAmplitude = 2.0,
            .incomingUnwrappedPhase = 3.0,
            .expectedAmplitude = 2.0,
            .expectedUnwrappedPhase = 3.0 + kPi,
            .expectedWrappedPhase = -1.4159265358979337e-1,
            .expectedSuppressed = false,
        },
        {
            .name = "raw_complex_coefficient_scales_and_adds_arg",
            .rawCoefficient = {
                4.3879128094518638e-1, 2.3971276930210150e-1},
            .acousticHalfSpace = true,
            .incomingAmplitude = 2.0,
            .incomingUnwrappedPhase = 3.0,
            .expectedAmplitude = 1.0,
            .expectedUnwrappedPhase = 3.5,
            .expectedWrappedPhase = -2.7831853071795867,
            .expectedSuppressed = false,
        },
        {
            .name = "zero_raw_coefficient_kills_without_phase_change",
            .rawCoefficient = {0.0, 0.0},
            .acousticHalfSpace = true,
            .incomingAmplitude = 2.0,
            .incomingUnwrappedPhase = 3.0,
            .expectedAmplitude = 0.0,
            .expectedUnwrappedPhase = 3.0,
            .expectedWrappedPhase = 3.0,
            .expectedSuppressed = true,
        },
        {
            .name = "strictly_below_threshold_is_killed",
            .rawCoefficient = {9.9999990e-6, 0.0},
            .acousticHalfSpace = true,
            .incomingAmplitude = 2.0,
            .incomingUnwrappedPhase = 3.0,
            .expectedAmplitude = 0.0,
            .expectedUnwrappedPhase = 3.0,
            .expectedWrappedPhase = 3.0,
            .expectedSuppressed = true,
        },
        {
            .name = "exact_threshold_is_not_killed",
            .rawCoefficient = {
                kLegacyCoefficientKillThreshold, 0.0},
            .acousticHalfSpace = true,
            .incomingAmplitude = 2.0,
            .incomingUnwrappedPhase = 3.0,
            .expectedAmplitude =
                2.0 * kLegacyCoefficientKillThreshold,
            .expectedUnwrappedPhase = 3.0,
            .expectedWrappedPhase = 3.0,
            .expectedSuppressed = false,
        },
    }};

}  // namespace bellhop::test
