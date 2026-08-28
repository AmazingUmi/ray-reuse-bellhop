#pragma once

#include <complex>
#include <cstddef>
#include <optional>
#include <variant>

#include "rayreuse/model/c_linear_frequency_ssp.hpp"
#include "rayreuse/model/c_linear_ssp.hpp"
#include "rayreuse/model/environment.hpp"
#include "rayreuse/model/n2_linear_frequency_ssp.hpp"
#include "rayreuse/model/n2_linear_ssp.hpp"
#include "rayreuse/model/pchip_frequency_ssp.hpp"
#include "rayreuse/model/pchip_ssp.hpp"
#include "rayreuse/model/sound_speed_types.hpp"
#include "rayreuse/numerics/vec2.hpp"

namespace rayreuse {

// Runtime-selected, value-owned SSP evaluator. The variant is visited without
// allocation; concrete evaluators retain the exact Bellhop interpolation and
// segment-hint behavior in their own implementations.
class GeometrySspEvaluator {
 public:
  explicit GeometrySspEvaluator(const SoundSpeedProfile& profile);

  [[nodiscard]] SspInterpolationKind interpolationKind() const noexcept;
  [[nodiscard]] SspGradientContinuity gradientContinuity() const noexcept;
  [[nodiscard]] std::size_t segmentCount() const noexcept;
  [[nodiscard]] std::size_t locateSegment(
      double depth, std::size_t previousSegment) const;
  [[nodiscard]] SoundSpeedSample evaluateAtSegment(
      Vec2 position, std::size_t segmentIndex) const;
  [[nodiscard]] SoundSpeedSample evaluate(
      Vec2 position, std::size_t previousSegment) const;

 private:
  using Backend = std::variant<CLinearSsp, PchipSsp, N2LinearSsp>;

  SspInterpolationKind interpolationKind_;
  Backend backend_;
};

// Frequency-dependent counterpart used while projecting complex travel time
// and attenuation. Geometry callers never see this type.
class FrequencySspEvaluator {
 public:
  FrequencySspEvaluator(const SoundSpeedProfile& profile, double frequency);

  [[nodiscard]] SspInterpolationKind interpolationKind() const noexcept;
  [[nodiscard]] SspGradientContinuity gradientContinuity() const noexcept;
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
  using Backend =
      std::variant<CLinearFrequencySsp, PchipFrequencySsp,
                   N2LinearFrequencySsp>;

  SspInterpolationKind interpolationKind_;
  Backend backend_;
};

}  // namespace rayreuse
