#pragma once

#include <array>
#include <complex>
#include <cstddef>
#include <memory>
#include <optional>

#include "bellhop/field/frequency_workspace.hpp"
#include "bellhop/field/beam_epsilon.hpp"
#include "bellhop/model/sound_speed_evaluator.hpp"
#include "bellhop/model/environment.hpp"
#include "bellhop/ray/ray_path.hpp"

namespace bellhop {

enum class CervenyImageKind {
  True,
  Surface,
  Bottom,
};

struct CartesianCervenySettings {
  std::size_t imageCount{3U};
  int beamWindow{5};
};

inline constexpr std::size_t kMaximumCartesianCervenyThreadCount = 256U;

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
    std::complex<double> qLeft, std::complex<double> qRight,
    int currentKmah,
    BeamWidthMode widthMode = BeamWidthMode::MinimumWidth);

[[nodiscard]] double cervenyHermiteTaper(
    double offset, double fullValueRadius,
    double zeroValueRadius);

class CartesianCervenyInfluence {
 public:
  CartesianCervenyInfluence(
      Environment environment, ReceiverGrid receivers,
      CartesianCervenySettings settings = {},
      BeamWidthMode widthMode = BeamWidthMode::MinimumWidth,
      SourceGeometry sourceGeometry = SourceGeometry::Point,
      SimulationRunMode runMode =
          SimulationRunMode::CoherentTransmissionLoss,
      std::size_t threadCount = 1U);

  ~CartesianCervenyInfluence();

  CartesianCervenyInfluence(const CartesianCervenyInfluence&) = delete;
  CartesianCervenyInfluence& operator=(
      const CartesianCervenyInfluence&) = delete;

  [[nodiscard]] std::size_t threadCount() const noexcept;

  [[nodiscard]] std::optional<CartesianCervenyDiagnostic> accumulate(
      FrequencyWorkspace& workspace, const RayPath& path,
      const RayFrequencyState& frequencyState,
      std::complex<double> epsilon,
      std::optional<CartesianCervenyDiagnosticRequest>
          diagnosticRequest = std::nullopt) const;

  [[nodiscard]] std::optional<CartesianCervenyDiagnostic>
  accumulateIntensity(
      IntensityWorkspace& workspace, const RayPath& path,
      const RayFrequencyState& frequencyState,
      std::complex<double> epsilon,
      std::optional<CartesianCervenyDiagnosticRequest>
          diagnosticRequest = std::nullopt) const;

 private:
  class DeterministicDepthTeam;

  [[nodiscard]] std::optional<CartesianCervenyDiagnostic> accumulateImpl(
      FrequencyWorkspace* pressureWorkspace,
      IntensityWorkspace* intensityWorkspace, const RayPath& path,
      const RayFrequencyState& frequencyState,
      std::complex<double> epsilon,
      std::optional<CartesianCervenyDiagnosticRequest>
          diagnosticRequest) const;

  Environment environment_;
  ReceiverGrid receivers_;
  CartesianCervenySettings settings_;
  BeamWidthMode widthMode_{BeamWidthMode::MinimumWidth};
  SourceGeometry sourceGeometry_{SourceGeometry::Point};
  SimulationRunMode runMode_{
      SimulationRunMode::CoherentTransmissionLoss};
  GeometrySspEvaluator soundSpeedProfile_;
  double receiverRangeDelta_{};
  std::size_t threadCount_{1U};
  mutable std::unique_ptr<DeterministicDepthTeam> depthTeam_;
};

}  // namespace bellhop
