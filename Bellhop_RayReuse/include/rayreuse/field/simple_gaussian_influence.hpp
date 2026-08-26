#pragma once

#include <complex>
#include <cstddef>
#include <optional>

#include "rayreuse/field/frequency_workspace.hpp"
#include "rayreuse/model/simulation_case.hpp"
#include "rayreuse/ray/ray_path.hpp"

namespace rayreuse {

struct SimpleGaussianDiagnosticRequest {
  std::size_t receiverRangeIndex{};
  std::size_t receiverDepthIndex{};
};

struct SimpleGaussianDiagnostic {
  bool evaluated{};
  std::size_t evaluationCount{};
  std::size_t receiverRangeIndex{};
  std::size_t receiverDepthIndex{};
  std::size_t leftPointIndex{};
  std::size_t rightPointIndex{};
  double interpolationWeight{};
  double qInterpolated{};
  double beta{};
  double gaussianA{};
  double normalization{};
  double legacyArcLength{};
  double closestPointDistance{};
  double offRayDistance{};
  double effectiveDistance{};
  double angularOffset{};
  double causticPhase{};
  double rightAmplitude{};
  double rightReflectionPhase{};
  std::complex<double> delay{};
  std::complex<double> pressureIncrement{};
};

class SimpleGaussianInfluence {
 public:
  SimpleGaussianInfluence(ReceiverGrid receivers,
                          double configuredStepLengthMeters);

  [[nodiscard]] std::optional<SimpleGaussianDiagnostic> accumulate(
      FrequencyWorkspace& workspace, const RayPath& path,
      const RayFrequencyState& frequencyState,
      double launchAngleSpacingRadians,
      std::optional<SimpleGaussianDiagnosticRequest> diagnosticRequest =
          std::nullopt) const;

 private:
  ReceiverGrid receivers_;
  double configuredStepLengthMeters_{};
};

}  // namespace rayreuse
