#include "rayreuse/model/environment.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <utility>

#include "rayreuse/error.hpp"
#include "rayreuse/model/quadrilateral_ssp.hpp"

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
    throw ValidationError("material.compressionalSoundSpeed must be positive");
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
  if (material.shearSoundSpeed == 0.0 &&
      material.shearAttenuation.value != 0.0) {
    throw ValidationError(
        "zero material shear speed requires zero shear attenuation");
  }
}

[[nodiscard]] GrainSizeMaterial grainSizeToGeoacoustic(double meanGrainSize) {
  requireFinite(meanGrainSize, "grainSize.meanGrainSize");
  const auto legacy = [](float value) { return static_cast<double>(value); };
  const double squared = meanGrainSize * meanGrainSize;
  double soundSpeedRatio{};
  double densityRatio{};
  if (meanGrainSize >= -1.0 && meanGrainSize < 1.0) {
    soundSpeedRatio = std::fma(-legacy(0.056452F), meanGrainSize,
                               legacy(0.002709F) * squared) +
                      legacy(1.2778F);
    densityRatio = std::fma(-legacy(0.17057F), meanGrainSize,
                            legacy(0.007797F) * squared) +
                   legacy(2.3139F);
  } else if (meanGrainSize >= 1.0 && meanGrainSize < legacy(5.3F)) {
    const double cubed = squared * meanGrainSize;
    soundSpeedRatio = std::fma(-legacy(0.1382798F), meanGrainSize,
                               std::fma(legacy(0.0213937F), squared,
                                        -legacy(0.0014881F) * cubed)) +
                      legacy(1.3425F);
    densityRatio = std::fma(-legacy(1.1069031F), meanGrainSize,
                            std::fma(legacy(0.2290201F), squared,
                                     -legacy(0.0165406F) * cubed)) +
                   legacy(3.0455F);
  } else {
    soundSpeedRatio =
        std::fma(-legacy(0.0024324F), meanGrainSize, legacy(1.0019F));
    densityRatio =
        std::fma(-legacy(0.0012973F), meanGrainSize, legacy(1.1565F));
  }

  double attenuationCoefficient{};
  if (meanGrainSize >= -1.0 && meanGrainSize < 0.0) {
    attenuationCoefficient = legacy(0.4556F);
  } else if (meanGrainSize >= 0.0 && meanGrainSize < legacy(2.6F)) {
    attenuationCoefficient =
        std::fma(legacy(0.0245F), meanGrainSize, legacy(0.4556F));
  } else if (meanGrainSize >= legacy(2.6F) && meanGrainSize < legacy(4.5F)) {
    attenuationCoefficient =
        std::fma(legacy(0.1245F), meanGrainSize, legacy(0.1978F));
  } else if (meanGrainSize >= legacy(4.5F) && meanGrainSize < legacy(6.0F)) {
    attenuationCoefficient =
        std::fma(legacy(0.20098F), squared,
                 std::fma(-legacy(2.5228F), meanGrainSize, legacy(8.0399F)));
  } else if (meanGrainSize >= legacy(6.0F) && meanGrainSize < legacy(9.5F)) {
    attenuationCoefficient =
        std::fma(legacy(0.0117F), squared,
                 std::fma(-legacy(0.2041F), meanGrainSize, legacy(0.9431F)));
  } else {
    attenuationCoefficient = legacy(0.0601F);
  }
  if (!std::isfinite(soundSpeedRatio) || soundSpeedRatio <= 0.0 ||
      !std::isfinite(densityRatio) || densityRatio <= 0.0 ||
      !std::isfinite(attenuationCoefficient) || attenuationCoefficient < 0.0) {
    throw ValidationError(
        "grain size derives non-physical geoacoustic properties");
  }
  return GrainSizeMaterial{.meanGrainSize = meanGrainSize,
                           .soundSpeedRatio = soundSpeedRatio,
                           .densityRatio = densityRatio,
                           .attenuationCoefficient = attenuationCoefficient};
}

