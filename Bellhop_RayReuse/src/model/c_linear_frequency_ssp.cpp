#include "rayreuse/model/c_linear_frequency_ssp.hpp"

#include <algorithm>
#include <cmath>

#include "rayreuse/acoustics/attenuation.hpp"
#include "rayreuse/error.hpp"

namespace rayreuse {

CLinearFrequencySsp::CLinearFrequencySsp(const SoundSpeedProfile& profile,
                                         double frequency)
    : CLinearFrequencySsp(profile, frequency, VolumeAttenuation{}) {}

CLinearFrequencySsp::CLinearFrequencySsp(
    const SoundSpeedProfile& profile, double frequency,
    const VolumeAttenuation& volumeAttenuation)
    : frequency_(frequency), realProfile_(profile) {
  if (!std::isfinite(frequency_) || frequency_ <= 0.0) {
    throw ValidationError("frequency must be finite and positive");
  }

  const std::vector<SoundSpeedPoint>& points = profile.points();
  depths_.reserve(points.size());
  realSoundSpeeds_.reserve(points.size());
  imaginarySoundSpeeds_.reserve(points.size());
  for (const SoundSpeedPoint& point : points) {
    depths_.push_back(point.depth);
    realSoundSpeeds_.push_back(point.soundSpeed);
    imaginarySoundSpeeds_.push_back(
        convertAttenuation(point.attenuation, volumeAttenuation, frequency_,
                           point.soundSpeed, point.depth)
            .imaginarySoundSpeed);
  }
}

double CLinearFrequencySsp::frequency() const noexcept { return frequency_; }

std::size_t CLinearFrequencySsp::segmentCount() const noexcept {
  return realProfile_.segmentCount();
}

bool CLinearFrequencySsp::isLossless() const noexcept {
  return std::ranges::all_of(imaginarySoundSpeeds_,
                             [](double value) { return value == 0.0; });
}

std::optional<std::complex<double>>
CLinearFrequencySsp::uniformComplexSoundSpeed() const noexcept {
  const double realValue = realSoundSpeeds_.front();
  const double imaginaryValue = imaginarySoundSpeeds_.front();
  const bool uniformReal = std::ranges::all_of(
      realSoundSpeeds_,
      [realValue](double value) { return value == realValue; });
  const bool uniformImaginary = std::ranges::all_of(
      imaginarySoundSpeeds_,
      [imaginaryValue](double value) { return value == imaginaryValue; });
  if (!uniformReal || !uniformImaginary) {
    return std::nullopt;
  }
  return std::complex<double>{realValue, imaginaryValue};
}

SoundSpeedSample CLinearFrequencySsp::addImaginarySoundSpeed(
    SoundSpeedSample sample, double depth) const {
  const std::size_t index = sample.segmentIndex;
  const double weight =
      (depth - depths_[index]) / (depths_[index + 1U] - depths_[index]);
  sample.imaginarySoundSpeed = (1.0 - weight) * imaginarySoundSpeeds_[index] +
                               weight * imaginarySoundSpeeds_[index + 1U];
  if (!std::isfinite(sample.imaginarySoundSpeed) ||
      sample.imaginarySoundSpeed < 0.0) {
    throw ValidationError(
        "interpolated imaginary sound speed must be finite and "
        "non-negative");
  }
  return sample;
}

SoundSpeedSample CLinearFrequencySsp::evaluateAtSegment(
    Vec2 position, std::size_t segmentIndex) const {
  return addImaginarySoundSpeed(
      realProfile_.evaluateAtSegment(position, segmentIndex), position.depth);
}

SoundSpeedSample CLinearFrequencySsp::evaluate(
    Vec2 position, std::size_t previousSegment) const {
  return addImaginarySoundSpeed(
      realProfile_.evaluate(position, previousSegment), position.depth);
}

}  // namespace rayreuse
