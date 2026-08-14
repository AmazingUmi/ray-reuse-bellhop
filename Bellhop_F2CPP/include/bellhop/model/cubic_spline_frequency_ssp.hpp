#pragma once

#include <complex>
#include <cstddef>
#include <optional>
#include <vector>

#include "bellhop/model/cubic_spline_ssp.hpp"

namespace bellhop {

class CubicSplineFrequencySsp {
 public:
  CubicSplineFrequencySsp(
      const SoundSpeedProfile& profile, double frequency);
  CubicSplineFrequencySsp(
      const SoundSpeedProfile& profile, double frequency,
      const VolumeAttenuation& volumeAttenuation);

  [[nodiscard]] static constexpr SspGradientContinuity gradientContinuity()
      noexcept {
    return sspGradientContinuity(SspInterpolationKind::CubicSpline);
  }
  [[nodiscard]] double frequency() const noexcept;
  [[nodiscard]] std::size_t segmentCount() const noexcept;
  [[nodiscard]] bool isLossless() const noexcept;
  [[nodiscard]] std::optional<std::complex<double>>
  uniformComplexSoundSpeed() const noexcept;
  [[nodiscard]] SoundSpeedSample evaluateAtSegment(
      Vec2 position, std::size_t segmentIndex) const;
  [[nodiscard]] SoundSpeedSample evaluate(
      Vec2 position, std::size_t previousSegment) const;

 private:
  [[nodiscard]] SoundSpeedSample evaluateComplex(
      SoundSpeedSample sample, double depth) const;

  double frequency_{};
  CubicSplineSsp realProfile_;
  std::vector<double> depths_;
  std::vector<std::complex<double>> nodeSoundSpeeds_;
  std::vector<ComplexSplinePolynomial> coefficients_;
};

}  // namespace bellhop
