#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include "bellhop/model/environment.hpp"
#include "bellhop/model/launch_fan_planner.hpp"

namespace bellhop {

inline constexpr std::size_t kMaximumReceiverGridValues = 2'000'000U;
inline constexpr std::size_t kMaximumRunRayCount = 2'000'000U;

enum class ReceiverGridLayout {
  Rectilinear,
  Irregular,
};

enum class SimulationRunMode {
  CoherentTransmissionLoss,
  IncoherentTransmissionLoss,
  SemiCoherentTransmissionLoss,
  AsciiArrivals,
  BinaryArrivals,
  RayTrace,
};

enum class FieldAccumulationKind {
  None,
  ComplexPressure,
  Intensity,
};

[[nodiscard]] bool isTransmissionLossMode(SimulationRunMode mode);
[[nodiscard]] bool isArrivalMode(SimulationRunMode mode);
[[nodiscard]] FieldAccumulationKind fieldAccumulationKind(
    SimulationRunMode mode);
[[nodiscard]] bool usesLloydMirror(SimulationRunMode mode);

enum class FieldComponent {
  // Origin's Cartesian Cerveny path parses and reports all three values but
  // currently evaluates the same pressure field for each.  The distinction
  // is retained here for input/PRT compatibility and the later ray-centered
  // implementation, where V/H have observable formulas.
  Pressure,
  Vertical,
  Horizontal,
};

enum class SourceGeometry {
  Point,
  Line,
};

enum class CervenyCoordinateSystem {
  Cartesian,
  RayCentered,
};

enum class BeamFamily {
  CervenyGaussian,
  GeometricHat,
  GeometricGaussian,
  SimpleGaussian,
};

class ReceiverGrid {
 public:
  ReceiverGrid(
      std::vector<double> depths, std::vector<double> ranges,
      ReceiverGridLayout layout = ReceiverGridLayout::Rectilinear);

  [[nodiscard]] const std::vector<double>& depths() const noexcept;
  [[nodiscard]] const std::vector<double>& ranges() const noexcept;
  [[nodiscard]] std::size_t depthCount() const noexcept;
  [[nodiscard]] std::size_t rangeCount() const noexcept;
  [[nodiscard]] std::size_t receiversPerRange() const noexcept;
  [[nodiscard]] ReceiverGridLayout layout() const noexcept;
  [[nodiscard]] bool isIrregular() const noexcept;
  [[nodiscard]] double depthAt(std::size_t pressureDepthIndex,
                               std::size_t rangeIndex) const;

 private:
  std::vector<double> depths_;
  std::vector<double> ranges_;
  ReceiverGridLayout layout_;
};

class FrequencyGrid {
 public:
  explicit FrequencyGrid(std::vector<double> values);

  [[nodiscard]] const std::vector<double>& values() const noexcept;
  [[nodiscard]] std::size_t size() const noexcept;
  [[nodiscard]] double designFrequency() const noexcept;

 private:
  std::vector<double> values_;
};

struct Source {
  double depth{};
  double amplitude{1.0};
};

struct SourceBeamPatternSample {
  double angleDegrees{};
  double powerDecibels{};
};

class SourceBeamPattern {
 public:
  [[nodiscard]] static SourceBeamPattern omnidirectional();
  [[nodiscard]] static SourceBeamPattern directional(
      std::vector<SourceBeamPatternSample> samples);

  [[nodiscard]] double amplitudeForLaunchAngle(
      double launchAngleRadians) const;
  [[nodiscard]] bool isDirectional() const noexcept;
  [[nodiscard]] std::size_t size() const noexcept;
  [[nodiscard]] double minimumAngleDegrees() const noexcept;
  [[nodiscard]] double maximumAngleDegrees() const noexcept;

 private:
  SourceBeamPattern(std::vector<double> anglesDegrees,
                    std::vector<double> amplitudes,
                    bool directional);

  std::vector<double> anglesDegrees_;
  std::vector<double> amplitudes_;
  bool directional_{};
};

struct LaunchFan {
  double minimumAngle{};
  double maximumAngle{};
  std::optional<std::size_t> explicitLaunchAngleCount{};
  std::optional<LaunchAngleDegreeBounds> inputDegreeBounds{};
};

struct IntegratorSettings {
  double stepLength{};
  double rangeLimit{};
  double depthLimit{};
  std::size_t maximumRayPoints{};
};

class SimulationCase {
 public:
  SimulationCase(Environment environment, Source source,
                 ReceiverGrid receivers, FrequencyGrid frequencies,
                 LaunchFan launchFan, IntegratorSettings integrator,
                 SourceBeamPattern sourceBeamPattern =
                     SourceBeamPattern::omnidirectional(),
                 SimulationRunMode runMode =
                     SimulationRunMode::CoherentTransmissionLoss,
                 FieldComponent fieldComponent = FieldComponent::Pressure,
                 SourceGeometry sourceGeometry = SourceGeometry::Point,
                 CervenyCoordinateSystem cervenyCoordinateSystem =
                     CervenyCoordinateSystem::Cartesian,
                 BeamFamily beamFamily = BeamFamily::CervenyGaussian);
  SimulationCase(Environment environment, std::vector<Source> sources,
                 ReceiverGrid receivers, FrequencyGrid frequencies,
                 LaunchFan launchFan, IntegratorSettings integrator,
                 SourceBeamPattern sourceBeamPattern =
                     SourceBeamPattern::omnidirectional(),
                 SimulationRunMode runMode =
                     SimulationRunMode::CoherentTransmissionLoss,
                 FieldComponent fieldComponent = FieldComponent::Pressure,
                 SourceGeometry sourceGeometry = SourceGeometry::Point,
                 CervenyCoordinateSystem cervenyCoordinateSystem =
                     CervenyCoordinateSystem::Cartesian,
                 BeamFamily beamFamily = BeamFamily::CervenyGaussian);

  [[nodiscard]] const Environment& environment() const noexcept;
  [[nodiscard]] const Source& source() const noexcept;
  [[nodiscard]] const std::vector<Source>& sources() const noexcept;
  [[nodiscard]] std::size_t sourceCount() const noexcept;
  [[nodiscard]] const ReceiverGrid& receivers() const noexcept;
  [[nodiscard]] const FrequencyGrid& frequencies() const noexcept;
  [[nodiscard]] const LaunchFanPlan& launchFanPlan() const noexcept;
  [[nodiscard]] const IntegratorSettings& integrator() const noexcept;
  [[nodiscard]] const SourceBeamPattern& sourceBeamPattern() const noexcept;
  [[nodiscard]] SimulationRunMode runMode() const noexcept;
  [[nodiscard]] FieldComponent fieldComponent() const noexcept;
  [[nodiscard]] SourceGeometry sourceGeometry() const noexcept;
  [[nodiscard]] CervenyCoordinateSystem cervenyCoordinateSystem() const
      noexcept;
  [[nodiscard]] BeamFamily beamFamily() const noexcept;

 private:
  Environment environment_;
  std::vector<Source> sources_;
  ReceiverGrid receivers_;
  FrequencyGrid frequencies_;
  LaunchFanPlan launchFanPlan_;
  IntegratorSettings integrator_;
  SourceBeamPattern sourceBeamPattern_;
  SimulationRunMode runMode_;
  FieldComponent fieldComponent_;
  SourceGeometry sourceGeometry_;
  CervenyCoordinateSystem cervenyCoordinateSystem_;
  BeamFamily beamFamily_;
};

}  // namespace bellhop
