#pragma once

#include <complex>
#include <cstddef>
#include <optional>
#include <vector>

#include "rayreuse/model/n2_linear_ssp.hpp"

namespace rayreuse {

// Frequency-dependent view of an N²-linear SSP, migrated line by line from
// the Bellhop F2CPP production implementation.
//
// Every tabulated SSP node is converted to complex sound speed at the target
// frequency before the complex N² coefficients are formed, and the sound
// speed is recovered through the principal complex square root.  The gradient
// and curvature deliberately keep F2CPP's real-part observable rather than a
// complex analytic derivative.
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
  // These queries let the frequency projector bypass repeated N² evaluation
  // without changing the general nonuniform path.
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

}  // namespace rayreuse
