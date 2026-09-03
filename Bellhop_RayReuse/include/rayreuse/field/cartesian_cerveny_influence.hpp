#pragma once

#include <array>
#include <complex>
#include <cstddef>
#include <optional>
#include <span>

#include "rayreuse/field/frequency_workspace.hpp"
#include "rayreuse/field/fused_pressure_workspace.hpp"
#include "rayreuse/model/beam_width.hpp"
#include "rayreuse/model/environment.hpp"
#include "rayreuse/model/sound_speed_evaluator.hpp"
#include "rayreuse/ray/ray_path.hpp"

namespace rayreuse {

enum class CervenyImageKind {
  True,
  Surface,
  Bottom,
};

struct CartesianCervenySettings {
  std::size_t imageCount{3U};
  int beamWindow{5};
  bool collectStatistics{false};
};

struct CartesianCervenyStatistics {
  std::size_t rayAccumulations{};
  std::size_t validatedRayPoints{};
  std::size_t validatedWorkspaceValues{};
  std::size_t activeRayPoints{};
  std::size_t segmentCandidates{};
  std::size_t eligibleSegments{};
  std::size_t receiverRangeEvaluations{};
  std::size_t receiverDepthEvaluations{};
  std::size_t imageEvaluations{};
  std::size_t windowRejections{};
  std::size_t taperRejections{};
  std::size_t nonzeroImageContributions{};
  // IGR-1 counter split: geometry-side counters count frequency-independent
  // traversal work on shared geometry; frequency-kernel counters count
  // per-frequency work executed on already-prepared geometry.  The legacy
  // counters above keep their original meaning; in the current
  // frequency-major kernel each new counter coincides in count with its
  // legacy counterpart.
  std::size_t geometrySegmentEvaluations{};
  std::size_t geometryRangeEvaluations{};
  std::size_t geometryDepthEvaluations{};
  std::size_t geometryImageGeometryEvaluations{};
  std::size_t frequencyRangeKernelEvaluations{};
  std::size_t frequencyImageKernelEvaluations{};
  double validationSeconds{};
  double precomputeSeconds{};
  double hotLoopSeconds{};
};

void accumulateCartesianCervenyStatistics(
    CartesianCervenyStatistics& total,
    const CartesianCervenyStatistics& value) noexcept;

struct CartesianCervenyDiagnosticRequest {
  std::size_t receiverRangeIndex{};
  std::size_t receiverDepthIndex{};
};

struct CartesianCervenyImageDiagnostic {
  CervenyImageKind kind{CervenyImageKind::True};
  double deltaDepth{};
  double polarity{};
  double windowMetric{};
  bool windowPassed{};
  double hermiteTaper{};
  std::complex<double> exponential{};
  std::complex<double> contribution{};
};

struct CartesianCervenyDiagnostic {
  bool evaluated{};
  std::size_t evaluationCount{};
  std::size_t receiverRangeIndex{};
  std::size_t receiverDepthIndex{};
  std::size_t leftPointIndex{};
  std::size_t rightPointIndex{};
  int kmahLeft{};
  int kmahFinal{};
  double interpolationWeight{};
  Vec2 interpolatedPosition;
  Vec2 interpolatedSlowness;
  double interpolatedSoundSpeed{};
  double rightAmplitude{};
  double rightPhase{};
  std::complex<double> epsilonLeft{};
  std::complex<double> pLeft{};
  std::complex<double> pRight{};
  std::complex<double> qLeft{};
  std::complex<double> qRight{};
  std::complex<double> qInterpolated{};
  std::complex<double> tauInterpolated{};
  std::complex<double> gammaLeft{};
  std::complex<double> gammaRight{};
  std::complex<double> gammaInterpolated{};
  std::complex<double> constantPrincipal{};
  std::complex<double> constantCorrected{};
  std::array<CartesianCervenyImageDiagnostic, 3> images{};
  std::complex<double> rawImageSum{};
  std::complex<double> finalContribution{};
  double intensityIncrement{};
};

[[nodiscard]] int updateCervenyKmah(
    std::complex<double> qLeft, std::complex<double> qRight, int currentKmah,
    BeamWidthMode widthMode = BeamWidthMode::MinimumWidth);

[[nodiscard]] double cervenyHermiteTaper(double offset, double fullValueRadius,
                                         double zeroValueRadius);

class CartesianCervenyInfluence {
 public:
  CartesianCervenyInfluence(
      Environment environment, ReceiverGrid receivers,
      CartesianCervenySettings settings = {},
      BeamWidthMode widthMode = BeamWidthMode::MinimumWidth,
      SourceGeometry sourceGeometry = SourceGeometry::Point);

