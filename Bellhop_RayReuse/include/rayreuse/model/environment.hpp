#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <vector>

#include "rayreuse/model/boundary_geometry.hpp"
#include "rayreuse/model/sound_speed_types.hpp"

namespace rayreuse {

enum class AttenuationUnit {
  NepersPerMeter,
  DecibelsPerMeter,
  DecibelsPerMeterPowerLaw,
  DecibelsPerMeterKilohertz,
  DecibelsPerWavelength,
  QualityFactor,
  LossParameter,
};

enum class VolumeAttenuationModel {
  None,
  Thorp,
  FrancoisGarrison,
  Biological,
};

struct RawAttenuation {
  double value{};
  AttenuationUnit unit{AttenuationUnit::DecibelsPerWavelength};
  double referenceFrequency{1.0};
  double powerLawExponent{1.0};
  double transitionFrequency{1.0};
  VolumeAttenuationModel volumeModel{VolumeAttenuationModel::None};
};

struct SoundSpeedPoint {
  double depth{};
  double soundSpeed{};
  double density{};
  RawAttenuation attenuation{};
};

// Range-dependent real sound-speed samples for the Origin 2-D `Q` option.
// Storage is depth-major: speedsDepthMajor[depthIndex * rangeCount +
// rangeIndex].  The reference SoundSpeedProfile still owns depths, density,
// and raw attenuation.
struct QuadrilateralSspGrid {
  std::vector<double> rangesMeters;
  std::vector<double> speedsDepthMajor;
  std::size_t depthCount{};
  std::size_t rangeCount{};
};

using SharedQuadrilateralSspGrid =
    std::shared_ptr<const QuadrilateralSspGrid>;

class SoundSpeedProfile {
 public:
  explicit SoundSpeedProfile(
      std::vector<SoundSpeedPoint> points,
      SspInterpolationKind interpolationKind = SspInterpolationKind::CLinear,
      SharedQuadrilateralSspGrid quadrilateralGrid = {});

  [[nodiscard]] const std::vector<SoundSpeedPoint>& points() const noexcept;
  [[nodiscard]] double minimumDepth() const noexcept;
  [[nodiscard]] double maximumDepth() const noexcept;
  [[nodiscard]] SspInterpolationKind interpolationKind() const noexcept;
  [[nodiscard]] const SharedQuadrilateralSspGrid& quadrilateralGrid()
      const noexcept;
  [[nodiscard]] double quadrilateralRealSoundSpeedAt(Vec2 position) const;

 private:
  std::vector<SoundSpeedPoint> points_;
  SspInterpolationKind interpolationKind_{SspInterpolationKind::CLinear};
  SharedQuadrilateralSspGrid quadrilateralGrid_;
};

enum class BoundaryKind {
  Vacuum,
  Rigid,
  AcousticHalfSpace,
  GrainSizeHalfSpace,
  TabulatedReflection,
};

struct AcousticMaterial {
  double compressionalSoundSpeed{};
  double shearSoundSpeed{};
  double density{};
  RawAttenuation compressionalAttenuation{};
  RawAttenuation shearAttenuation{};
};

struct GrainSizeMaterial {
  double meanGrainSize{};
  double soundSpeedRatio{};
  double densityRatio{};
  double attenuationCoefficient{};
};

struct TabulatedReflectionPoint {
  double angleDegrees{};
  double magnitude{};
  double phaseRadians{};
};

using TabulatedReflectionTable = std::vector<TabulatedReflectionPoint>;
using SharedTabulatedReflectionTable =
    std::shared_ptr<const TabulatedReflectionTable>;
using SharedLongBoundaryMaterials =
    std::shared_ptr<const std::vector<AcousticMaterial>>;
inline constexpr double kLegacyLongBoundaryAttenuationDepth = 1.0e20;

class BoundaryModel {
 public:
  [[nodiscard]] static BoundaryModel vacuum(double depth);
  [[nodiscard]] static BoundaryModel vacuum(BoundaryGeometry geometry);
  [[nodiscard]] static BoundaryModel rigid(double depth);
  [[nodiscard]] static BoundaryModel rigid(BoundaryGeometry geometry);
  [[nodiscard]] static BoundaryModel acousticHalfSpace(
      double depth, AcousticMaterial material);
  [[nodiscard]] static BoundaryModel acousticHalfSpace(
      BoundaryGeometry geometry, AcousticMaterial material);
  [[nodiscard]] static BoundaryModel acousticHalfSpace(
      BoundaryGeometry geometry, AcousticMaterial material,
      SharedLongBoundaryMaterials longMaterials);
  [[nodiscard]] static BoundaryModel grainSizeHalfSpace(double depth,
                                                        double meanGrainSize);
  [[nodiscard]] static BoundaryModel grainSizeHalfSpace(
      BoundaryGeometry geometry, double meanGrainSize);
  [[nodiscard]] static BoundaryModel tabulatedReflection(
      BoundaryGeometry geometry, SharedTabulatedReflectionTable table);

  [[nodiscard]] BoundaryKind kind() const noexcept;
  [[nodiscard]] double depth() const noexcept;
  [[nodiscard]] const BoundaryGeometry& geometry() const noexcept;
  [[nodiscard]] const std::optional<AcousticMaterial>& material()
      const noexcept;
  [[nodiscard]] const std::optional<GrainSizeMaterial>& grainSizeMaterial()
      const noexcept;
  [[nodiscard]] const SharedTabulatedReflectionTable& reflectionTable()
      const noexcept;
  [[nodiscard]] bool hasRangeDependentMaterials() const noexcept;
  [[nodiscard]] const AcousticMaterial& materialAtSegment(
      std::size_t segmentIndex) const;
  [[nodiscard]] double materialAttenuationDepthAtSegment(
      std::size_t segmentIndex) const;

 private:
  BoundaryModel(BoundaryKind kind, BoundaryGeometry geometry,
                std::optional<AcousticMaterial> material,
                SharedLongBoundaryMaterials longMaterials = {},
                std::optional<GrainSizeMaterial> grainSizeMaterial = {},
                SharedTabulatedReflectionTable reflectionTable = {});

  BoundaryKind kind_;
  BoundaryGeometry geometry_;
  std::optional<AcousticMaterial> material_;
  SharedLongBoundaryMaterials longMaterials_;
  std::optional<GrainSizeMaterial> grainSizeMaterial_;
  SharedTabulatedReflectionTable reflectionTable_;
};

class Environment {
 public:
  Environment(SoundSpeedProfile soundSpeedProfile, BoundaryModel seaSurface,
              BoundaryModel seabed);

  [[nodiscard]] const SoundSpeedProfile& soundSpeedProfile() const noexcept;
  [[nodiscard]] const BoundaryModel& seaSurface() const noexcept;
  [[nodiscard]] const BoundaryModel& seabed() const noexcept;
  [[nodiscard]] double waterDepth() const noexcept;

 private:
  SoundSpeedProfile soundSpeedProfile_;
  BoundaryModel seaSurface_;
  BoundaryModel seabed_;
};

}  // namespace rayreuse
