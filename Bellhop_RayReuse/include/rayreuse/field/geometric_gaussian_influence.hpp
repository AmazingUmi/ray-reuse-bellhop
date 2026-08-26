#pragma once

#include <complex>
#include <cstddef>
#include <functional>
#include <optional>

#include "rayreuse/field/arrival_workspace.hpp"
#include "rayreuse/field/eigenray_hit.hpp"
#include "rayreuse/field/frequency_workspace.hpp"
#include "rayreuse/model/simulation_case.hpp"
#include "rayreuse/ray/ray_path.hpp"

namespace rayreuse {

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

  explicit GeometricGaussianInfluence(ReceiverGrid receivers);

  [[nodiscard]] std::optional<GeometricGaussianDiagnostic> accumulate(
      FrequencyWorkspace& workspace, const RayPath& path,
      const RayFrequencyState& frequencyState,
      double launchAngleSpacingRadians,
      std::optional<GeometricGaussianDiagnosticRequest> diagnosticRequest =
          std::nullopt) const;

  [[nodiscard]] std::optional<GeometricGaussianDiagnostic>
  accumulateIntensity(
      IntensityWorkspace& workspace, const RayPath& path,
      const RayFrequencyState& frequencyState,
      double launchAngleSpacingRadians,
      std::optional<GeometricGaussianDiagnosticRequest> diagnosticRequest =
          std::nullopt) const;

  void accumulateArrivals(ArrivalWorkspace& workspace, const RayPath& path,
                          const RayFrequencyState& frequencyState,
                          double launchAngleSpacingRadians) const;

  void collectEigenrayHits(const EigenrayHitSink& sink, const RayPath& path,
                           const RayFrequencyState& frequencyState,
                           double launchAngleSpacingRadians) const;

 private:
  [[nodiscard]] std::optional<GeometricGaussianDiagnostic> accumulateField(
      FrequencyWorkspace* pressureWorkspace,
      IntensityWorkspace* intensityWorkspace, const RayPath& path,
      const RayFrequencyState& frequencyState,
      double launchAngleSpacingRadians,
      std::optional<GeometricGaussianDiagnosticRequest>
          diagnosticRequest) const;

  ReceiverGrid receivers_;
};

}  // namespace rayreuse
