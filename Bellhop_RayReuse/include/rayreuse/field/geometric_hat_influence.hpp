#pragma once

#include <complex>
#include <cstddef>
#include <functional>
#include <optional>
#include <span>

#include "rayreuse/field/arrival_workspace.hpp"
#include "rayreuse/field/eigenray_hit.hpp"
#include "rayreuse/field/frequency_workspace.hpp"
#include "rayreuse/model/simulation_case.hpp"
#include "rayreuse/ray/ray_path.hpp"

namespace rayreuse {

class FusedPressureWorkspace;
class FusedIntensityWorkspace;
struct CartesianCervenyStatistics;

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

class GeometricHatInfluence {
 public:
  using EigenrayHitSink = std::function<void(const EigenrayHit&)>;

  explicit GeometricHatInfluence(
      ReceiverGrid receivers,
      CervenyCoordinateSystem coordinates = CervenyCoordinateSystem::Cartesian,
      SourceGeometry sourceGeometry = SourceGeometry::Point);

  [[nodiscard]] std::optional<GeometricHatDiagnostic> accumulate(
      FrequencyWorkspace& workspace, const RayPath& path,
      const RayFrequencyState& frequencyState, double launchAngleSpacingRadians,
      std::optional<GeometricHatDiagnosticRequest> diagnosticRequest =
          std::nullopt) const;

  [[nodiscard]] std::optional<GeometricHatDiagnostic> accumulateIntensity(
      IntensityWorkspace& workspace, const RayPath& path,
      const RayFrequencyState& frequencyState, double launchAngleSpacingRadians,
      std::optional<GeometricHatDiagnosticRequest> diagnosticRequest =
          std::nullopt) const;

  void accumulateArrivals(ArrivalWorkspace& workspace, const RayPath& path,
                          const RayFrequencyState& frequencyState,
                          double launchAngleSpacingRadians) const;

  void collectEigenrayHits(const EigenrayHitSink& sink, const RayPath& path,
                           const RayFrequencyState& frequencyState,
                           double launchAngleSpacingRadians) const;

 private:
  // IGR-3A unified-executor adapter (design §4): its accumulateFused /
  // accumulateFusedIntensity hooks forward to the private fused kernel
  // entries below (both coordinate systems — the kernel owns the internal
  // Cartesian/ray-centered traversal selection).
  friend struct GeometricHatFusedAdapter;

  [[nodiscard]] std::optional<GeometricHatDiagnostic> accumulateField(
      FrequencyWorkspace* pressureWorkspace,
      IntensityWorkspace* intensityWorkspace, const RayPath& path,
      const RayFrequencyState& frequencyState, double launchAngleSpacingRadians,
      std::optional<GeometricHatDiagnosticRequest> diagnosticRequest) const;

  [[nodiscard]] std::optional<GeometricHatDiagnostic>
  accumulateRayCenteredField(
      FrequencyWorkspace* pressureWorkspace,
      IntensityWorkspace* intensityWorkspace, const RayPath& path,
      const RayFrequencyState& frequencyState, double launchAngleSpacingRadians,
      std::optional<GeometricHatDiagnosticRequest> diagnosticRequest) const;

  // IGR-3A A04 fused kernel entries (design §5, no epsilon channel): one
  // ray, all frequency lanes, receiver cells [rangeBegin, rangeEnd). The
  // coherent and intensity twins share one traversal with a per-lane payload
  // branch at the store, mirroring the legacy single-traversal
  // accumulateField split (the internal once-per-ray selection between the
  // Cartesian and ray-centered fused traversals reproduces the legacy
  // coordinate routing).
  [[nodiscard]] bool accumulateFusedPrevalidated(
      FusedPressureWorkspace& workspace,
      std::span<const double> frequencies, const RayPath& path,
      std::span<const RayFrequencyState> frequencyStates,
      std::size_t rangeBegin, std::size_t rangeEnd,
      CartesianCervenyStatistics* statistics = nullptr) const;

  [[nodiscard]] bool accumulateFusedIntensityPrevalidated(
      FusedIntensityWorkspace& workspace,
      std::span<const double> frequencies, const RayPath& path,
      std::span<const RayFrequencyState> frequencyStates,
      std::size_t rangeBegin, std::size_t rangeEnd,
      CartesianCervenyStatistics* statistics = nullptr) const;

  template <bool IntensityPayload, typename Workspace>
  [[nodiscard]] bool accumulateFusedImpl(
      Workspace& workspace, std::span<const double> frequencies,
      const RayPath& path, std::span<const RayFrequencyState> frequencyStates,
      std::size_t rangeBegin, std::size_t rangeEnd) const;

  template <bool IntensityPayload, typename Workspace>
  [[nodiscard]] bool accumulateFusedCartesian(
      Workspace& workspace, const RayPath& path,
      std::span<const RayFrequencyState> frequencyStates,
      std::size_t rangeBegin, std::size_t rangeEnd) const;

  template <bool IntensityPayload, typename Workspace>
  [[nodiscard]] bool accumulateFusedRayCentered(
      Workspace& workspace, const RayPath& path,
      std::span<const RayFrequencyState> frequencyStates,
      std::size_t rangeBegin, std::size_t rangeEnd) const;

  // IGR-3A A04 fused-run state (design §4/§5): the frozen adapter interface
  // carries no launch-angle spacing to the fused kernel entries (the Hat
  // family has no epsilon channel to hide it in), so the adapter installs
  // the run's launch-fan angle step once after constructing the kernel with
  // the verbatim single-frequency arguments. The public per-frequency
  // entries keep receiving the spacing per call and never read this field.
  void setFusedLaunchAngleStep(double launchAngleStep);

  ReceiverGrid receivers_;
  CervenyCoordinateSystem coordinates_{CervenyCoordinateSystem::Cartesian};
  SourceGeometry sourceGeometry_{SourceGeometry::Point};
  double receiverRangeDelta_{};
  double fusedLaunchAngleStep_{};
};

}  // namespace rayreuse
