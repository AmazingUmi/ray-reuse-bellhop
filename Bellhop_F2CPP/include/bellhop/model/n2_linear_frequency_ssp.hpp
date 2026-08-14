#pragma once

#include <complex>
#include <cstddef>
#include <optional>
#include <vector>

#include "bellhop/model/n2_linear_ssp.hpp"

namespace bellhop {

class N2LinearFrequencySsp {
 public:
  N2LinearFrequencySsp(const SoundSpeedProfile& profile, double frequency);
  N2LinearFrequencySsp(const SoundSpeedProfile& profile, double frequency,
                       const VolumeAttenuation& volumeAttenuation);

  [[nodiscard]] static constexpr SspGradientContinuity gradientContinuity()
      noexcept {
    return sspGradientContinuity(SspInterpolationKind::N2Linear);
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
  struct Segment {
    std::complex<double> n2AtMinimumDepth{};
    std::complex<double> n2DepthGradient{};
  };

  [[nodiscard]] SoundSpeedSample addComplexSoundSpeed(
      SoundSpeedSample sample, double depth) const;

  double frequency_{};
  N2LinearSsp realProfile_;
  std::vector<double> depths_;
  std::vector<std::complex<double>> nodeSoundSpeeds_;
  std::vector<Segment> segments_;
};

}  // namespace bellhop
