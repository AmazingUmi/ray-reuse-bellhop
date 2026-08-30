#pragma once

#include <complex>
#include <cstddef>
#include <optional>
#include <vector>

#include "rayreuse/model/cubic_spline_ssp.hpp"

namespace rayreuse {

// Target-frequency view of a cubic-spline SSP, migrated line by line from the
// Bellhop F2CPP production implementation.
//
// Every tabulated SSP node is converted to complex sound speed at the target
// frequency before the complex not-a-knot spline coefficients are formed with
// the shared A01 kernel, so attenuation is never applied at query points.
// The observable sample takes density and segment identity from the real
// evaluator and then overrides the sound speed pair and the real parts of the
// gradient and Hessian exactly in the F2CPP order. Interior imaginary sound
// speed is validated finite only; no PCHIP-style non-negativity constraint
// exists on the spline interior.
class CubicSplineFrequencySsp {
 public:
  CubicSplineFrequencySsp(const SoundSpeedProfile& profile, double frequency);
  CubicSplineFrequencySsp(const SoundSpeedProfile& profile, double frequency,
                          const VolumeAttenuation& volumeAttenuation);

  [[nodiscard]] static constexpr SspGradientContinuity
  gradientContinuity() noexcept {
    return sspGradientContinuity(SspInterpolationKind::CubicSpline);
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
  [[nodiscard]] SoundSpeedSample evaluateComplex(SoundSpeedSample sample,
                                                 double depth) const;

  double frequency_{};
  CubicSplineSsp realProfile_;
  std::vector<double> depths_;
  std::vector<std::complex<double>> nodeSoundSpeeds_;
  std::vector<ComplexSplinePolynomial> coefficients_;
};

}  // namespace rayreuse
