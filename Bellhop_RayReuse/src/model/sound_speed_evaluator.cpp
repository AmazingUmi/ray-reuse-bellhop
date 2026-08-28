#include "rayreuse/model/sound_speed_evaluator.hpp"

#include <cmath>
#include <limits>
#include <utility>

#include "rayreuse/error.hpp"

namespace rayreuse {
namespace {

[[nodiscard]] std::variant<CLinearSsp, PchipSsp, N2LinearSsp, CubicSplineSsp,
                           QuadrilateralSsp>
makeGeometryBackend(const SoundSpeedProfile& profile) {
  switch (profile.interpolationKind()) {
    case SspInterpolationKind::CLinear:
      return CLinearSsp(profile);
    case SspInterpolationKind::Pchip:
      return PchipSsp(profile);
    case SspInterpolationKind::N2Linear:
      return N2LinearSsp(profile);
    case SspInterpolationKind::CubicSpline:
      return CubicSplineSsp(profile);
    case SspInterpolationKind::Quadrilateral:
      return QuadrilateralSsp(profile);
  }
  throw ValidationError("SSP interpolation kind is invalid");
}

// Keep the factories' return types explicit so unsupported kinds cannot fall
// back to another interpolation backend.
[[nodiscard]] std::variant<CLinearFrequencySsp, PchipFrequencySsp,
                           N2LinearFrequencySsp, CubicSplineFrequencySsp,
                           QuadrilateralFrequencySsp>
makeFrequencyBackend(const SoundSpeedProfile& profile, double frequency) {
  switch (profile.interpolationKind()) {
    case SspInterpolationKind::CLinear:
      return CLinearFrequencySsp(profile, frequency);
    case SspInterpolationKind::Pchip:
      return PchipFrequencySsp(profile, frequency);
    case SspInterpolationKind::N2Linear:
      return N2LinearFrequencySsp(profile, frequency);
    case SspInterpolationKind::CubicSpline:
      return CubicSplineFrequencySsp(profile, frequency);
    case SspInterpolationKind::Quadrilateral:
      return QuadrilateralFrequencySsp(profile, frequency);
  }
  throw ValidationError("SSP interpolation kind is invalid");
}

}  // namespace

GeometrySspEvaluator::GeometrySspEvaluator(const SoundSpeedProfile& profile)
    : interpolationKind_(profile.interpolationKind()),
      backend_(makeGeometryBackend(profile)) {}

SspInterpolationKind GeometrySspEvaluator::interpolationKind() const noexcept {
  return interpolationKind_;
}

SspGradientContinuity GeometrySspEvaluator::gradientContinuity() const noexcept {
  return std::visit(
      [](const auto& backend) { return backend.gradientContinuity(); },
      backend_);
}

std::size_t GeometrySspEvaluator::segmentCount() const noexcept {
  return std::visit(
      [](const auto& backend) { return backend.segmentCount(); }, backend_);
}

std::size_t GeometrySspEvaluator::rangeSegmentCount() const noexcept {
  if (const auto* quadrilateral = std::get_if<QuadrilateralSsp>(&backend_)) {
    return quadrilateral->rangeSegmentCount();
  }
  return 1U;
}

std::size_t GeometrySspEvaluator::locateSegment(
    double depth, std::size_t previousSegment) const {
  return std::visit(
      [depth, previousSegment](const auto& backend) {
        return backend.locateSegment(depth, previousSegment);
      },
      backend_);
}

std::size_t GeometrySspEvaluator::locateRangeSegment(
    double range, std::size_t previousRangeSegment) const {
  if (const auto* quadrilateral = std::get_if<QuadrilateralSsp>(&backend_)) {
    return quadrilateral->locateRangeSegment(range, previousRangeSegment);
  }
  if (!std::isfinite(range)) {
    throw ValidationError("SSP query range must be finite");
  }
  if (previousRangeSegment != 0U) {
    throw ValidationError("SSP previous range segment index is out of range");
  }
  return 0U;
}

double GeometrySspEvaluator::minimumRangeForSegment(
    std::size_t rangeSegmentIndex) const {
  if (const auto* quadrilateral = std::get_if<QuadrilateralSsp>(&backend_)) {
    return quadrilateral->minimumRangeForSegment(rangeSegmentIndex);
  }
  if (rangeSegmentIndex != 0U) {
    throw ValidationError("SSP range segment index is out of range");
  }
  return -std::numeric_limits<double>::infinity();
}

double GeometrySspEvaluator::maximumRangeForSegment(
    std::size_t rangeSegmentIndex) const {
  if (const auto* quadrilateral = std::get_if<QuadrilateralSsp>(&backend_)) {
    return quadrilateral->maximumRangeForSegment(rangeSegmentIndex);
  }
  if (rangeSegmentIndex != 0U) {
    throw ValidationError("SSP range segment index is out of range");
  }
  return std::numeric_limits<double>::infinity();
}

SoundSpeedSample GeometrySspEvaluator::evaluateAtSegment(
    Vec2 position, std::size_t segmentIndex) const {
  return std::visit(
      [position, segmentIndex](const auto& backend) {
        return backend.evaluateAtSegment(position, segmentIndex);
      },
      backend_);
}

SoundSpeedSample GeometrySspEvaluator::evaluateAtSegments(
    Vec2 position, std::size_t segmentIndex,
    std::size_t rangeSegmentIndex) const {
  if (const auto* quadrilateral = std::get_if<QuadrilateralSsp>(&backend_)) {
    return quadrilateral->evaluateAtSegments(
        position, segmentIndex, rangeSegmentIndex);
  }
  if (rangeSegmentIndex != 0U) {
    throw ValidationError("SSP range segment index is out of range");
  }
  return evaluateAtSegment(position, segmentIndex);
}

SoundSpeedSample GeometrySspEvaluator::evaluate(
    Vec2 position, std::size_t previousSegment) const {
  return std::visit(
      [position, previousSegment](const auto& backend) {
        return backend.evaluate(position, previousSegment);
      },
      backend_);
}

SoundSpeedSample GeometrySspEvaluator::evaluate(
    Vec2 position, std::size_t previousSegment,
    std::size_t previousRangeSegment) const {
  if (const auto* quadrilateral = std::get_if<QuadrilateralSsp>(&backend_)) {
    return quadrilateral->evaluate(
        position, previousSegment, previousRangeSegment);
  }
  if (previousRangeSegment != 0U) {
    throw ValidationError("SSP previous range segment index is out of range");
  }
  return evaluate(position, previousSegment);
}

FrequencySspEvaluator::FrequencySspEvaluator(
    const SoundSpeedProfile& profile, double frequency)
    : interpolationKind_(profile.interpolationKind()),
      backend_(makeFrequencyBackend(profile, frequency)) {}

SspInterpolationKind FrequencySspEvaluator::interpolationKind() const noexcept {
  return interpolationKind_;
}

SspGradientContinuity FrequencySspEvaluator::gradientContinuity() const noexcept {
  return std::visit(
      [](const auto& backend) { return backend.gradientContinuity(); },
      backend_);
}

double FrequencySspEvaluator::frequency() const noexcept {
  return std::visit(
      [](const auto& backend) { return backend.frequency(); }, backend_);
}

std::size_t FrequencySspEvaluator::segmentCount() const noexcept {
  return std::visit(
      [](const auto& backend) { return backend.segmentCount(); }, backend_);
}

std::size_t FrequencySspEvaluator::rangeSegmentCount() const noexcept {
  if (const auto* quadrilateral =
          std::get_if<QuadrilateralFrequencySsp>(&backend_)) {
    return quadrilateral->rangeSegmentCount();
  }
  return 1U;
}

std::size_t FrequencySspEvaluator::locateRangeSegment(
    double range, std::size_t previousRangeSegment) const {
  if (const auto* quadrilateral =
          std::get_if<QuadrilateralFrequencySsp>(&backend_)) {
    return quadrilateral->locateRangeSegment(range, previousRangeSegment);
  }
  if (!std::isfinite(range)) {
    throw ValidationError("SSP query range must be finite");
  }
  if (previousRangeSegment != 0U) {
    throw ValidationError("SSP previous range segment index is out of range");
  }
  return 0U;
}

double FrequencySspEvaluator::minimumRangeForSegment(
    std::size_t rangeSegmentIndex) const {
  if (const auto* quadrilateral =
          std::get_if<QuadrilateralFrequencySsp>(&backend_)) {
    return quadrilateral->minimumRangeForSegment(rangeSegmentIndex);
  }
  if (rangeSegmentIndex != 0U) {
    throw ValidationError("SSP range segment index is out of range");
  }
  return -std::numeric_limits<double>::infinity();
}

double FrequencySspEvaluator::maximumRangeForSegment(
    std::size_t rangeSegmentIndex) const {
  if (const auto* quadrilateral =
          std::get_if<QuadrilateralFrequencySsp>(&backend_)) {
    return quadrilateral->maximumRangeForSegment(rangeSegmentIndex);
  }
  if (rangeSegmentIndex != 0U) {
    throw ValidationError("SSP range segment index is out of range");
  }
  return std::numeric_limits<double>::infinity();
}

bool FrequencySspEvaluator::isLossless() const noexcept {
  return std::visit(
      [](const auto& backend) { return backend.isLossless(); }, backend_);
}

std::optional<std::complex<double>>
FrequencySspEvaluator::uniformComplexSoundSpeed() const noexcept {
  return std::visit(
      [](const auto& backend) { return backend.uniformComplexSoundSpeed(); },
      backend_);
}

SoundSpeedSample FrequencySspEvaluator::evaluateAtSegment(
    Vec2 position, std::size_t segmentIndex) const {
  return std::visit(
      [position, segmentIndex](const auto& backend) {
        return backend.evaluateAtSegment(position, segmentIndex);
      },
      backend_);
}

SoundSpeedSample FrequencySspEvaluator::evaluateAtSegments(
    Vec2 position, std::size_t segmentIndex,
    std::size_t rangeSegmentIndex) const {
  if (const auto* quadrilateral =
          std::get_if<QuadrilateralFrequencySsp>(&backend_)) {
    return quadrilateral->evaluateAtSegments(
        position, segmentIndex, rangeSegmentIndex);
  }
  if (rangeSegmentIndex != 0U) {
    throw ValidationError("SSP range segment index is out of range");
  }
  return evaluateAtSegment(position, segmentIndex);
}

SoundSpeedSample FrequencySspEvaluator::evaluate(
    Vec2 position, std::size_t previousSegment) const {
  return std::visit(
      [position, previousSegment](const auto& backend) {
        return backend.evaluate(position, previousSegment);
      },
      backend_);
}

SoundSpeedSample FrequencySspEvaluator::evaluate(
    Vec2 position, std::size_t previousSegment,
    std::size_t previousRangeSegment) const {
  if (const auto* quadrilateral =
          std::get_if<QuadrilateralFrequencySsp>(&backend_)) {
    return quadrilateral->evaluate(
        position, previousSegment, previousRangeSegment);
  }
  if (previousRangeSegment != 0U) {
    throw ValidationError("SSP previous range segment index is out of range");
  }
  return evaluate(position, previousSegment);
}

}  // namespace rayreuse
