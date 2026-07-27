#pragma once

#include <optional>
#include <vector>

namespace bellhop {

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

class SoundSpeedProfile {
 public:
  explicit SoundSpeedProfile(std::vector<SoundSpeedPoint> points);

  [[nodiscard]] const std::vector<SoundSpeedPoint>& points() const noexcept;
  [[nodiscard]] double minimumDepth() const noexcept;
  [[nodiscard]] double maximumDepth() const noexcept;

 private:
  std::vector<SoundSpeedPoint> points_;
};

enum class BoundaryKind {
  Vacuum,
  Rigid,
  AcousticHalfSpace,
};

struct AcousticMaterial {
  double compressionalSoundSpeed{};
  double shearSoundSpeed{};
  double density{};
  RawAttenuation compressionalAttenuation{};
  RawAttenuation shearAttenuation{};
};

class BoundaryModel {
 public:
  [[nodiscard]] static BoundaryModel vacuum(double depth);
  [[nodiscard]] static BoundaryModel rigid(double depth);
  [[nodiscard]] static BoundaryModel acousticHalfSpace(
      double depth, AcousticMaterial material);

  [[nodiscard]] BoundaryKind kind() const noexcept;
  [[nodiscard]] double depth() const noexcept;
  [[nodiscard]] const std::optional<AcousticMaterial>& material() const noexcept;

 private:
  BoundaryModel(BoundaryKind kind, double depth,
                std::optional<AcousticMaterial> material);

  BoundaryKind kind_;
  double depth_;
  std::optional<AcousticMaterial> material_;
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

}  // namespace bellhop
