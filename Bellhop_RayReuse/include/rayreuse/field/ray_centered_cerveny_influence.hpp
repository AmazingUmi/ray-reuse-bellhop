#pragma once

#include <complex>
#include <cstddef>
#include <optional>

#include "rayreuse/field/cartesian_cerveny_influence.hpp"
#include "rayreuse/field/frequency_workspace.hpp"
#include "rayreuse/model/environment.hpp"
#include "rayreuse/model/simulation_case.hpp"
#include "rayreuse/ray/ray_path.hpp"

namespace rayreuse {

struct RayCenteredCervenyDiagnosticRequest {
  std::size_t receiverRangeIndex{};
  std::size_t receiverDepthIndex{};
};

struct RayCenteredCervenyDiagnostic {
  bool evaluated{};
  std::size_t evaluationCount{};
  std::size_t receiverRangeIndex{};
  std::size_t receiverDepthIndex{};
  std::size_t leftPointIndex{};
  std::size_t rightPointIndex{};
  CervenyImageKind imageKind{CervenyImageKind::True};
  double interpolationWeight{};
  double normalOffset{};
  double hermiteTaper{};
  int kmahFinal{};
  std::complex<double> qInterpolated{};
  std::complex<double> gammaInterpolated{};
  std::complex<double> pressureContribution{};
  double intensityIncrement{};
};

class RayCenteredCervenyInfluence {
 public:
  RayCenteredCervenyInfluence(
      Environment environment, ReceiverGrid receivers,
      CartesianCervenySettings settings = {},
      BeamWidthMode widthMode = BeamWidthMode::MinimumWidth,
      SimulationRunMode runMode = SimulationRunMode::Coherent,
      FieldComponent fieldComponent = FieldComponent::Pressure);

  [[nodiscard]] std::optional<RayCenteredCervenyDiagnostic> accumulate(
      FrequencyWorkspace& workspace, const RayPath& path,
      const RayFrequencyState& frequencyState, std::complex<double> epsilon,
      std::optional<RayCenteredCervenyDiagnosticRequest> diagnosticRequest =
          std::nullopt) const;

  [[nodiscard]] std::optional<RayCenteredCervenyDiagnostic>
  accumulateIntensity(
      IntensityWorkspace& workspace, const RayPath& path,
      const RayFrequencyState& frequencyState, std::complex<double> epsilon,
      std::optional<RayCenteredCervenyDiagnosticRequest> diagnosticRequest =
          std::nullopt) const;

 private:
  [[nodiscard]] std::optional<RayCenteredCervenyDiagnostic> accumulateImpl(
      FrequencyWorkspace* pressureWorkspace,
      IntensityWorkspace* intensityWorkspace, const RayPath& path,
      const RayFrequencyState& frequencyState, std::complex<double> epsilon,
      std::optional<RayCenteredCervenyDiagnosticRequest>
          diagnosticRequest) const;

  Environment environment_;
  ReceiverGrid receivers_;
  CartesianCervenySettings settings_;
  BeamWidthMode widthMode_{BeamWidthMode::MinimumWidth};
  SimulationRunMode runMode_{SimulationRunMode::Coherent};
  FieldComponent fieldComponent_{FieldComponent::Pressure};
  double receiverRangeDelta_{};
};

}  // namespace rayreuse
