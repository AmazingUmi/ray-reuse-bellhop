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
class BroadbandArrivalWorkspace;
struct CartesianCervenyStatistics;
struct ArrivalAccumulationStatistics;

enum class GeometricGaussianWidthBranch {
  Geometric,
  NearField,
  WavelengthCap,
};

struct GeometricGaussianDiagnosticRequest {
  std::size_t receiverRangeIndex{};
  std::size_t receiverDepthIndex{};
};

struct GeometricGaussianDiagnostic {
  bool evaluated{};
  std::size_t evaluationCount{};
  std::size_t receiverRangeIndex{};
  std::size_t receiverDepthIndex{};
  std::size_t leftPointIndex{};
  std::size_t rightPointIndex{};
  double interpolationWeight{};
  double normalOffset{};
  double qInterpolated{};
  double geometricSigma{};
  double nearFieldSigma{};
  double wavelengthSigma{};
  double sigma1{};
  GeometricGaussianWidthBranch widthBranch{
      GeometricGaussianWidthBranch::Geometric};
  double gaussianWeight{};
  double amplitudeConstant{};
  double causticPhase{};
  std::complex<double> delay{};
  std::complex<double> pressureIncrement{};
  double intensityIncrement{};
};

class GeometricGaussianInfluence {
 public:
  using EigenrayHitSink = std::function<void(const EigenrayHit&)>;

  explicit GeometricGaussianInfluence(
      ReceiverGrid receivers,
      SourceGeometry sourceGeometry = SourceGeometry::Point);

  [[nodiscard]] std::optional<GeometricGaussianDiagnostic> accumulate(
      FrequencyWorkspace& workspace, const RayPath& path,
      const RayFrequencyState& frequencyState, double launchAngleSpacingRadians,
      std::optional<GeometricGaussianDiagnosticRequest> diagnosticRequest =
          std::nullopt) const;

  [[nodiscard]] std::optional<GeometricGaussianDiagnostic> accumulateIntensity(
      IntensityWorkspace& workspace, const RayPath& path,
      const RayFrequencyState& frequencyState, double launchAngleSpacingRadians,
      std::optional<GeometricGaussianDiagnosticRequest> diagnosticRequest =
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
  // entries below (Cartesian only — the family has no ray-centered variant).
  friend struct GeometricGaussianFusedAdapter;

  [[nodiscard]] std::optional<GeometricGaussianDiagnostic> accumulateField(
      FrequencyWorkspace* pressureWorkspace,
      IntensityWorkspace* intensityWorkspace, const RayPath& path,
      const RayFrequencyState& frequencyState, double launchAngleSpacingRadians,
      std::optional<GeometricGaussianDiagnosticRequest> diagnosticRequest)
      const;

  // IGR-3A A05 fused kernel entries (design §5, no epsilon channel): one
  // ray, all frequency lanes, receiver cells [rangeBegin, rangeEnd). The
  // coherent and intensity twins share one traversal with a per-lane payload
  // branch at the store, mirroring the legacy single-traversal
  // accumulateField.
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

  [[nodiscard]] bool accumulateFusedArrivalsPrevalidated(
      BroadbandArrivalWorkspace& workspace,
      std::span<const double> frequencies, const RayPath& path,
      std::span<const RayFrequencyState> frequencyStates,
      std::size_t rangeBegin, std::size_t rangeEnd,
      ArrivalAccumulationStatistics& statistics) const;

  template <bool IntensityPayload, bool ArrivalPayload, typename Workspace>
  [[nodiscard]] bool accumulateFusedImpl(
      Workspace& workspace, std::span<const double> frequencies,
      const RayPath& path, std::span<const RayFrequencyState> frequencyStates,
      std::size_t rangeBegin, std::size_t rangeEnd,
      ArrivalAccumulationStatistics* arrivalStatistics = nullptr) const;

  // IGR-3A A05 fused-run state (design §4/§5, A04 Hat precedent): the frozen
  // adapter interface carries no launch-angle spacing to the fused kernel
  // entries (the family has no epsilon channel to hide it in), so the adapter
  // installs the run's launch-fan angle step once after constructing the
  // kernel with the verbatim single-frequency arguments. The public
  // per-frequency entries keep receiving the spacing per call and never read
  // this field.
  void setFusedLaunchAngleStep(double launchAngleStep);

  ReceiverGrid receivers_;
  SourceGeometry sourceGeometry_{SourceGeometry::Point};
  double fusedLaunchAngleStep_{};
};

}  // namespace rayreuse
