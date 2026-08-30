#pragma once

#include <complex>
#include <cstddef>
#include <optional>
#include <vector>

#include "rayreuse/model/pchip_ssp.hpp"

namespace rayreuse {

class PchipFrequencySsp {
 public:
  PchipFrequencySsp(const SoundSpeedProfile& profile, double frequency);
  PchipFrequencySsp(const SoundSpeedProfile& profile, double frequency,
                    const VolumeAttenuation& volumeAttenuation);

  [[nodiscard]] static constexpr SspGradientContinuity
  gradientContinuity() noexcept {
    return sspGradientContinuity(SspInterpolationKind::Pchip);
  }
  [[nodiscard]] double frequency() const noexcept;
  [[nodiscard]] std::size_t segmentCount() const noexcept;
  [[nodiscard]] bool isLossless() const noexcept;
  [[nodiscard]] std::optional<std::complex<double>> uniformComplexSoundSpeed()
      const noexcept;
  [[nodiscard]] SoundSpeedSample evaluateAtSegment(
      Vec2 position, std::size_t segmentIndex) const;
  [[nodiscard]] SoundSpeedSample evaluate(Vec2 position,
                                          std::size_t previousSegment) const;

 private:
  [[nodiscard]] SoundSpeedSample addImaginarySoundSpeed(SoundSpeedSample sample,
                                                        double depth) const;

  double frequency_{};
  PchipSsp realProfile_;
  std::vector<double> depths_;
  std::vector<std::complex<double>> nodeSoundSpeeds_;
  std::vector<ComplexCubicPolynomial> coefficients_;
};

}  // namespace rayreuse
