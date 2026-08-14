#include "bellhop/model/n2_linear_frequency_ssp.hpp"

#include <algorithm>
#include <cmath>

#include "bellhop/acoustics/attenuation.hpp"
#include "bellhop/error.hpp"

namespace bellhop {

N2LinearFrequencySsp::N2LinearFrequencySsp(
    const SoundSpeedProfile& profile, double frequency)
    : N2LinearFrequencySsp(profile, frequency, VolumeAttenuation{}) {}

N2LinearFrequencySsp::N2LinearFrequencySsp(
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
        convertAttenuation(
            point.attenuation, volumeAttenuation, frequency_,
            point.soundSpeed, point.depth)
            .imaginarySoundSpeed);
  }
  segments_.reserve(nodeSoundSpeeds_.size() - 1U);
  for (std::size_t index = 0U; index + 1U < nodeSoundSpeeds_.size(); ++index) {
    const std::complex<double> firstN2 =
        1.0 / (nodeSoundSpeeds_[index] * nodeSoundSpeeds_[index]);
    const std::complex<double> secondN2 =
        1.0 /
        (nodeSoundSpeeds_[index + 1U] * nodeSoundSpeeds_[index + 1U]);
    const std::complex<double> gradient =
        (secondN2 - firstN2) /
        (depths_[index + 1U] - depths_[index]);
    if (!std::isfinite(firstN2.real()) || !std::isfinite(firstN2.imag()) ||
        !std::isfinite(gradient.real()) ||
        !std::isfinite(gradient.imag())) {
      throw ValidationError("frequency N2-linear coefficient must be finite");
    }
    segments_.push_back(
        {.n2AtMinimumDepth = firstN2, .n2DepthGradient = gradient});
  }
}

double N2LinearFrequencySsp::frequency() const noexcept { return frequency_; }

std::size_t N2LinearFrequencySsp::segmentCount() const noexcept {
  return realProfile_.segmentCount();
}

bool N2LinearFrequencySsp::isLossless() const noexcept {
  return std::ranges::all_of(
      nodeSoundSpeeds_, [](const auto& value) { return value.imag() == 0.0; });
}

std::optional<std::complex<double>>
N2LinearFrequencySsp::uniformComplexSoundSpeed() const noexcept {
  const std::complex<double> value = nodeSoundSpeeds_.front();
  if (!std::ranges::all_of(
          nodeSoundSpeeds_,
          [value](const auto& candidate) { return candidate == value; })) {
    return std::nullopt;
  }
  return value;
}

SoundSpeedSample N2LinearFrequencySsp::addComplexSoundSpeed(
    SoundSpeedSample sample, double depth) const {
  const std::size_t index = sample.segmentIndex;
  const std::complex<double> n2 =
      segments_[index].n2AtMinimumDepth +
      (depth - depths_[index]) * segments_[index].n2DepthGradient;
  const std::complex<double> soundSpeed = 1.0 / std::sqrt(n2);
  sample.soundSpeed = soundSpeed.real();
  sample.imaginarySoundSpeed = soundSpeed.imag();
  sample.soundSpeedGradient.depth =
      -0.5 * sample.soundSpeed * sample.soundSpeed * sample.soundSpeed *
      segments_[index].n2DepthGradient.real();
  sample.soundSpeedHessian.depthDepth =
      3.0 * sample.soundSpeedGradient.depth *
      sample.soundSpeedGradient.depth / sample.soundSpeed;
  if (!std::isfinite(sample.soundSpeed) || sample.soundSpeed <= 0.0 ||
      !std::isfinite(sample.imaginarySoundSpeed) ||
      sample.imaginarySoundSpeed < 0.0 ||
      !std::isfinite(sample.soundSpeedGradient.depth) ||
      !std::isfinite(sample.soundSpeedHessian.depthDepth)) {
    throw ValidationError("frequency N2-linear evaluation is invalid");
  }
  return sample;
}

SoundSpeedSample N2LinearFrequencySsp::evaluateAtSegment(
    Vec2 position, std::size_t segmentIndex) const {
  return addComplexSoundSpeed(
      realProfile_.evaluateAtSegment(position, segmentIndex), position.depth);
}

SoundSpeedSample N2LinearFrequencySsp::evaluate(
    Vec2 position, std::size_t previousSegment) const {
  return addComplexSoundSpeed(
      realProfile_.evaluate(position, previousSegment), position.depth);
}

}  // namespace bellhop
