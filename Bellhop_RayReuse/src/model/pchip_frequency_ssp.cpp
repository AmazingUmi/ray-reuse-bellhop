#include "rayreuse/model/pchip_frequency_ssp.hpp"

#include <algorithm>
#include <cmath>

#include "rayreuse/acoustics/attenuation.hpp"
#include "rayreuse/error.hpp"

namespace rayreuse {

PchipFrequencySsp::PchipFrequencySsp(
    const SoundSpeedProfile& profile, double frequency)
    : PchipFrequencySsp(profile, frequency, VolumeAttenuation{}) {}

PchipFrequencySsp::PchipFrequencySsp(
    const SoundSpeedProfile& profile, double frequency,
    const VolumeAttenuation& volumeAttenuation)
    : frequency_(frequency), realProfile_(profile) {
  if (!std::isfinite(frequency_) || frequency_ <= 0.0) {
    throw ValidationError("frequency must be finite and positive");
  }
  for (const SoundSpeedPoint& point : profile.points()) {
    depths_.push_back(point.depth);
    nodeSoundSpeeds_.emplace_back(
        point.soundSpeed,
        convertAttenuation(point.attenuation, volumeAttenuation, frequency_,
                           point.soundSpeed, point.depth)
            .imaginarySoundSpeed);
  }
  coefficients_ = computePchipCoefficients(depths_, nodeSoundSpeeds_);
}

double PchipFrequencySsp::frequency() const noexcept { return frequency_; }

std::size_t PchipFrequencySsp::segmentCount() const noexcept {
  return realProfile_.segmentCount();
}

bool PchipFrequencySsp::isLossless() const noexcept {
  return std::ranges::all_of(
      nodeSoundSpeeds_, [](const auto& value) { return value.imag() == 0.0; });
}

std::optional<std::complex<double>>
PchipFrequencySsp::uniformComplexSoundSpeed() const noexcept {
  const std::complex<double> value = nodeSoundSpeeds_.front();
  if (!std::ranges::all_of(
          nodeSoundSpeeds_,
          [value](const auto& candidate) { return candidate == value; })) {
    return std::nullopt;
  }
  return value;
}

SoundSpeedSample PchipFrequencySsp::addImaginarySoundSpeed(
    SoundSpeedSample sample, double depth) const {
  const std::size_t index = sample.segmentIndex;
  const double offset = depth - depths_[index];
  const ComplexCubicPolynomial& polynomial = coefficients_[index];
  sample.imaginarySoundSpeed =
      std::imag(polynomial.constant +
                offset * (polynomial.linear +
                          offset * (polynomial.quadratic +
                                    offset * polynomial.cubic)));
  if (!std::isfinite(sample.imaginarySoundSpeed) ||
      sample.imaginarySoundSpeed < 0.0) {
    throw ValidationError(
        "interpolated imaginary sound speed must be finite and "
        "non-negative");
  }
  return sample;
}

SoundSpeedSample PchipFrequencySsp::evaluateAtSegment(
    Vec2 position, std::size_t segmentIndex) const {
  return addImaginarySoundSpeed(
      realProfile_.evaluateAtSegment(position, segmentIndex),
      position.depth);
}

SoundSpeedSample PchipFrequencySsp::evaluate(
    Vec2 position, std::size_t previousSegment) const {
  return addImaginarySoundSpeed(
      realProfile_.evaluate(position, previousSegment), position.depth);
}

}  // namespace rayreuse
