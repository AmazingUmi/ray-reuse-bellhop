#pragma once

#include <complex>
#include <cstddef>
#include <optional>
#include <vector>

#include "rayreuse/model/c_linear_ssp.hpp"

namespace rayreuse {

// Frequency-dependent view of a C-linear SSP.
//
// Bellhop converts every tabulated SSP node to complex sound speed before it
// forms the C-linear slope.  Interpolating raw attenuation or converting the
// already-interpolated real sound speed is not equivalent for all attenuation
// models, so this class retains the converted imaginary value at every node.
class CLinearFrequencySsp {
 public:
  CLinearFrequencySsp(const SoundSpeedProfile& profile, double frequency);
  CLinearFrequencySsp(const SoundSpeedProfile& profile, double frequency,
                      const VolumeAttenuation& volumeAttenuation);

  [[nodiscard]] static constexpr SspGradientContinuity gradientContinuity()
      noexcept {
    return sspGradientContinuity(SspInterpolationKind::CLinear);
  }

  [[nodiscard]] double frequency() const noexcept;
  [[nodiscard]] std::size_t segmentCount() const noexcept;
  // These queries let the frequency projector bypass repeated C-linear
  // evaluation without changing the general nonuniform path.
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

  double frequency_;
  CLinearSsp realProfile_;
  std::vector<double> depths_;
  std::vector<double> realSoundSpeeds_;
  std::vector<double> imaginarySoundSpeeds_;
};

}  // namespace rayreuse
