#include "rayreuse/model/quadrilateral_frequency_ssp.hpp"

#include <algorithm>
#include <cmath>
#include <ranges>

#include "rayreuse/acoustics/attenuation.hpp"
#include "rayreuse/error.hpp"

namespace rayreuse {

QuadrilateralFrequencySsp::QuadrilateralFrequencySsp(
    const SoundSpeedProfile& profile, double frequency)
    : QuadrilateralFrequencySsp(profile, frequency, VolumeAttenuation{}) {}

QuadrilateralFrequencySsp::QuadrilateralFrequencySsp(
    const SoundSpeedProfile& profile, double frequency,
    const VolumeAttenuation& volumeAttenuation)
    : frequency_(frequency),
      realProfile_(profile),
      grid_(profile.quadrilateralGrid()) {
  if (!std::isfinite(frequency_) || frequency_ <= 0.0) {
    throw ValidationError("frequency must be finite and positive");
  }

  const std::vector<SoundSpeedPoint>& points = profile.points();
  depths_.reserve(points.size());
  imaginarySoundSpeeds_.reserve(points.size());
  for (const SoundSpeedPoint& point : points) {
    depths_.push_back(point.depth);
    // Preserve Origin's node-first ordering.  In particular, point.soundSpeed
    // is the ENV reference value and must not be replaced by a Q-matrix value.
    imaginarySoundSpeeds_.push_back(
        convertAttenuation(point.attenuation, volumeAttenuation, frequency_,
                           point.soundSpeed, point.depth)
            .imaginarySoundSpeed);
  }
}

double QuadrilateralFrequencySsp::frequency() const noexcept {
  return frequency_;
}

std::size_t QuadrilateralFrequencySsp::segmentCount() const noexcept {
  return realProfile_.segmentCount();
}

std::size_t QuadrilateralFrequencySsp::rangeSegmentCount() const noexcept {
  return realProfile_.rangeSegmentCount();
}

bool QuadrilateralFrequencySsp::isLossless() const noexcept {
  return std::ranges::all_of(
      imaginarySoundSpeeds_,
      [](double value) { return value == 0.0; });
}

std::optional<std::complex<double>>
QuadrilateralFrequencySsp::uniformComplexSoundSpeed() const noexcept {
  const double realValue = grid_->speedsDepthMajor.front();
  const double imaginaryValue = imaginarySoundSpeeds_.front();
  const bool uniformReal = std::ranges::all_of(
      grid_->speedsDepthMajor,
      [realValue](double value) { return value == realValue; });
  const bool uniformImaginary = std::ranges::all_of(
      imaginarySoundSpeeds_, [imaginaryValue](double value) {
        return value == imaginaryValue;
      });
  if (!uniformReal || !uniformImaginary) {
    return std::nullopt;
  }
  return std::complex<double>{realValue, imaginaryValue};
}

std::size_t QuadrilateralFrequencySsp::locateSegment(
    double depth, std::size_t previousSegment) const {
  return realProfile_.locateSegment(depth, previousSegment);
}

std::size_t QuadrilateralFrequencySsp::locateRangeSegment(
    double range, std::size_t previousRangeSegment) const {
  return realProfile_.locateRangeSegment(range, previousRangeSegment);
}

double QuadrilateralFrequencySsp::minimumRangeForSegment(
    std::size_t rangeSegmentIndex) const {
  return realProfile_.minimumRangeForSegment(rangeSegmentIndex);
}

double QuadrilateralFrequencySsp::maximumRangeForSegment(
    std::size_t rangeSegmentIndex) const {
  return realProfile_.maximumRangeForSegment(rangeSegmentIndex);
}

SoundSpeedSample QuadrilateralFrequencySsp::addImaginarySoundSpeed(
    SoundSpeedSample sample, double depth) const {
  const std::size_t index = sample.segmentIndex;
  const double weight =
      (depth - depths_[index]) /
      (depths_[index + 1U] - depths_[index]);
  sample.imaginarySoundSpeed =
      (1.0 - weight) * imaginarySoundSpeeds_[index] +
      weight * imaginarySoundSpeeds_[index + 1U];
  if (!std::isfinite(sample.imaginarySoundSpeed) ||
      sample.imaginarySoundSpeed < 0.0) {
    throw ValidationError(
        "interpolated imaginary sound speed must be finite and "
        "non-negative");
  }
  return sample;
}

SoundSpeedSample QuadrilateralFrequencySsp::evaluateAtSegment(
    Vec2 position, std::size_t segmentIndex) const {
  return addImaginarySoundSpeed(
      realProfile_.evaluateAtSegment(position, segmentIndex),
      position.depth);
}

SoundSpeedSample QuadrilateralFrequencySsp::evaluateAtSegments(
    Vec2 position, std::size_t segmentIndex,
    std::size_t rangeSegmentIndex) const {
  return addImaginarySoundSpeed(
      realProfile_.evaluateAtSegments(
          position, segmentIndex, rangeSegmentIndex),
      position.depth);
}

SoundSpeedSample QuadrilateralFrequencySsp::evaluate(
    Vec2 position, std::size_t previousSegment) const {
  return addImaginarySoundSpeed(
      realProfile_.evaluate(position, previousSegment), position.depth);
}

SoundSpeedSample QuadrilateralFrequencySsp::evaluate(
    Vec2 position, std::size_t previousSegment,
    std::size_t previousRangeSegment) const {
  return addImaginarySoundSpeed(
      realProfile_.evaluate(
          position, previousSegment, previousRangeSegment),
      position.depth);
}

}  // namespace rayreuse