void validateReflectionTable(const SharedTabulatedReflectionTable& table) {
  if (!table || table->size() < 2U) {
    throw ValidationError(
        "tabulated reflection requires at least two table points");
  }
  for (std::size_t index = 0U; index < table->size(); ++index) {
    const TabulatedReflectionPoint& point = (*table)[index];
    requireFinite(point.angleDegrees, "reflectionTable.angleDegrees");
    requireFinite(point.magnitude, "reflectionTable.magnitude");
    requireFinite(point.phaseRadians, "reflectionTable.phaseRadians");
    if (point.magnitude < 0.0) {
      throw ValidationError("reflectionTable.magnitude must be non-negative");
    }
    if (index > 0U && (*table)[index - 1U].angleDegrees >= point.angleDegrees) {
      throw ValidationError(
          "reflectionTable angles must be strictly increasing");
    }
  }
}

void validateVolumeAttenuation(const VolumeAttenuation& attenuation) {
  switch (attenuation.model) {
    case VolumeAttenuationModel::None:
    case VolumeAttenuationModel::Thorp:
      if (!std::holds_alternative<std::monostate>(attenuation.parameters)) {
        throw ValidationError(
            "None and Thorp volume attenuation require empty parameters");
      }
      return;
    case VolumeAttenuationModel::FrancoisGarrison: {
      const auto* parameters =
          std::get_if<FrancoisGarrisonParameters>(&attenuation.parameters);
      if (parameters == nullptr) {
        throw ValidationError(
            "Francois-Garrison volume attenuation requires its parameters");
      }
      requireFinite(parameters->temperatureCelsius,
                    "volumeAttenuation.temperatureCelsius");
      requireFinite(parameters->salinityPsu, "volumeAttenuation.salinityPsu");
      requireFinite(parameters->pH, "volumeAttenuation.pH");
      requireFinite(parameters->meanDepthMeters,
                    "volumeAttenuation.meanDepthMeters");
      if (parameters->temperatureCelsius <= -273.0) {
        throw ValidationError(
            "volumeAttenuation.temperatureCelsius must exceed -273 C");
      }
      if (parameters->salinityPsu < 0.0) {
        throw ValidationError(
            "volumeAttenuation.salinityPsu must be non-negative");
      }
      if (parameters->meanDepthMeters < 0.0) {
        throw ValidationError(
            "volumeAttenuation.meanDepthMeters must be non-negative");
      }
      return;
    }
    case VolumeAttenuationModel::Biological: {
      const auto* layers =
          std::get_if<SharedBiologicalAttenuationLayers>(
              &attenuation.parameters);
      if (layers == nullptr || !*layers) {
        throw ValidationError(
            "biological volume attenuation requires immutable layers");
      }
      if ((*layers)->size() > 200U) {
        throw ValidationError(
            "biological volume attenuation supports at most 200 layers");
      }
      for (const BiologicalAttenuationLayer& layer : **layers) {
        requireFinite(layer.minimumDepth,
                      "volumeAttenuation.layer.minimumDepth");
        requireFinite(layer.maximumDepth,
                      "volumeAttenuation.layer.maximumDepth");
        requireFinite(layer.resonanceFrequency,
                      "volumeAttenuation.layer.resonanceFrequency");
        requireFinite(layer.qualityFactor,
                      "volumeAttenuation.layer.qualityFactor");
        requireFinite(
            layer.attenuationCoefficientDecibelsPerKilometer,
            "volumeAttenuation.layer.attenuationCoefficientDecibelsPerKilometer");
        if (layer.minimumDepth > layer.maximumDepth) {
          throw ValidationError(
              "biological layer minimum depth must not exceed maximum depth");
        }
        if (layer.resonanceFrequency <= 0.0) {
          throw ValidationError(
              "biological layer resonance frequency must be positive");
        }
        if (layer.qualityFactor <= 0.0) {
          throw ValidationError(
              "biological layer quality factor must be positive");
        }
        if (layer.attenuationCoefficientDecibelsPerKilometer < 0.0) {
          throw ValidationError(
              "biological layer attenuation coefficient must be non-negative");
        }
      }
      return;
    }
  }
  throw ValidationError("unknown volume attenuation model");
}

