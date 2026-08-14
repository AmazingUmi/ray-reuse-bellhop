#pragma once

#include <complex>
#include <cstddef>
#include <optional>
#include <variant>

#include "bellhop/model/c_linear_frequency_ssp.hpp"
#include "bellhop/model/c_linear_ssp.hpp"
#include "bellhop/model/cubic_spline_frequency_ssp.hpp"
#include "bellhop/model/cubic_spline_ssp.hpp"
#include "bellhop/model/environment.hpp"
#include "bellhop/model/n2_linear_frequency_ssp.hpp"
#include "bellhop/model/n2_linear_ssp.hpp"
#include "bellhop/model/pchip_frequency_ssp.hpp"
#include "bellhop/model/pchip_ssp.hpp"
#include "bellhop/model/quadrilateral_frequency_ssp.hpp"
#include "bellhop/model/quadrilateral_ssp.hpp"
#include "bellhop/numerics/vec2.hpp"

namespace bellhop {

// Runtime-selected, value-owned SSP evaluator. The variant is visited without
// allocation; concrete evaluators retain the exact Bellhop interpolation and
// segment-hint behavior in their own implementations.
class GeometrySspEvaluator {
 public:
  explicit GeometrySspEvaluator(const SoundSpeedProfile& profile);

  [[nodiscard]] SspInterpolationKind interpolationKind() const noexcept;
  [[nodiscard]] SspGradientContinuity gradientContinuity() const noexcept;
  [[nodiscard]] std::size_t segmentCount() const noexcept;
  [[nodiscard]] std::size_t rangeSegmentCount() const noexcept;
  [[nodiscard]] std::size_t locateSegment(
      double depth, std::size_t previousSegment) const;
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
  [[nodiscard]] SoundSpeedSample evaluate(
      Vec2 position, std::size_t previousSegment) const;
  [[nodiscard]] SoundSpeedSample evaluate(
      Vec2 position, std::size_t previousSegment,
      std::size_t previousRangeSegment) const;

 private:
  using Backend =
      std::variant<CLinearSsp, N2LinearSsp, PchipSsp, CubicSplineSsp,
                   QuadrilateralSsp>;

  SspInterpolationKind interpolationKind_;
  Backend backend_;
};

// Frequency-dependent counterpart used only while reconstructing one
// frequency's complex travel time. Geometry callers never see this type.
class FrequencySspEvaluator {
 public:
  FrequencySspEvaluator(const SoundSpeedProfile& profile, double frequency);
  FrequencySspEvaluator(const SoundSpeedProfile& profile, double frequency,
                        const VolumeAttenuation& volumeAttenuation);

  [[nodiscard]] SspInterpolationKind interpolationKind() const noexcept;
  [[nodiscard]] SspGradientContinuity gradientContinuity() const noexcept;
  [[nodiscard]] double frequency() const noexcept;
  [[nodiscard]] std::size_t segmentCount() const noexcept;
  [[nodiscard]] std::size_t rangeSegmentCount() const noexcept;
  [[nodiscard]] std::size_t locateRangeSegment(
      double range, std::size_t previousRangeSegment) const;
  [[nodiscard]] double minimumRangeForSegment(
      std::size_t rangeSegmentIndex) const;
  [[nodiscard]] double maximumRangeForSegment(
      std::size_t rangeSegmentIndex) const;
  [[nodiscard]] bool isLossless() const noexcept;
  [[nodiscard]] std::optional<std::complex<double>>
  uniformComplexSoundSpeed() const noexcept;
  [[nodiscard]] SoundSpeedSample evaluateAtSegment(
      Vec2 position, std::size_t segmentIndex) const;
  [[nodiscard]] SoundSpeedSample evaluateAtSegments(
      Vec2 position, std::size_t segmentIndex,
      std::size_t rangeSegmentIndex) const;
  [[nodiscard]] SoundSpeedSample evaluate(
      Vec2 position, std::size_t previousSegment) const;
  [[nodiscard]] SoundSpeedSample evaluate(
      Vec2 position, std::size_t previousSegment,
      std::size_t previousRangeSegment) const;

 private:
  using Backend =
      std::variant<CLinearFrequencySsp, N2LinearFrequencySsp,
                   PchipFrequencySsp, CubicSplineFrequencySsp,
                   QuadrilateralFrequencySsp>;

  SspInterpolationKind interpolationKind_;
  Backend backend_;
};

}  // namespace bellhop
