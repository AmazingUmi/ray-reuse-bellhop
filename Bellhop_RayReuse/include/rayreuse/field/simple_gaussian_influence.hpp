#pragma once

#include <complex>
#include <cstddef>
#include <optional>
#include <span>

#include "rayreuse/field/frequency_workspace.hpp"
#include "rayreuse/model/simulation_case.hpp"
#include "rayreuse/ray/ray_path.hpp"

namespace rayreuse {

class FusedPressureWorkspace;
struct CartesianCervenyStatistics;

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
  SimpleGaussianInfluence(
      ReceiverGrid receivers, double configuredStepLengthMeters,
      SourceGeometry sourceGeometry = SourceGeometry::Point);

  [[nodiscard]] std::optional<SimpleGaussianDiagnostic> accumulate(
      FrequencyWorkspace& workspace, const RayPath& path,
      const RayFrequencyState& frequencyState, double launchAngleSpacingRadians,
      std::optional<SimpleGaussianDiagnosticRequest> diagnosticRequest =
          std::nullopt) const;

 private:
  // IGR-3A unified-executor adapter (design §4): its accumulateFused hook
  // forwards to the private fused kernel entry below. Coherent only — the
  // family's legal TL run modes are coherent-only (design §9), so the
  // adapter defines no intensity twins and no intensity kernel entry exists.
  friend struct SimpleGaussianFusedAdapter;

  // IGR-3A A06 fused kernel entry (design §5/§8, no epsilon channel,
  // coherent only): one ray, all frequency lanes, receiver cells
  // [rangeBegin, rangeEnd) of the fused pressure workspace. The legacy
  // traversal is reproduced exactly — segment loop over the union active
  // prefix, monotone range cursor, shared depth rows (rectilinear semantics;
  // no irregular support added).
  [[nodiscard]] bool accumulateFusedPrevalidated(
      FusedPressureWorkspace& workspace,
      std::span<const double> frequencies, const RayPath& path,
      std::span<const RayFrequencyState> frequencyStates,
      std::size_t rangeBegin, std::size_t rangeEnd,
      CartesianCervenyStatistics* statistics = nullptr) const;

  // IGR-3A A06 fused-run state (design §4/§5, A04/A05 precedent): the frozen
  // adapter interface carries no launch-angle spacing to the fused kernel
  // entry, while the legacy per-frequency accumulate keeps receiving it per
  // call (single_frequency_solver.cpp:304 passes
  // launchFanPlan().launchAngleStep). The constructor's
  // configuredStepLengthMeters is a different quantity — the integrator step
  // length of the legacy arc length — and cannot carry the beam width. The
  // adapter installs the run's launch-fan angle step once after constructing
  // the kernel with the verbatim single-frequency arguments; the public
  // per-frequency entry never reads this field.
  void setFusedLaunchAngleStep(double launchAngleStep);

  ReceiverGrid receivers_;
  double configuredStepLengthMeters_{};
  double fusedLaunchAngleStep_{};
};

}  // namespace rayreuse
