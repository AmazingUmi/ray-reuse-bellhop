#pragma once

#include <complex>
#include <cstddef>
#include <optional>

#include "bellhop/field/frequency_workspace.hpp"
#include "bellhop/model/simulation_case.hpp"
#include "bellhop/ray/ray_path.hpp"

namespace bellhop {

struct GeometricHatDiagnosticRequest {
  std::size_t receiverRangeIndex{};
  std::size_t receiverDepthIndex{};
};

struct GeometricHatDiagnostic {
  bool evaluated{};
  std::size_t evaluationCount{};
  std::size_t receiverRangeIndex{};
  std::size_t receiverDepthIndex{};
  std::size_t leftPointIndex{};
  std::size_t rightPointIndex{};
  double interpolationWeight{};
  double normalOffset{};
  double qInterpolated{};
  double hatWeight{};
  double amplitudeConstant{};
  double causticPhase{};
  std::complex<double> delay{};
  std::complex<double> pressureIncrement{};
  double intensityIncrement{};
};

// Origin's geometric hat families share the same contribution law but use
// different receiver walkers: G projects receivers onto Cartesian ray
// segments, while g intersects ray normals with each receiver-depth line.
class GeometricHatInfluence {
 public:
  GeometricHatInfluence(
      ReceiverGrid receivers, CervenyCoordinateSystem coordinates,
      SourceGeometry sourceGeometry = SourceGeometry::Point,
      SimulationRunMode runMode =
          SimulationRunMode::CoherentTransmissionLoss);

  [[nodiscard]] std::optional<GeometricHatDiagnostic> accumulate(
      FrequencyWorkspace& workspace, const RayPath& path,
      const RayFrequencyState& frequencyState,
      double launchAngleSpacingRadians,
      std::optional<GeometricHatDiagnosticRequest> diagnosticRequest =
          std::nullopt) const;

  [[nodiscard]] std::optional<GeometricHatDiagnostic>
  accumulateIntensity(
      IntensityWorkspace& workspace, const RayPath& path,
      const RayFrequencyState& frequencyState,
      double launchAngleSpacingRadians,
      std::optional<GeometricHatDiagnosticRequest> diagnosticRequest =
          std::nullopt) const;

 private:
  [[nodiscard]] std::optional<GeometricHatDiagnostic> accumulateImpl(
      FrequencyWorkspace* pressureWorkspace,
      IntensityWorkspace* intensityWorkspace, const RayPath& path,
      const RayFrequencyState& frequencyState,
      double launchAngleSpacingRadians,
      std::optional<GeometricHatDiagnosticRequest> diagnosticRequest) const;

  ReceiverGrid receivers_;
  CervenyCoordinateSystem coordinates_{CervenyCoordinateSystem::Cartesian};
  SourceGeometry sourceGeometry_{SourceGeometry::Point};
  SimulationRunMode runMode_{
      SimulationRunMode::CoherentTransmissionLoss};
  double receiverRangeDelta_{};
};

}  // namespace bellhop
