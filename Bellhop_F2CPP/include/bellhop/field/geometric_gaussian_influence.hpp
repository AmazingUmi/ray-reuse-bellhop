#pragma once

#include <complex>
#include <cstddef>
#include <functional>
#include <optional>

#include "bellhop/field/arrival_workspace.hpp"
#include "bellhop/field/eigenray_hit.hpp"
#include "bellhop/field/frequency_workspace.hpp"
#include "bellhop/model/simulation_case.hpp"
#include "bellhop/ray/ray_path.hpp"

namespace bellhop {

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

// Origin's B family: geometrically spreading Gaussian beams evaluated in
// Cartesian ray coordinates.
class GeometricGaussianInfluence {
 public:
  using EigenrayHitSink = std::function<void(const EigenrayHit&)>;

  explicit GeometricGaussianInfluence(
      ReceiverGrid receivers,
      SourceGeometry sourceGeometry = SourceGeometry::Point,
      SimulationRunMode runMode =
          SimulationRunMode::CoherentTransmissionLoss);

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

  [[nodiscard]] std::optional<GeometricGaussianDiagnostic>
  accumulateArrivals(
      ArrivalWorkspace& workspace, const RayPath& path,
      const RayFrequencyState& frequencyState,
      double launchAngleSpacingRadians,
      std::optional<GeometricGaussianDiagnosticRequest> diagnosticRequest =
          std::nullopt) const;

  void collectEigenrayHits(const EigenrayHitSink& sink, const RayPath& path,
                           const RayFrequencyState& frequencyState,
                           double launchAngleSpacingRadians) const;

 private:
  [[nodiscard]] std::optional<GeometricGaussianDiagnostic> accumulateImpl(
      FrequencyWorkspace* pressureWorkspace,
      IntensityWorkspace* intensityWorkspace,
      ArrivalWorkspace* arrivalWorkspace, const RayPath& path,
      const RayFrequencyState& frequencyState,
      double launchAngleSpacingRadians,
      std::optional<GeometricGaussianDiagnosticRequest>
          diagnosticRequest,
      const EigenrayHitSink* eigenraySink = nullptr) const;

  ReceiverGrid receivers_;
  SourceGeometry sourceGeometry_{SourceGeometry::Point};
  SimulationRunMode runMode_{
      SimulationRunMode::CoherentTransmissionLoss};
};

}  // namespace bellhop
