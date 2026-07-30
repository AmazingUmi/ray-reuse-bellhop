#include "rayreuse/model/environment.hpp"

#include <cmath>
#include <string>
#include <utility>

#include "rayreuse/error.hpp"

namespace rayreuse {
namespace {

void requireFinite(double value, const std::string& name) {
  if (!std::isfinite(value)) {
    throw ValidationError(name + " must be finite");
  }
}

void validateRawAttenuation(const RawAttenuation& attenuation,
                            const std::string& name) {
  requireFinite(attenuation.value, name + ".value");
  requireFinite(attenuation.referenceFrequency, name + ".referenceFrequency");
  requireFinite(attenuation.powerLawExponent, name + ".powerLawExponent");
  requireFinite(attenuation.transitionFrequency, name + ".transitionFrequency");
  if (attenuation.value < 0.0) {
    throw ValidationError(name + ".value must be non-negative");
  }
  if (attenuation.referenceFrequency <= 0.0) {
    throw ValidationError(name + ".referenceFrequency must be positive");
  }
  if (attenuation.transitionFrequency <= 0.0) {
    throw ValidationError(name + ".transitionFrequency must be positive");
  }
}

void validateMaterial(const AcousticMaterial& material) {
  requireFinite(material.compressionalSoundSpeed,
                "material.compressionalSoundSpeed");
  requireFinite(material.shearSoundSpeed, "material.shearSoundSpeed");
  requireFinite(material.density, "material.density");
  if (material.compressionalSoundSpeed <= 0.0) {
    throw ValidationError(
        "material.compressionalSoundSpeed must be positive");
  }
  if (material.shearSoundSpeed < 0.0) {
    throw ValidationError("material.shearSoundSpeed must be non-negative");
  }
  if (material.density <= 0.0) {
    throw ValidationError("material.density must be positive");
  }
  validateRawAttenuation(material.compressionalAttenuation,
                         "material.compressionalAttenuation");
  validateRawAttenuation(material.shearAttenuation,
                         "material.shearAttenuation");
}

}  // namespace

SoundSpeedProfile::SoundSpeedProfile(std::vector<SoundSpeedPoint> points)
    : points_(std::move(points)) {
  if (points_.size() < 2U) {
    throw ValidationError("sound-speed profile requires at least two points");
  }

  for (std::size_t index = 0; index < points_.size(); ++index) {
    const SoundSpeedPoint& point = points_[index];
    requireFinite(point.depth, "soundSpeedProfile.depth");
    requireFinite(point.soundSpeed, "soundSpeedProfile.soundSpeed");
    requireFinite(point.density, "soundSpeedProfile.density");
    if (point.soundSpeed <= 0.0) {
      throw ValidationError("soundSpeedProfile.soundSpeed must be positive");
    }
    if (point.density <= 0.0) {
      throw ValidationError("soundSpeedProfile.density must be positive");
    }
    validateRawAttenuation(point.attenuation,
                           "soundSpeedProfile.attenuation");
    if (index > 0U && points_[index - 1U].depth >= point.depth) {
      throw ValidationError(
          "sound-speed profile depths must be strictly increasing");
    }
  }
}

const std::vector<SoundSpeedPoint>& SoundSpeedProfile::points() const noexcept {
  return points_;
}

double SoundSpeedProfile::minimumDepth() const noexcept {
  return points_.front().depth;
}

double SoundSpeedProfile::maximumDepth() const noexcept {
  return points_.back().depth;
}

BoundaryModel BoundaryModel::vacuum(double depth) {
  return BoundaryModel(BoundaryKind::Vacuum, depth, std::nullopt);
}

BoundaryModel BoundaryModel::rigid(double depth) {
  return BoundaryModel(BoundaryKind::Rigid, depth, std::nullopt);
}

BoundaryModel BoundaryModel::acousticHalfSpace(double depth,
                                               AcousticMaterial material) {
  validateMaterial(material);
  return BoundaryModel(BoundaryKind::AcousticHalfSpace, depth,
                       std::move(material));
}

BoundaryModel::BoundaryModel(BoundaryKind kind, double depth,
                             std::optional<AcousticMaterial> material)
    : kind_(kind), depth_(depth), material_(std::move(material)) {
  requireFinite(depth_, "boundary.depth");
  if (kind_ == BoundaryKind::AcousticHalfSpace && !material_.has_value()) {
    throw ValidationError("acoustic half-space requires material properties");
  }
  if (kind_ != BoundaryKind::AcousticHalfSpace && material_.has_value()) {
    throw ValidationError(
        "vacuum and rigid boundaries cannot carry acoustic material");
  }
}

BoundaryKind BoundaryModel::kind() const noexcept { return kind_; }

double BoundaryModel::depth() const noexcept { return depth_; }

const std::optional<AcousticMaterial>& BoundaryModel::material() const noexcept {
  return material_;
}

Environment::Environment(SoundSpeedProfile soundSpeedProfile,
                         BoundaryModel seaSurface, BoundaryModel seabed)
    : soundSpeedProfile_(std::move(soundSpeedProfile)),
      seaSurface_(std::move(seaSurface)),
      seabed_(std::move(seabed)) {
  if (seaSurface_.kind() != BoundaryKind::Vacuum) {
    throw ValidationError("the first RayReuse sea surface must be vacuum");
  }
  if (seabed_.kind() != BoundaryKind::Rigid &&
      seabed_.kind() != BoundaryKind::AcousticHalfSpace) {
    throw ValidationError(
        "the first RayReuse seabed must be rigid or an acoustic half-space");
  }
  if (seaSurface_.depth() >= seabed_.depth()) {
    throw ValidationError("sea-surface depth must be less than seabed depth");
  }
  if (soundSpeedProfile_.minimumDepth() > seaSurface_.depth() ||
      soundSpeedProfile_.maximumDepth() < seabed_.depth()) {
    throw ValidationError(
        "sound-speed profile must cover the complete water column");
  }
}

const SoundSpeedProfile& Environment::soundSpeedProfile() const noexcept {
  return soundSpeedProfile_;
}

const BoundaryModel& Environment::seaSurface() const noexcept {
  return seaSurface_;
}

const BoundaryModel& Environment::seabed() const noexcept { return seabed_; }

double Environment::waterDepth() const noexcept {
  return seabed_.depth() - seaSurface_.depth();
}

}  // namespace rayreuse
