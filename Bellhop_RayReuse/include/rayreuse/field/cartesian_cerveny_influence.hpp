#pragma once

#include <array>
#include <complex>
#include <cstddef>
#include <optional>

#include "rayreuse/field/frequency_workspace.hpp"
#include "rayreuse/model/c_linear_ssp.hpp"
#include "rayreuse/model/environment.hpp"
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
};

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
};

[[nodiscard]] int updateCervenyKmah(
    std::complex<double> qLeft, std::complex<double> qRight,
    int currentKmah);

[[nodiscard]] double cervenyHermiteTaper(
    double offset, double fullValueRadius,
    double zeroValueRadius);

class CartesianCervenyInfluence {
 public:
  CartesianCervenyInfluence(
      Environment environment, ReceiverGrid receivers,
      CartesianCervenySettings settings = {});

  [[nodiscard]] std::optional<CartesianCervenyDiagnostic> accumulate(
      FrequencyWorkspace& workspace, const RayPath& path,
      const RayFrequencyState& frequencyState,
      std::complex<double> epsilon,
      std::optional<CartesianCervenyDiagnosticRequest>
          diagnosticRequest = std::nullopt) const;

 private:
  Environment environment_;
  ReceiverGrid receivers_;
  CartesianCervenySettings settings_;
  CLinearSsp soundSpeedProfile_;
  double receiverRangeDelta_{};
};

}  // namespace rayreuse
