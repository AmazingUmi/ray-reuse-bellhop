#pragma once

#include <cstddef>
#include <vector>

#include "rayreuse/cache/ray_path_cache.hpp"
#include "rayreuse/field/cartesian_cerveny_influence.hpp"
#include "rayreuse/field/frequency_workspace.hpp"
#include "rayreuse/model/simulation_case.hpp"

namespace rayreuse {

[[nodiscard]] double semiCoherentLloydMirrorFactor(double frequency,
                                                   double sourceSoundSpeed,
                                                   double sourceDepth,
                                                   double launchAngleRadians);

[[nodiscard]] double semiCoherentProjectedSourceAmplitude(
    double baseAmplitude, double frequency, double sourceSoundSpeed,
    double sourceDepth, double launchAngleRadians);

struct SingleFrequencyTimings {
  double traceSeconds{};
  double projectSeconds{};
  double influenceSeconds{};
  double scaleSeconds{};
  CartesianCervenyStatistics influenceStatistics;
};

// Per-frequency product state. Mirrors the F2CPP result shape: `workspace`
// holds the first (shallowest) source's field and `additionalSourceWorkspaces`
// holds sources 1..NSz-1 in the model's depth-ascending source order.
struct SingleFrequencyResult {
  FrequencyWorkspace workspace;
  std::vector<FrequencyWorkspace> additionalSourceWorkspaces;
  std::size_t rayCount{};
  std::size_t totalRayPointCount{};
  // Peak per-source frozen cache bytes (F2CPP reports the max over sources).
  std::size_t rayCacheBytes{};
  SingleFrequencyTimings timings;

  [[nodiscard]] std::size_t sourceCount() const noexcept;
  [[nodiscard]] const FrequencyWorkspace& sourceWorkspace(
      std::size_t sourceIndex) const;
};

struct RayFanTraceResult {
  RayPathCache cache;
  std::size_t totalRayPointCount{};
  double traceSeconds{};
};

class SingleFrequencySolver {
 public:
  // First-source fan trace. Legacy single-source entry point; with NSz > 1 it
  // traces sources().front() (the shallowest source) until the product side
  // migrates to the per-source entries.
  [[nodiscard]] static RayFanTraceResult traceRayFan(
      const SimulationCase& simulation);

  // Traces one source's launch fan into an independent frozen cache. The
  // shared launch-angle set comes from SimulationCase::launchFanPlan() (F2CPP
  // plans one fan outside the source loop). Trace failures are reported with
  // the F2CPP-aligned diagnostic carrying the source index.
  [[nodiscard]] static RayFanTraceResult traceSourceFan(
      const SimulationCase& simulation, std::size_t sourceIndex);

  // Traces every source into NSz independent frozen caches, one entry per
  // SimulationCase::sources() entry (depth-ascending). The returned vector is
  // the solver-side owner of the per-source frozen geometry; each cache is
  // individually frozen and individually fingerprinted.
  [[nodiscard]] static std::vector<RayFanTraceResult> traceAllSourceFans(
      const SimulationCase& simulation);

  // Projects one source's frozen fan at `frequency`. Every source-dependent
  // input (source sound speed sample, Lloyd/semi-coherent source term, epsilon
  // inputs, source amplitude, `.sbp` pattern amplitude) is taken from
  // `simulation.sources()[sourceIndex]`, matching the F2CPP per-source loop.
  // The (sourceIndex, rayCache) pair must match: the cache must have been
  // traced from the same source.
  [[nodiscard]] static SingleFrequencyResult solveFrequencyFromSourceCache(
      const SimulationCase& simulation, double frequency,
      const RayPathCache& rayCache, std::size_t sourceIndex,
      double epsilonMultiplier, double loopRange,
      CartesianCervenySettings influenceSettings = {});

  // First-source convenience wrapper over
  // solveFrequencyFromSourceCache(simulation, frequency, rayCache, 0, ...).
  // Retained for the single-source legacy call sites.
  [[nodiscard]] static SingleFrequencyResult solveFrequencyFromCache(
      const SimulationCase& simulation, double frequency,
      const RayPathCache& rayCache, double epsilonMultiplier, double loopRange,
      CartesianCervenySettings influenceSettings = {});

  [[nodiscard]] static SingleFrequencyResult solve(
      const SimulationCase& simulation, double epsilonMultiplier,
      double loopRange, CartesianCervenySettings influenceSettings = {});

  // Traces every source's fan once (NSz trace passes) and returns the
  // per-source workspace sequence for `frequency` (first source in
  // `workspace`, the rest in `additionalSourceWorkspaces`).
  [[nodiscard]] static SingleFrequencyResult solveAtFrequency(
      const SimulationCase& simulation, double frequency,
      double epsilonMultiplier, double loopRange,
      CartesianCervenySettings influenceSettings = {});
};

}  // namespace rayreuse
