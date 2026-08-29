#include "rayreuse/model/cubic_spline_frequency_ssp.hpp"

#include <algorithm>
#include <cmath>

#include "rayreuse/acoustics/attenuation.hpp"
#include "rayreuse/error.hpp"

namespace rayreuse {
namespace {

// splinec.f90 forms SIXTH from default-real literals before assigning it to
// a real(8) PARAMETER, so the binary32-rounded value is part of the oracle.
constexpr double kFortranSixth = static_cast<double>(1.0F / 6.0F);

}  // namespace

CubicSplineFrequencySsp::CubicSplineFrequencySsp(
    const SoundSpeedProfile& profile, double frequency)
    : CubicSplineFrequencySsp(profile, frequency, VolumeAttenuation{}) {}

CubicSplineFrequencySsp::CubicSplineFrequencySsp(
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
  coefficients_ =
      computeCubicSplineCoefficients(depths_, nodeSoundSpeeds_);
}

double CubicSplineFrequencySsp::frequency() const noexcept {
  return frequency_;
}

std::size_t CubicSplineFrequencySsp::segmentCount() const noexcept {
  return realProfile_.segmentCount();
}

bool CubicSplineFrequencySsp::isLossless() const noexcept {
  return std::ranges::all_of(
      nodeSoundSpeeds_, [](const auto& value) { return value.imag() == 0.0; });
}

std::optional<std::complex<double>>
CubicSplineFrequencySsp::uniformComplexSoundSpeed() const noexcept {
  const std::complex<double> value = nodeSoundSpeeds_.front();
  if (!std::ranges::all_of(
          nodeSoundSpeeds_,
          [value](const auto& candidate) { return candidate == value; })) {
    return std::nullopt;
  }
  return value;
}

SoundSpeedSample CubicSplineFrequencySsp::evaluateComplex(
    SoundSpeedSample sample, double depth) const {
  const std::size_t index = sample.segmentIndex;
  const double offset = depth - depths_[index];
  const ComplexSplinePolynomial& polynomial = coefficients_[index];
  const std::complex<double> soundSpeed =
      polynomial.value +
      offset * (polynomial.derivative +
                offset * (0.5 * polynomial.curvature +
                          kFortranSixth * offset *
                              polynomial.thirdDerivative));
  const std::complex<double> gradient =
      polynomial.derivative +
      offset * (polynomial.curvature +
                0.5 * offset * polynomial.thirdDerivative);
  const std::complex<double> curvature =
      polynomial.curvature + offset * polynomial.thirdDerivative;
  sample.soundSpeed = soundSpeed.real();
  sample.imaginarySoundSpeed = soundSpeed.imag();
  sample.soundSpeedGradient.depth = gradient.real();
  sample.soundSpeedHessian.depthDepth = curvature.real();
  if (!std::isfinite(sample.soundSpeed) || sample.soundSpeed <= 0.0 ||
      !std::isfinite(sample.imaginarySoundSpeed) ||
      !std::isfinite(sample.soundSpeedGradient.depth) ||
      !std::isfinite(sample.soundSpeedHessian.depthDepth)) {
    throw ValidationError("frequency cubic-spline evaluation is invalid");
  }
  return sample;
}

SoundSpeedSample CubicSplineFrequencySsp::evaluateAtSegment(
    Vec2 position, std::size_t segmentIndex) const {
  return evaluateComplex(
      realProfile_.evaluateAtSegment(position, segmentIndex),
      position.depth);
}

SoundSpeedSample CubicSplineFrequencySsp::evaluate(
    Vec2 position, std::size_t previousSegment) const {
  return evaluateComplex(
      realProfile_.evaluate(position, previousSegment), position.depth);
}

}  // namespace rayreuse