  [[nodiscard]] std::optional<CartesianCervenyDiagnostic> accumulate(
      FrequencyWorkspace& workspace, const RayPath& path,
      const RayFrequencyState& frequencyState, std::complex<double> epsilon,
      std::optional<CartesianCervenyDiagnosticRequest> diagnosticRequest =
          std::nullopt,
      CartesianCervenyStatistics* statistics = nullptr) const;

  [[nodiscard]] std::optional<CartesianCervenyDiagnostic> accumulateIntensity(
      IntensityWorkspace& workspace, const RayPath& path,
      const RayFrequencyState& frequencyState, std::complex<double> epsilon,
      std::optional<CartesianCervenyDiagnosticRequest> diagnosticRequest =
          std::nullopt,
      CartesianCervenyStatistics* statistics = nullptr) const;

 private:
  friend class SingleFrequencySolver;
  friend class FusedRayReuseSolver;

  [[nodiscard]] std::optional<CartesianCervenyDiagnostic>
  accumulatePrevalidated(
      FrequencyWorkspace& workspace, const RayPath& path,
      const RayFrequencyState& frequencyState, std::complex<double> epsilon,
      CartesianCervenyStatistics* statistics = nullptr) const;

  [[nodiscard]] std::optional<CartesianCervenyDiagnostic>
  accumulateIntensityPrevalidated(
      IntensityWorkspace& workspace, const RayPath& path,
      const RayFrequencyState& frequencyState, std::complex<double> epsilon,
      CartesianCervenyStatistics* statistics = nullptr) const;

  // IGR-1 fused kernel (design §4): one ray, all frequencies. Reuses the
  // constructor validation of the per-frequency path; per-frequency
  // prevalidated checks run at entry; shared segment/range/image geometry is
  // computed once per ray and consumed by every frequency.
  [[nodiscard]] bool accumulateFusedPrevalidated(
      FusedPressureWorkspace& workspace,
      std::span<const double> frequencies, const RayPath& path,
      std::span<const RayFrequencyState> frequencyStates,
      std::span<const std::complex<double>> epsilons,
      std::size_t rangeBegin, std::size_t rangeEnd,
      CartesianCervenyStatistics* statistics = nullptr) const;

  template <bool CollectStatistics, std::size_t ImageCount>
  [[nodiscard]] bool accumulateFusedImpl(
      FusedPressureWorkspace& workspace,
      std::span<const double> frequencies, const RayPath& path,
      std::span<const RayFrequencyState> frequencyStates,
      std::span<const std::complex<double>> epsilons,
      std::size_t rangeBegin, std::size_t rangeEnd,
      CartesianCervenyStatistics* statistics) const;

  template <bool CollectStatistics>
  [[nodiscard]] std::optional<CartesianCervenyDiagnostic>
  accumulateWithImageCount(
      FrequencyWorkspace* pressureWorkspace,
      IntensityWorkspace* intensityWorkspace, const RayPath& path,
      const RayFrequencyState& frequencyState, std::complex<double> epsilon,
      std::optional<CartesianCervenyDiagnosticRequest> diagnosticRequest,
      CartesianCervenyStatistics* statistics) const;

  template <bool CollectStatistics, std::size_t ImageCount>
  [[nodiscard]] std::optional<CartesianCervenyDiagnostic> accumulateImpl(
      FrequencyWorkspace* pressureWorkspace,
      IntensityWorkspace* intensityWorkspace, const RayPath& path,
      const RayFrequencyState& frequencyState, std::complex<double> epsilon,
      std::optional<CartesianCervenyDiagnosticRequest> diagnosticRequest,
      CartesianCervenyStatistics* statistics) const;

  Environment environment_;
  ReceiverGrid receivers_;
  CartesianCervenySettings settings_;
  BeamWidthMode widthMode_;
  SourceGeometry sourceGeometry_{SourceGeometry::Point};
  GeometrySspEvaluator soundSpeedProfile_;
  double receiverRangeDelta_{};
};

}  // namespace rayreuse
