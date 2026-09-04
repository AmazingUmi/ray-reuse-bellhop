#pragma once

#include <complex>
#include <cstddef>
#include <optional>
#include <span>

#include "rayreuse/field/cartesian_cerveny_influence.hpp"
#include "rayreuse/field/frequency_workspace.hpp"
#include "rayreuse/model/environment.hpp"
#include "rayreuse/model/simulation_case.hpp"
#include "rayreuse/ray/ray_path.hpp"

namespace rayreuse {

class FusedIntensityWorkspace;

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
      FieldComponent fieldComponent = FieldComponent::Pressure,
      SourceGeometry sourceGeometry = SourceGeometry::Point);

  [[nodiscard]] std::optional<RayCenteredCervenyDiagnostic> accumulate(
      FrequencyWorkspace& workspace, const RayPath& path,
      const RayFrequencyState& frequencyState, std::complex<double> epsilon,
      std::optional<RayCenteredCervenyDiagnosticRequest> diagnosticRequest =
          std::nullopt) const;

  [[nodiscard]] std::optional<RayCenteredCervenyDiagnostic> accumulateIntensity(
      IntensityWorkspace& workspace, const RayPath& path,
      const RayFrequencyState& frequencyState, std::complex<double> epsilon,
      std::optional<RayCenteredCervenyDiagnosticRequest> diagnosticRequest =
          std::nullopt) const;

 private:
  // IGR-3A unified-executor adapter (design §4): its accumulateFused /
  // accumulateFusedIntensity hooks forward to the private fused kernel
  // entries below.
  friend struct RayCenteredCervenyFusedAdapter;

  [[nodiscard]] std::optional<RayCenteredCervenyDiagnostic> accumulateImpl(
      FrequencyWorkspace* pressureWorkspace,
      IntensityWorkspace* intensityWorkspace, const RayPath& path,
      const RayFrequencyState& frequencyState, std::complex<double> epsilon,
      std::optional<RayCenteredCervenyDiagnosticRequest> diagnosticRequest)
      const;

  // IGR-3A A03 fused kernel entries (design §5): one ray, all frequency
  // lanes, receiver cells [rangeBegin, rangeEnd). The coherent and intensity
  // twins share one traversal (accumulateFusedImpl below) with a per-lane
  // payload branch at the store, mirroring the legacy single-traversal
  // accumulateImpl. Entry-kind validation matches the public per-frequency
  // entries (coherent requires Coherent, intensity requires I/S).
  [[nodiscard]] bool accumulateFusedPrevalidated(
      FusedPressureWorkspace& workspace,
      std::span<const double> frequencies, const RayPath& path,
      std::span<const RayFrequencyState> frequencyStates,
      std::span<const std::complex<double>> epsilons,
      std::size_t rangeBegin, std::size_t rangeEnd,
      CartesianCervenyStatistics* statistics = nullptr) const;

  [[nodiscard]] bool accumulateFusedIntensityPrevalidated(
      FusedIntensityWorkspace& workspace,
      std::span<const double> frequencies, const RayPath& path,
      std::span<const RayFrequencyState> frequencyStates,
      std::span<const std::complex<double>> epsilons,
      std::size_t rangeBegin, std::size_t rangeEnd,
      CartesianCervenyStatistics* statistics = nullptr) const;

  template <bool IntensityPayload, typename Workspace>
  [[nodiscard]] bool accumulateFusedImpl(
      Workspace& workspace, std::span<const double> frequencies,
      const RayPath& path, std::span<const RayFrequencyState> frequencyStates,
      std::span<const std::complex<double>> epsilons,
      std::size_t rangeBegin, std::size_t rangeEnd,
      CartesianCervenyStatistics* statistics) const;

  Environment environment_;
  ReceiverGrid receivers_;
  CartesianCervenySettings settings_;
  BeamWidthMode widthMode_{BeamWidthMode::MinimumWidth};
  SimulationRunMode runMode_{SimulationRunMode::Coherent};
  FieldComponent fieldComponent_{FieldComponent::Pressure};
  SourceGeometry sourceGeometry_{SourceGeometry::Point};
  double receiverRangeDelta_{};
};

}  // namespace rayreuse
