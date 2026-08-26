#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include "rayreuse/model/beam_curvature.hpp"
#include "rayreuse/model/environment.hpp"
#include "rayreuse/model/launch_fan_planner.hpp"

namespace rayreuse {

class ReceiverGrid {
 public:
  ReceiverGrid(std::vector<double> depths, std::vector<double> ranges);

  [[nodiscard]] const std::vector<double>& depths() const noexcept;
  [[nodiscard]] const std::vector<double>& ranges() const noexcept;
  [[nodiscard]] std::size_t depthCount() const noexcept;
  [[nodiscard]] std::size_t rangeCount() const noexcept;

 private:
  std::vector<double> depths_;
  std::vector<double> ranges_;
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

enum class SimulationRunMode {
  Coherent,
  Incoherent,
  SemiCoherent,
  RayTrace,
  AsciiArrivals,
  BinaryArrivals,
  Eigenray,
};

enum class FieldAccumulationKind {
  None,
  ComplexPressure,
  Intensity,
};

enum class FieldComponent {
  Pressure,
  Vertical,
  Horizontal,
};

[[nodiscard]] bool isTransmissionLossMode(SimulationRunMode mode);
[[nodiscard]] FieldAccumulationKind fieldAccumulationKind(
    SimulationRunMode mode);
[[nodiscard]] bool usesLloydMirror(SimulationRunMode mode);

enum class BeamFamily {
  CervenyGaussian,
  GeometricHat,
  GeometricGaussian,
  SimpleGaussian,
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

  [[nodiscard]] double amplitudeForLaunchAngle(double launchAngleRadians) const;
  [[nodiscard]] bool isDirectional() const noexcept;
  [[nodiscard]] std::size_t size() const noexcept;
  [[nodiscard]] double minimumAngleDegrees() const noexcept;
  [[nodiscard]] double maximumAngleDegrees() const noexcept;

 private:
  SourceBeamPattern(std::vector<double> anglesDegrees,
                    std::vector<double> amplitudes, bool directional);

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
  SimulationCase(Environment environment, Source source, ReceiverGrid receivers,
                 FrequencyGrid frequencies, LaunchFan launchFan,
                 IntegratorSettings integrator,
                 SourceBeamPattern sourceBeamPattern =
                     SourceBeamPattern::omnidirectional(),
                 SimulationRunMode runMode = SimulationRunMode::Coherent,
                 BeamFamily beamFamily = BeamFamily::CervenyGaussian,
                 FieldComponent fieldComponent = FieldComponent::Pressure,
                 BoundaryCurvatureMode curvatureMode =
                     BoundaryCurvatureMode::Standard);

  [[nodiscard]] const Environment& environment() const noexcept;
  [[nodiscard]] const Source& source() const noexcept;
  [[nodiscard]] const ReceiverGrid& receivers() const noexcept;
  [[nodiscard]] const FrequencyGrid& frequencies() const noexcept;
  [[nodiscard]] const LaunchFanPlan& launchFanPlan() const noexcept;
  [[nodiscard]] const IntegratorSettings& integrator() const noexcept;
  [[nodiscard]] const SourceBeamPattern& sourceBeamPattern() const noexcept;
  [[nodiscard]] SimulationRunMode runMode() const noexcept;
  [[nodiscard]] BeamFamily beamFamily() const noexcept;
  [[nodiscard]] FieldComponent fieldComponent() const noexcept;
  [[nodiscard]] BoundaryCurvatureMode curvatureMode() const noexcept;

 private:
  Environment environment_;
  Source source_;
  ReceiverGrid receivers_;
  FrequencyGrid frequencies_;
  LaunchFanPlan launchFanPlan_;
  IntegratorSettings integrator_;
  SourceBeamPattern sourceBeamPattern_;
  SimulationRunMode runMode_;
  BeamFamily beamFamily_;
  FieldComponent fieldComponent_;
  BoundaryCurvatureMode curvatureMode_;
};

}  // namespace rayreuse
