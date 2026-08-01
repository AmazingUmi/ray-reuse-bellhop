#pragma once

#include "rayreuse/model/environment.hpp"

namespace rayreuse {

struct AttenuationConversion {
  double attenuationNpPerMeter{};
  double imaginarySoundSpeed{};
};

// Converts the immutable parser-facing attenuation specification at one
// frequency.  AttenuationUnit::DecibelsPerMeterKilohertz follows legacy
// Bellhop code F exactly: its raw value is dB/(m*kHz), not dB/(km*kHz).
[[nodiscard]] double attenuationNpPerMeter(const RawAttenuation& attenuation,
                                           double frequency, double soundSpeed);

// Updated Thorp formula used by AttenMod.f90, returned in Np/m.
[[nodiscard]] double thorpAttenuationNpPerMeter(double frequency);

// Bellhop's positive-imaginary-sound-speed convention:
// cImag = attenuationNpPerMeter * c^2 / angularFrequency.
[[nodiscard]] double imaginarySoundSpeedFromAttenuation(
    double attenuationNpPerMeter, double frequency, double soundSpeed);

[[nodiscard]] AttenuationConversion convertAttenuation(
    const RawAttenuation& attenuation, double frequency, double soundSpeed);

}  // namespace rayreuse
