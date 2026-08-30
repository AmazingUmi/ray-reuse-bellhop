#pragma once

#include <complex>
#include <cstddef>
#include <optional>
#include <vector>

#include "rayreuse/model/quadrilateral_ssp.hpp"

namespace rayreuse {

// Frequency-dependent view of Origin Bellhop's two-dimensional `Q` SSP,
// migrated line by line from the Bellhop F2CPP production implementation.
//
// Quad obtains real sound speed and all of its spatial derivatives from the
// range-dependent SSP matrix.  Its imaginary sound speed is different: every
// ENV reference-profile node is converted at the requested frequency first,
// using that node's reference real sound speed and raw attenuation; the
// converted values are then interpolated only in depth.  RayReuse carries the
// explicit environment volume attenuation model and node depth.
class QuadrilateralFrequencySsp {
 public:
  QuadrilateralFrequencySsp(const SoundSpeedProfile& profile, double frequency);
  QuadrilateralFrequencySsp(const SoundSpeedProfile& profile, double frequency,
                            const VolumeAttenuation& volumeAttenuation);

  [[nodiscard]] static constexpr SspGradientContinuity
  gradientContinuity() noexcept {
    return sspGradientContinuity(SspInterpolationKind::Quadrilateral);
  }

  [[nodiscard]] double frequency() const noexcept;
  [[nodiscard]] std::size_t segmentCount() const noexcept;
  [[nodiscard]] std::size_t rangeSegmentCount() const noexcept;
  [[nodiscard]] bool isLossless() const noexcept;
  [[nodiscard]] std::optional<std::complex<double>> uniformComplexSoundSpeed()
      const noexcept;

  [[nodiscard]] std::size_t locateSegment(double depth,
                                          std::size_t previousSegment) const;
  [[nodiscard]] std::size_t locateRangeSegment(
      double range, std::size_t previousRangeSegment) const;
  [[nodiscard]] double minimumRangeForSegment(
      std::size_t rangeSegmentIndex) const;
  [[nodiscard]] double maximumRangeForSegment(
      std::size_t rangeSegmentIndex) const;

  [[nodiscard]] SoundSpeedSample evaluateAtSegment(
      Vec2 position, std::size_t segmentIndex) const;
  [[nodiscard]] SoundSpeedSample evaluateAtSegments(
      Vec2 position, std::size_t segmentIndex,
      std::size_t rangeSegmentIndex) const;
  [[nodiscard]] SoundSpeedSample evaluate(Vec2 position,
                                          std::size_t previousSegment) const;
  [[nodiscard]] SoundSpeedSample evaluate(
      Vec2 position, std::size_t previousSegment,
      std::size_t previousRangeSegment) const;

 private:
  [[nodiscard]] SoundSpeedSample addImaginarySoundSpeed(SoundSpeedSample sample,
                                                        double depth) const;

  double frequency_;
  QuadrilateralSsp realProfile_;
  SharedQuadrilateralSspGrid grid_;
  std::vector<double> depths_;
  std::vector<double> imaginarySoundSpeeds_;
};

}  // namespace rayreuse
