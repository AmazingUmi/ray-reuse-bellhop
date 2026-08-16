#include "rayreuse/cache/ray_path_cache.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>

#include "rayreuse/error.hpp"

namespace rayreuse {
namespace {

constexpr double kGeometryTolerance = 1.0e-10;
constexpr double kSlownessNormTolerance = 2.0e-4;
constexpr double kPositionTolerance = 1.0e-8;
constexpr double kSlownessTolerance = 1.0e-12;
constexpr double kTravelTimeTolerance = 1.0e-12;

class FingerprintBuilder {
 public:
  void append(std::uint64_t value) noexcept {
    // Hash a canonical least-significant-byte-first representation so the
    // fingerprint does not depend on host endianness or object layout.
    for (unsigned int byteIndex = 0U; byteIndex < 8U; ++byteIndex) {
      hash_ ^= value & 0xffU;
      hash_ *= kFnvPrime;
      value >>= 8U;
    }
  }

  void append(double value) noexcept {
    append(std::bit_cast<std::uint64_t>(value));
  }

  [[nodiscard]] std::uint64_t value() const noexcept { return hash_; }

 private:
  static constexpr std::uint64_t kFnvOffsetBasis = 14695981039346656037ULL;
  static constexpr std::uint64_t kFnvPrime = 1099511628211ULL;