void validateQuadrilateralGrid(const SharedQuadrilateralSspGrid& grid,
                               std::size_t depthCount) {
  if (!grid || grid->depthCount != depthCount || grid->depthCount < 2U ||
      grid->rangeCount < 2U ||
      grid->rangesMeters.size() != grid->rangeCount) {
    throw ValidationError("quadrilateral SSP grid dimensions are invalid");
  }
  if (grid->rangeCount >
      std::numeric_limits<std::size_t>::max() / grid->depthCount) {
    throw ValidationError("quadrilateral SSP grid dimensions overflow");
  }
  if (grid->speedsDepthMajor.size() != grid->depthCount * grid->rangeCount) {
    throw ValidationError("quadrilateral SSP grid sample count is invalid");
  }
  for (std::size_t index = 0U; index < grid->rangeCount; ++index) {
    requireFinite(grid->rangesMeters[index], "quadrilateral SSP range");
    if (index > 0U &&
        grid->rangesMeters[index - 1U] >= grid->rangesMeters[index]) {
      throw ValidationError(
          "quadrilateral SSP ranges must be strictly increasing");
    }
  }
  for (double speed : grid->speedsDepthMajor) {
    requireFinite(speed, "quadrilateral SSP sound speed");
    if (speed <= 0.0) {
      throw ValidationError(
          "quadrilateral SSP sound speeds must be positive");
    }
  }
}

}  // namespace

