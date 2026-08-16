#pragma once

#include <cstddef>
#include <vector>

#include "bellhop/field/cartesian_cerveny_influence.hpp"
#include "bellhop/field/beam_epsilon.hpp"
#include "bellhop/field/frequency_workspace.hpp"
#include "bellhop/model/simulation_case.hpp"
#include "bellhop/ray/flat_boundary_reflection.hpp"

namespace bellhop {

[[nodiscard]] double semiCoherentLloydMirrorFactor(
    double frequency, double sourceSoundSpeed, double sourceDepth,
    double launchAngleRadians);
[[nodiscard]] double semiCoherentProjectedSourceAmplitude(
    double baseAmplitude, double frequency, double sourceSoundSpeed,
    double sourceDepth, double launchAngleRadians);

struct SingleFrequencyTimings {
  double traceSeconds{};
  double projectSeconds{};
  double influenceSeconds{};
  double scaleSeconds{};
};

struct SingleFrequencyResult {
  // The first workspace remains a named field for source-compatible
  // single-source callers.  Additional source slices follow source-major
  // order and are never coherently combined with it.
  FrequencyWorkspace workspace;
  std::vector<FrequencyWorkspace> additionalSourceWorkspaces;
  std::size_t rayCount{};
  std::size_t totalRayPointCount{};
  std::size_t rayCacheBytes{};
  std::size_t influenceThreadCount{1U};
  SingleFrequencyTimings timings;

  [[nodiscard]] std::size_t sourceCount() const noexcept;
  [[nodiscard]] const FrequencyWorkspace& sourceWorkspace(
      std::size_t sourceIndex) const;
};

class SingleFrequencySolver {
 public:
  [[nodiscard]] static SingleFrequencyResult solve(
      const SimulationCase& simulation,
      double epsilonMultiplier, double loopRange,
      CartesianCervenySettings influenceSettings = {},
      BeamWidthMode widthMode = BeamWidthMode::MinimumWidth,
      BoundaryCurvatureMode curvatureMode =
          BoundaryCurvatureMode::Standard,
      std::size_t influenceThreadCount = 1U);
};

}  // namespace bellhop