  std::uint64_t hash_{kFnvOffsetBasis};
};

void appendVec2(FingerprintBuilder& fingerprint, Vec2 value) noexcept {
  fingerprint.append(value.range);
  fingerprint.append(value.depth);
}

void appendRayState(FingerprintBuilder& fingerprint,
                    const RayState& point) noexcept {
  appendVec2(fingerprint, point.position);
  appendVec2(fingerprint, point.slowness);
  fingerprint.append(point.dynamicP[0]);
  fingerprint.append(point.dynamicP[1]);
  fingerprint.append(point.dynamicQ[0]);
  fingerprint.append(point.dynamicQ[1]);
  fingerprint.append(point.soundSpeed);
  fingerprint.append(point.realTravelTime);
}

void appendStep(FingerprintBuilder& fingerprint,
                const StepQuadrature& step) noexcept {
  fingerprint.append(step.stepLength);
  fingerprint.append(step.startWeight);
  fingerprint.append(step.midpointWeight);
  appendVec2(fingerprint, step.midpoint);
}

void appendAttenuation(FingerprintBuilder& fingerprint,
                       const RawAttenuation& attenuation) noexcept {
  fingerprint.append(attenuation.value);
  fingerprint.append(static_cast<std::uint64_t>(attenuation.unit));
  fingerprint.append(attenuation.referenceFrequency);
  fingerprint.append(attenuation.powerLawExponent);
  fingerprint.append(attenuation.transitionFrequency);
  fingerprint.append(static_cast<std::uint64_t>(attenuation.volumeModel));
}

void appendMaterial(FingerprintBuilder& fingerprint,
                    const AcousticMaterial& material) noexcept {
  fingerprint.append(material.compressionalSoundSpeed);
  fingerprint.append(material.shearSoundSpeed);
  fingerprint.append(material.density);
  appendAttenuation(fingerprint, material.compressionalAttenuation);
  appendAttenuation(fingerprint, material.shearAttenuation);
}

void appendEvent(FingerprintBuilder& fingerprint,
                 const ReflectionEvent& event) noexcept {
  fingerprint.append(static_cast<std::uint64_t>(event.rayPointIndex));
  fingerprint.append(static_cast<std::uint64_t>(event.reflectedRayPointIndex));
  fingerprint.append(static_cast<std::uint64_t>(event.boundary));
  fingerprint.append(static_cast<std::uint64_t>(event.boundarySegmentIndex));
  fingerprint.append(event.boundaryCurvature);
  appendVec2(fingerprint, event.position);
  appendVec2(fingerprint, event.boundaryTangent);
  appendVec2(fingerprint, event.outwardNormal);
  appendVec2(fingerprint, event.incidentSlowness);
  appendVec2(fingerprint, event.reflectedSlowness);
  fingerprint.append(event.tangentSlowness);
  fingerprint.append(event.normalSlowness);
  fingerprint.append(
      static_cast<std::uint64_t>(event.longMaterialOverride.has_value()));
  if (event.longMaterialOverride.has_value()) {
    appendMaterial(fingerprint, event.longMaterialOverride->material);
    fingerprint.append(event.longMaterialOverride->attenuationEvaluationDepth);
  }
}

void appendPath(FingerprintBuilder& fingerprint, const RayPath& path) noexcept {
  constexpr std::uint64_t kPathMarker = 0x5241595041544801ULL;
  constexpr std::uint64_t kPointsMarker = 0x504f494e54530101ULL;
  constexpr std::uint64_t kStepsMarker = 0x5354455053010101ULL;
  constexpr std::uint64_t kEventsMarker = 0x4556454e54530101ULL;
  constexpr std::uint64_t kTerminationMarker = 0x5445524d494e0101ULL;

  fingerprint.append(kPathMarker);
  fingerprint.append(path.launchAngle);

  fingerprint.append(kPointsMarker);
  fingerprint.append(static_cast<std::uint64_t>(path.points.size()));
  for (const RayState& point : path.points) {
    appendRayState(fingerprint, point);
  }

  fingerprint.append(kStepsMarker);
  fingerprint.append(static_cast<std::uint64_t>(path.steps.size()));
  for (const StepQuadrature& step : path.steps) {
    appendStep(fingerprint, step);
  }

  fingerprint.append(kEventsMarker);
  fingerprint.append(static_cast<std::uint64_t>(path.events.size()));
  for (const ReflectionEvent& event : path.events) {
    appendEvent(fingerprint, event);
  }

  fingerprint.append(kTerminationMarker);
  fingerprint.append(static_cast<std::uint64_t>(path.terminationReason));
}

bool finiteArray(const std::array<double, 2>& values) {
  return std::isfinite(values[0]) && std::isfinite(values[1]);
}

void validateRayState(const RayState& point, double previousTravelTime) {
  if (!isFinite(point.position) || !isFinite(point.slowness) ||
      !finiteArray(point.dynamicP) || !finiteArray(point.dynamicQ) ||
      !std::isfinite(point.soundSpeed) ||
      !std::isfinite(point.realTravelTime)) {
    throw ValidationError("ray path contains a non-finite state");
  }
  if (point.soundSpeed <= 0.0) {
    throw ValidationError("ray-state sound speed must be positive");
  }
  if (point.realTravelTime < 0.0 || point.realTravelTime < previousTravelTime) {
    throw ValidationError("ray-state travel time must be non-decreasing");
  }
}

void validateStep(const StepQuadrature& step) {
  if (!std::isfinite(step.stepLength) || !std::isfinite(step.startWeight) ||
      !std::isfinite(step.midpointWeight) || !isFinite(step.midpoint)) {
    throw ValidationError("ray path contains non-finite quadrature data");
  }
  if (step.stepLength <= 0.0 || step.startWeight < 0.0 ||
      step.midpointWeight < 0.0) {
    throw ValidationError(
        "ray quadrature lengths and weights must be non-negative");
  }
  const double weightError =
      std::abs(step.startWeight + step.midpointWeight - step.stepLength);
  const double weightTolerance = 1.0e-12 * std::max(1.0, step.stepLength);
  if (weightError > weightTolerance) {
    throw ValidationError(
        "ray quadrature weights must sum to the actual step length");
  }
}

[[nodiscard]] bool validAttenuationUnit(AttenuationUnit unit) noexcept {
  switch (unit) {
    case AttenuationUnit::NepersPerMeter:
    case AttenuationUnit::DecibelsPerMeter:
    case AttenuationUnit::DecibelsPerMeterPowerLaw:
    case AttenuationUnit::DecibelsPerMeterKilohertz:
    case AttenuationUnit::DecibelsPerWavelength:
    case AttenuationUnit::QualityFactor:
    case AttenuationUnit::LossParameter:
      return true;
  }
  return false;
}

[[nodiscard]] bool validVolumeModel(VolumeAttenuationModel model) noexcept {
  switch (model) {
    case VolumeAttenuationModel::None:
    case VolumeAttenuationModel::Thorp:
    case VolumeAttenuationModel::FrancoisGarrison:
    case VolumeAttenuationModel::Biological:
      return true;
  }
  return false;
}

void validateFrozenMaterial(const FrozenBoundaryMaterial& frozen) {
  const AcousticMaterial& material = frozen.material;
  if (frozen.attenuationEvaluationDepth !=
          kLegacyLongBoundaryAttenuationDepth ||
      !std::isfinite(material.compressionalSoundSpeed) ||
      !std::isfinite(material.shearSoundSpeed) ||
      !std::isfinite(material.density) ||
      material.compressionalSoundSpeed <= 0.0 ||
      material.shearSoundSpeed < 0.0 || material.density <= 0.0) {
    throw ValidationError("frozen long-format boundary material is invalid");
  }
  for (const RawAttenuation* attenuation :
       {&material.compressionalAttenuation, &material.shearAttenuation}) {
    if (!validAttenuationUnit(attenuation->unit) ||
        !validVolumeModel(attenuation->volumeModel) ||
        !std::isfinite(attenuation->value) ||
        !std::isfinite(attenuation->referenceFrequency) ||
        !std::isfinite(attenuation->powerLawExponent) ||
        !std::isfinite(attenuation->transitionFrequency) ||
        attenuation->value < 0.0 || attenuation->referenceFrequency <= 0.0 ||
        attenuation->transitionFrequency <= 0.0) {
      throw ValidationError(
          "frozen long-format boundary attenuation is invalid");
    }
  }
  if (material.shearSoundSpeed == 0.0 &&
      material.shearAttenuation.value != 0.0) {
    throw ValidationError(
        "zero frozen long-format shear speed requires zero shear attenuation");
  }
}

void validateEvent(const ReflectionEvent& event,
                   const std::vector<RayState>& points) {
  if (event.rayPointIndex >= points.size() - 1U) {
    throw ValidationError(
        "reflection event must reference a pre-reflection point followed by "
        "a post-reflection point");
  }
  if (event.reflectedRayPointIndex != event.rayPointIndex + 1U ||
      event.reflectedRayPointIndex >= points.size()) {
    throw ValidationError(
        "reflection event must explicitly reference its adjacent "
        "post-reflection point");
  }
  if (!isFinite(event.position) || !isFinite(event.boundaryTangent) ||
      !isFinite(event.outwardNormal) || !isFinite(event.incidentSlowness) ||
      !isFinite(event.reflectedSlowness) ||
      !std::isfinite(event.boundaryCurvature) ||
      !std::isfinite(event.tangentSlowness) ||
      !std::isfinite(event.normalSlowness)) {
    throw ValidationError("reflection event contains a non-finite value");
  }
  if (event.longMaterialOverride.has_value()) {
    validateFrozenMaterial(*event.longMaterialOverride);
  }
  const double tangentNorm = norm(event.boundaryTangent);
  const double normalNorm = norm(event.outwardNormal);
  if (tangentNorm <= std::numeric_limits<double>::min() ||
      normalNorm <= std::numeric_limits<double>::min()) {
    throw ValidationError(
        "reflection-event tangent and normal must be non-zero");
  }
  const double frameScale = std::max({1.0, tangentNorm, normalNorm});
  if (std::abs(tangentNorm - normalNorm) > kGeometryTolerance * frameScale ||
      std::abs(
          fortranDotProduct2D(event.boundaryTangent, event.outwardNormal)) >
          kGeometryTolerance * tangentNorm * normalNorm) {
    throw ValidationError(
        "reflection-event tangent and normal must be orthogonal with equal "
        "norm");
  }
  const Vec2 expectedNormal = event.boundary == ReflectionBoundary::SeaSurface
                                  ? Vec2{.range = event.boundaryTangent.depth,
                                         .depth = -event.boundaryTangent.range}
                                  : Vec2{.range = -event.boundaryTangent.depth,
                                         .depth = event.boundaryTangent.range};
  if (event.boundaryTangent.range <= 0.0 ||
      norm(event.outwardNormal - expectedNormal) >
          kGeometryTolerance * frameScale) {
    throw ValidationError(
        "reflection-event boundary frame has the wrong orientation");
  }
  const RayState& incidentPoint = points[event.rayPointIndex];
  const RayState& reflectedPoint = points[event.reflectedRayPointIndex];
  if (norm(event.position - incidentPoint.position) > kPositionTolerance ||
      norm(event.position - reflectedPoint.position) > kPositionTolerance) {
    throw ValidationError(
        "reflection-event position must match its pre/post point pair");
  }
  const double timeTolerance =
      kTravelTimeTolerance * std::max({1.0, incidentPoint.realTravelTime,
                                       reflectedPoint.realTravelTime});
  if (std::abs(incidentPoint.realTravelTime - reflectedPoint.realTravelTime) >
      timeTolerance) {
    throw ValidationError(
        "reflection pre/post points must have the same travel time");
  }
  if (norm(event.incidentSlowness - incidentPoint.slowness) >
          kSlownessTolerance ||
      norm(event.reflectedSlowness - reflectedPoint.slowness) >
          kSlownessTolerance) {
    throw ValidationError(
        "reflection-event slowness must match its pre/post point pair");
  }
  const double incidentTangent =
      fortranDotProduct2D(event.incidentSlowness, event.boundaryTangent);
  const double incidentNormal =
      fortranDotProduct2D(event.incidentSlowness, event.outwardNormal);
  if (std::abs(event.tangentSlowness - incidentTangent) > kSlownessTolerance ||
      std::abs(event.normalSlowness - incidentNormal) > kSlownessTolerance) {
    throw ValidationError(
        "reflection-event scalar slowness components are inconsistent");
  }
  const double twiceIncidentNormal = 2.0 * incidentNormal;
  const Vec2 expectedReflectedSlowness{
      .range = std::fma(-twiceIncidentNormal, event.outwardNormal.range,
                        event.incidentSlowness.range),
      .depth = std::fma(-twiceIncidentNormal, event.outwardNormal.depth,
                        event.incidentSlowness.depth)};
  if (norm(event.reflectedSlowness - expectedReflectedSlowness) >
      kSlownessTolerance) {
    throw ValidationError(
        "reflection-event slowness does not satisfy the legacy mirror "
        "formula");
  }
  const bool unitFrame = std::abs(tangentNorm - 1.0) <= kGeometryTolerance &&
                         std::abs(normalNorm - 1.0) <= kGeometryTolerance;
  if (unitFrame) {
    if (std::abs(incidentPoint.soundSpeed * norm(event.incidentSlowness) -
                 1.0) > kSlownessNormTolerance ||
        std::abs(reflectedPoint.soundSpeed * norm(event.reflectedSlowness) -
                 1.0) > kSlownessNormTolerance) {
      throw ValidationError(
          "unit-frame reflection-event slowness norm is inconsistent with "
          "sound speed");
    }
  }
}

void validatePath(const RayPath& path) {
  if (!std::isfinite(path.launchAngle)) {
    throw ValidationError("ray launch angle must be finite");
  }
  if (path.points.empty()) {
    throw ValidationError("ray path must contain at least one point");
  }
  if (path.steps.size() >
      std::numeric_limits<std::size_t>::max() - path.events.size()) {
    throw ValidationError("ray path transition count overflows size_t");
  }
  const std::size_t transitionCount = path.steps.size() + path.events.size();
  if (path.points.size() - 1U != transitionCount) {
    throw ValidationError(
        "ray path requires one step or reflection event per point pair");
  }

  double previousTravelTime = 0.0;
  for (const RayState& point : path.points) {
    validateRayState(point, previousTravelTime);
    previousTravelTime = point.realTravelTime;
  }
  for (const StepQuadrature& step : path.steps) {
    validateStep(step);
  }
  std::size_t previousEventIndex = 0U;
  bool havePreviousEvent = false;
  for (const ReflectionEvent& event : path.events) {
    if (havePreviousEvent && event.rayPointIndex <= previousEventIndex) {
      throw ValidationError(
          "reflection events must have unique, strictly increasing indices");
    }
    validateEvent(event, path.points);
    previousEventIndex = event.rayPointIndex;
    havePreviousEvent = true;
  }
}

std::size_t pathAllocationBytes(const RayPath& path) noexcept {
  return path.points.capacity() * sizeof(RayState) +
         path.steps.capacity() * sizeof(StepQuadrature) +
         path.events.capacity() * sizeof(ReflectionEvent);
}

}  // namespace

void RayPathCache::reserve(std::size_t rayCount) {
  if (frozen_) {
    throw std::logic_error("cannot reserve a frozen RayPathCache");
  }
  paths_.reserve(rayCount);
}

void RayPathCache::append(RayPath path) {
  if (frozen_) {
    throw std::logic_error("cannot append to a frozen RayPathCache");
  }
  paths_.push_back(std::move(path));
}

void RayPathCache::freeze() {
  if (frozen_) {
    return;
  }
  if (paths_.empty()) {
    throw ValidationError("cannot freeze an empty RayPathCache");
  }
  for (const RayPath& path : paths_) {
    validatePath(path);
  }
  frozen_ = true;
}

bool RayPathCache::frozen() const noexcept { return frozen_; }

bool RayPathCache::empty() const noexcept { return paths_.empty(); }

std::size_t RayPathCache::size() const noexcept { return paths_.size(); }

const RayPath& RayPathCache::at(std::size_t index) const {
  if (!frozen_) {
    throw std::logic_error("cannot read an unfrozen RayPathCache");
  }
  return paths_.at(index);
}

std::span<const RayPath> RayPathCache::paths() const {
  if (!frozen_) {
    throw std::logic_error("cannot read an unfrozen RayPathCache");
  }
  return paths_;
}

std::size_t RayPathCache::memoryFootprintBytes() const noexcept {
  std::size_t bytes =
      sizeof(RayPathCache) + paths_.capacity() * sizeof(RayPath);
  for (const RayPath& path : paths_) {
    bytes += pathAllocationBytes(path);
  }
  return bytes;
}

std::uint64_t RayPathCache::contentFingerprint() const {
  if (!frozen_) {
    throw std::logic_error("cannot fingerprint an unfrozen RayPathCache");
  }

  constexpr std::uint64_t kCacheMarker = 0x5241594341434801ULL;
  FingerprintBuilder fingerprint;
  fingerprint.append(kCacheMarker);
  fingerprint.append(static_cast<std::uint64_t>(paths_.size()));
  for (const RayPath& path : paths_) {
    appendPath(fingerprint, path);
  }
  return fingerprint.value();
}

}  // namespace rayreuse