SoundSpeedProfile::SoundSpeedProfile(
    std::vector<SoundSpeedPoint> points,
    SspInterpolationKind interpolationKind,
    SharedQuadrilateralSspGrid quadrilateralGrid)
    : points_(std::move(points)),
      interpolationKind_(interpolationKind),
      quadrilateralGrid_(std::move(quadrilateralGrid)) {
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
    validateRawAttenuation(point.attenuation, "soundSpeedProfile.attenuation");
    if (index > 0U && points_[index - 1U].depth >= point.depth) {
      throw ValidationError(
          "sound-speed profile depths must be strictly increasing");
    }
  }
  if (interpolationKind_ == SspInterpolationKind::Quadrilateral) {
    validateQuadrilateralGrid(quadrilateralGrid_, points_.size());
  } else if (quadrilateralGrid_) {
    throw ValidationError(
        "only quadrilateral SSP profiles can carry a quadrilateral grid");
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

SspInterpolationKind SoundSpeedProfile::interpolationKind() const noexcept {
  return interpolationKind_;
}

const SharedQuadrilateralSspGrid& SoundSpeedProfile::quadrilateralGrid()
    const noexcept {
  return quadrilateralGrid_;
}

double SoundSpeedProfile::quadrilateralRealSoundSpeedAt(Vec2 position) const {
  if (interpolationKind_ != SspInterpolationKind::Quadrilateral ||
      !quadrilateralGrid_) {
    throw ValidationError("quadrilateral SSP grid is not available");
  }
  requireFinite(position.range, "quadrilateral SSP query range");
  requireFinite(position.depth, "quadrilateral SSP query depth");
  if (position.range < quadrilateralGrid_->rangesMeters.front() ||
      position.range > quadrilateralGrid_->rangesMeters.back() ||
      position.depth < points_.front().depth ||
      position.depth > points_.back().depth) {
    throw ValidationError("quadrilateral SSP query is outside its grid");
  }
  // Launch planning must use the same depth-first arithmetic and cell
  // selection as ray geometry, rather than a separately rounded interpolation.
  return QuadrilateralSsp(*this).evaluate(position, 0U, 0U).soundSpeed;
}

BoundaryModel BoundaryModel::vacuum(double depth) {
  return vacuum(BoundaryGeometry::flat(depth, BoundaryOrientation::Upper));
}

BoundaryModel BoundaryModel::vacuum(BoundaryGeometry geometry) {
  return BoundaryModel(BoundaryKind::Vacuum, std::move(geometry), std::nullopt);
}

BoundaryModel BoundaryModel::rigid(double depth) {
  return rigid(BoundaryGeometry::flat(depth, BoundaryOrientation::Lower));
}

BoundaryModel BoundaryModel::rigid(BoundaryGeometry geometry) {
  return BoundaryModel(BoundaryKind::Rigid, std::move(geometry), std::nullopt);
}

BoundaryModel BoundaryModel::acousticHalfSpace(double depth,
                                               AcousticMaterial material) {
  return acousticHalfSpace(
      BoundaryGeometry::flat(depth, BoundaryOrientation::Lower),
      std::move(material));
}

BoundaryModel BoundaryModel::acousticHalfSpace(BoundaryGeometry geometry,
                                               AcousticMaterial material) {
  return acousticHalfSpace(std::move(geometry), std::move(material), {});
}

BoundaryModel BoundaryModel::acousticHalfSpace(
    BoundaryGeometry geometry, AcousticMaterial material,
    SharedLongBoundaryMaterials longMaterials) {
  validateMaterial(material);
  if (longMaterials && longMaterials->size() != geometry.nodes().size()) {
    throw ValidationError(
        "long-format material count must match boundary nodes");
  }
  if (longMaterials && longMaterials->empty()) {
    throw ValidationError("long-format material profile must not be empty");
  }
  if (longMaterials && geometry.isFlat()) {
    throw ValidationError(
        "long-format materials require range-dependent piecewise-linear "
        "boundary geometry");
  }
  if (longMaterials) {
    for (const AcousticMaterial& nodeMaterial : *longMaterials) {
      validateMaterial(nodeMaterial);
    }
  }
  return BoundaryModel(BoundaryKind::AcousticHalfSpace, std::move(geometry),
                       std::move(material), std::move(longMaterials));
}

BoundaryModel BoundaryModel::grainSizeHalfSpace(double depth,
                                                double meanGrainSize) {
  return grainSizeHalfSpace(
      BoundaryGeometry::flat(depth, BoundaryOrientation::Lower), meanGrainSize);
}

BoundaryModel BoundaryModel::grainSizeHalfSpace(BoundaryGeometry geometry,
                                                double meanGrainSize) {
  return BoundaryModel(BoundaryKind::GrainSizeHalfSpace, std::move(geometry),
                       std::nullopt, {}, grainSizeToGeoacoustic(meanGrainSize));
}

BoundaryModel BoundaryModel::tabulatedReflection(
    BoundaryGeometry geometry, SharedTabulatedReflectionTable table) {
  validateReflectionTable(table);
  return BoundaryModel(BoundaryKind::TabulatedReflection, std::move(geometry),
                       std::nullopt, {}, {}, std::move(table));
}

BoundaryModel::BoundaryModel(BoundaryKind kind, BoundaryGeometry geometry,
                             std::optional<AcousticMaterial> material,
                             SharedLongBoundaryMaterials longMaterials,
                             std::optional<GrainSizeMaterial> grainSizeMaterial,
                             SharedTabulatedReflectionTable reflectionTable)
    : kind_(kind),
      geometry_(std::move(geometry)),
      material_(std::move(material)),
      longMaterials_(std::move(longMaterials)),
      grainSizeMaterial_(std::move(grainSizeMaterial)),
      reflectionTable_(std::move(reflectionTable)) {
  if (kind_ == BoundaryKind::AcousticHalfSpace && !material_.has_value()) {
    throw ValidationError("acoustic half-space requires material properties");
  }
  if (kind_ != BoundaryKind::AcousticHalfSpace && material_.has_value()) {
    throw ValidationError(
        "only ordinary acoustic half-spaces can carry acoustic material");
  }
  if (kind_ != BoundaryKind::AcousticHalfSpace && longMaterials_) {
    throw ValidationError(
        "only ordinary acoustic half-spaces can carry segment materials");
  }
  if (kind_ == BoundaryKind::GrainSizeHalfSpace &&
      !grainSizeMaterial_.has_value()) {
    throw ValidationError("grain-size half-space requires grain properties");
  }
  if (kind_ != BoundaryKind::GrainSizeHalfSpace &&
      grainSizeMaterial_.has_value()) {
    throw ValidationError(
        "only grain-size half-spaces can carry grain properties");
  }
  if (kind_ == BoundaryKind::TabulatedReflection) {
    validateReflectionTable(reflectionTable_);
  } else if (reflectionTable_) {
    throw ValidationError(
        "only tabulated-reflection boundaries can carry a reflection table");
  }
}

BoundaryKind BoundaryModel::kind() const noexcept { return kind_; }

double BoundaryModel::depth() const noexcept {
  return geometry_.referenceDepth();
}

const BoundaryGeometry& BoundaryModel::geometry() const noexcept {
  return geometry_;
}

const std::optional<AcousticMaterial>& BoundaryModel::material()
    const noexcept {
  return material_;
}

const std::optional<GrainSizeMaterial>& BoundaryModel::grainSizeMaterial()
    const noexcept {
  return grainSizeMaterial_;
}

const SharedTabulatedReflectionTable& BoundaryModel::reflectionTable()
    const noexcept {
  return reflectionTable_;
}

bool BoundaryModel::hasRangeDependentMaterials() const noexcept {
  return static_cast<bool>(longMaterials_);
}

const AcousticMaterial& BoundaryModel::materialAtSegment(
    std::size_t segmentIndex) const {
  if (kind_ != BoundaryKind::AcousticHalfSpace || !material_.has_value()) {
    throw ValidationError(
        "only acoustic half-spaces provide material properties");
  }
  if (segmentIndex >= geometry_.segmentCount()) {
    throw ValidationError("boundary material segment index is out of range");
  }
  if (!longMaterials_) {
    return *material_;
  }
  const std::size_t nodeIndex =
      segmentIndex == 0U
          ? 0U
          : std::min(segmentIndex - 1U, longMaterials_->size() - 1U);
  return (*longMaterials_)[nodeIndex];
}

double BoundaryModel::materialAttenuationDepthAtSegment(
    std::size_t segmentIndex) const {
  static_cast<void>(materialAtSegment(segmentIndex));
  return longMaterials_ ? kLegacyLongBoundaryAttenuationDepth : depth();
}

Environment::Environment(SoundSpeedProfile soundSpeedProfile,
                         BoundaryModel seaSurface, BoundaryModel seabed,
                         VolumeAttenuation volumeAttenuation)
    : soundSpeedProfile_(std::move(soundSpeedProfile)),
      seaSurface_(std::move(seaSurface)),
      seabed_(std::move(seabed)),
      volumeAttenuation_(std::move(volumeAttenuation)) {
  validateVolumeAttenuation(volumeAttenuation_);
  if (seaSurface_.geometry().orientation() != BoundaryOrientation::Upper) {
    throw ValidationError("sea-surface geometry must be upper");
  }
  if (seabed_.geometry().orientation() != BoundaryOrientation::Lower) {
    throw ValidationError("seabed geometry must be lower");
  }
  std::vector<double> boundaryRanges;
  boundaryRanges.reserve(seaSurface_.geometry().nodes().size() +
                         seabed_.geometry().nodes().size() + 1U);
  for (Vec2 node : seaSurface_.geometry().nodes()) {
    boundaryRanges.push_back(node.range);
  }
  for (Vec2 node : seabed_.geometry().nodes()) {
    boundaryRanges.push_back(node.range);
  }
  if (boundaryRanges.empty()) {
    boundaryRanges.push_back(0.0);
  }
  std::sort(boundaryRanges.begin(), boundaryRanges.end());
  boundaryRanges.erase(
      std::unique(boundaryRanges.begin(), boundaryRanges.end()),
      boundaryRanges.end());
  for (double range : boundaryRanges) {
    if (seaSurface_.geometry().depthAt(range, 0U) >=
        seabed_.geometry().depthAt(range, 0U)) {
      throw ValidationError(
          "sea surface must remain strictly above the seabed");
    }
  }
  if (soundSpeedProfile_.minimumDepth() >
          seaSurface_.geometry().minimumDepth() ||
      soundSpeedProfile_.maximumDepth() < seabed_.geometry().maximumDepth()) {
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

const VolumeAttenuation& Environment::volumeAttenuation() const noexcept {
  return volumeAttenuation_;
}

double Environment::waterDepth() const noexcept {
  return seabed_.depth() - seaSurface_.depth();
}

}  // namespace rayreuse
