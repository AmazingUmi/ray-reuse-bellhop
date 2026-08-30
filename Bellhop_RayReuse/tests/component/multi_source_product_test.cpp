// FP-2F F04 component tests: per-(frequency, source) product state across the
// three execution modes (nonreuse / reuse / parallel) and per-source
// numerical correctness against equivalent single-source runs.

#include <algorithm>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <numbers>
#include <optional>
#include <tuple>
#include <utility>
#include <vector>

#include "rayreuse/model/simulation_case.hpp"
#include "rayreuse/solver/arrival_solver.hpp"
#include "rayreuse/solver/broadband_nonreuse_solver.hpp"
#include "rayreuse/solver/eigenray_solver.hpp"
#include "rayreuse/solver/parallel_ray_reuse_solver.hpp"
#include "rayreuse/solver/ray_trace_product.hpp"
#include "rayreuse/solver/serial_ray_reuse_solver.hpp"
#include "support/test_harness.hpp"

namespace {

using rayreuse::ArrivalSolver;
using rayreuse::ArrivalSolverStatistics;
using rayreuse::BeamFamily;
using rayreuse::BoundaryCurvatureMode;
using rayreuse::BoundaryModel;
using rayreuse::BroadbandNonReuseResult;
using rayreuse::BroadbandNonReuseSolver;
using rayreuse::CervenyCoordinateSystem;
using rayreuse::EigenrayHit;
using rayreuse::EigenraySolver;
using rayreuse::EigenraySolverStatistics;
using rayreuse::EigenraySourceHits;
using rayreuse::Environment;
using rayreuse::FieldComponent;
using rayreuse::FrequencyGrid;
using rayreuse::FrequencyWorkspace;
using rayreuse::IntegratorSettings;
using rayreuse::LaunchFan;
using rayreuse::ParallelRayReuseSettings;
using rayreuse::ParallelRayReuseSolver;
using rayreuse::ParallelRayReuseStatistics;
using rayreuse::RayPathCache;
using rayreuse::ReceiverGrid;
using rayreuse::SerialRayReuseResult;
using rayreuse::SerialRayReuseSolver;
using rayreuse::SerialRayReuseStatistics;
using rayreuse::SimulationCase;
using rayreuse::SimulationRunMode;
using rayreuse::SingleFrequencyResult;
using rayreuse::SingleFrequencySolver;
using rayreuse::SoundSpeedPoint;
using rayreuse::SoundSpeedProfile;
using rayreuse::Source;
using rayreuse::SourceBeamPattern;
using rayreuse::traceRayProduct;
using rayreuse::traceRayProducts;
using rayreuse::test::Context;

// Depth-varying SSP so each source depth yields a distinct source sound
// speed (exercising the per-source Lloyd/epsilon/scaling inputs).
Environment makeEnvironment() {
  return Environment(
      SoundSpeedProfile(
          {SoundSpeedPoint{
               .depth = 0.0, .soundSpeed = 1500.0, .density = 1000.0},
           SoundSpeedPoint{
               .depth = 100.0, .soundSpeed = 1510.0, .density = 1000.0}}),
      BoundaryModel::vacuum(0.0), BoundaryModel::rigid(100.0));
}

SourceBeamPattern makePattern(bool directional) {
  return directional ? SourceBeamPattern::directional(
                           {{-10.0, -20.0}, {0.0, 0.0}, {10.0, -20.0}})
                     : SourceBeamPattern::omnidirectional();
}

SimulationCase makeCase(std::vector<Source> sources, SimulationRunMode runMode,
                        BeamFamily beamFamily, std::vector<double> frequencies,
                        bool directional,
                        std::size_t explicitLaunchAngleCount = 300U) {
  return SimulationCase(
      makeEnvironment(), std::move(sources),
      ReceiverGrid({25.0, 50.0, 75.0}, {10.0, 55.0, 100.0}),
      FrequencyGrid(std::move(frequencies)),
      LaunchFan{.minimumAngle = -2.0 * std::numbers::pi / 180.0,
                .maximumAngle = 2.0 * std::numbers::pi / 180.0,
                .explicitLaunchAngleCount = explicitLaunchAngleCount},
      IntegratorSettings{.stepLength = 10.0,
                         .rangeLimit = 110.0,
                         .depthLimit = 110.0,
                         .maximumRayPoints = 1000U},
      makePattern(directional), runMode, beamFamily, FieldComponent::Pressure,
      BoundaryCurvatureMode::Standard);
}

// Dual-source fixture given out of depth order; the model sorts ascending.
std::vector<Source> dualSources() {
  return {Source{.depth = 70.0, .amplitude = 1.0},
          Source{.depth = 30.0, .amplitude = 1.0}};
}

bool workspaceEqual(const FrequencyWorkspace& left,
                    const FrequencyWorkspace& right) {
  return left.frequency() == right.frequency() &&
         left.depthCount() == right.depthCount() &&
         left.rangeCount() == right.rangeCount() &&
         std::equal(left.pressure().begin(), left.pressure().end(),
                    right.pressure().begin(), right.pressure().end());
}

bool sameArrival(const rayreuse::Arrival& left,
                 const rayreuse::Arrival& right) {
  return left.amplitude == right.amplitude &&
         left.phaseRadians == right.phaseRadians &&
         left.delaySeconds == right.delaySeconds &&
         left.sourceDeclinationDegrees == right.sourceDeclinationDegrees &&
         left.receiverDeclinationDegrees == right.receiverDeclinationDegrees &&
         left.topBounceCount == right.topBounceCount &&
         left.bottomBounceCount == right.bottomBounceCount;
}

using ArrivalCells = std::vector<std::vector<rayreuse::Arrival>>;

ArrivalCells snapshotArrivals(const rayreuse::ArrivalWorkspace& workspace) {
  ArrivalCells cells(workspace.receiverCellCount());
  for (std::size_t cell = 0U; cell < cells.size(); ++cell) {
    cells[cell].assign(workspace.cellAt(cell).begin(),
                       workspace.cellAt(cell).end());
  }
  return cells;
}

using HitIdentity =
    std::tuple<std::size_t, std::size_t, std::size_t, std::size_t>;

std::vector<HitIdentity> snapshotHits(const EigenraySourceHits& hits) {
  std::vector<HitIdentity> identities;
  identities.reserve(hits.size());
  for (const auto& [launch, hit] : hits) {
    identities.emplace_back(launch, hit.receiverRangeIndex,
                            hit.receiverDepthIndex, hit.prefixPointCount);
  }
  return identities;
}

struct ParallelRun {
  std::vector<std::optional<std::vector<FrequencyWorkspace>>> workspaces;
  ParallelRayReuseStatistics statistics;
};

ParallelRun runParallel(const SimulationCase& simulation) {
  ParallelRun run{
      .workspaces = std::vector<std::optional<std::vector<FrequencyWorkspace>>>(
          simulation.frequencies().size()),
      .statistics = {}};
  run.statistics = ParallelRayReuseSolver::solveStreaming(
      simulation, 1.0, 50.0,
      [&run](std::size_t frequencyIndex,
             std::vector<FrequencyWorkspace>&& sourceWorkspaces,
             const rayreuse::SingleFrequencyTimings&) {
        run.workspaces.at(frequencyIndex).emplace(std::move(sourceWorkspaces));
      },
      ParallelRayReuseSettings{.workerCount = 2U,
                               .outputQueueCapacity = 1U,
                               .memoryBudgetBytes = 0U},
      {}, true);
  return run;
}

// TL: dual-source per-(frequency, source) agreement across all three
// execution modes, frozen trace-pass semantics, per-source fingerprint
// stability, and per-source numerics against equivalent single-source runs.
void testDualSourceTlThreeModes(Context& context) {
  const SimulationCase dual =
      makeCase(dualSources(), SimulationRunMode::SemiCoherent,
               BeamFamily::CervenyGaussian, {50.0, 100.0}, true);
  const std::size_t sourceCount = dual.sourceCount();
  const std::size_t fanCount = dual.launchFanPlan().launchAngleCount;
  context.check(sourceCount == 2U && dual.sources()[0U].depth == 30.0 &&
                    dual.sources()[1U].depth == 70.0,
                "dual-source TL fixture is depth sorted");

  const BroadbandNonReuseResult nonReuse =
      BroadbandNonReuseSolver::solve(dual, 1.0, 50.0);
  const SerialRayReuseResult reuse =
      SerialRayReuseSolver::solve(dual, 1.0, 50.0, {}, true);
  const ParallelRun parallel = runParallel(dual);

  context.check(nonReuse.statistics.tracePassCount == 4U &&
                    reuse.statistics.tracePassCount == 2U &&
                    parallel.statistics.tracePassCount == 2U,
                "dual-source two-frequency trace passes follow 4/2/2");
  context.check(reuse.statistics.rayCount == 2U * fanCount &&
                    parallel.statistics.rayCount == 2U * fanCount &&
                    nonReuse.statistics.totalRayCount == 4U * fanCount,
                "reuse ray counts cover both sources; non-reuse covers both "
                "frequencies and both sources");
  context.check(
      reuse.statistics.sourceCacheFingerprintsBefore.size() == 2U &&
          reuse.statistics.sourceCacheFingerprintsAfter ==
              reuse.statistics.sourceCacheFingerprintsBefore &&
          parallel.statistics.sourceCacheFingerprintsBefore.size() == 2U &&
          parallel.statistics.sourceCacheFingerprintsAfter ==
              parallel.statistics.sourceCacheFingerprintsBefore &&
          reuse.statistics.sourceCacheFingerprintsBefore[0U] !=
              reuse.statistics.sourceCacheFingerprintsBefore[1U],
      "reuse modes verify per-source fingerprints before == after");
  context.check(
      parallel.statistics.estimatedWorkspaceBytes ==
          2U * 3U * 3U * (sizeof(std::complex<double>) + sizeof(double)),
      "parallel workspace estimate covers the per-source intensity "
      "workspace sequence");

  bool allModesAgree = true;
  for (std::size_t frequencyIndex = 0U; frequencyIndex < 2U; ++frequencyIndex) {
    const SingleFrequencyResult& nonReuseResult =
        nonReuse.frequencyResults[frequencyIndex];
    const std::vector<FrequencyWorkspace>& serialWorkspaces =
        reuse.frequencyResults[frequencyIndex].workspaces;
    allModesAgree = allModesAgree && nonReuseResult.sourceCount() == 2U &&
                    serialWorkspaces.size() == 2U &&
                    parallel.workspaces[frequencyIndex].has_value() &&
                    parallel.workspaces[frequencyIndex]->size() == 2U;
    if (!allModesAgree) {
      break;
    }
    for (std::size_t sourceIndex = 0U; sourceIndex < sourceCount;
         ++sourceIndex) {
      allModesAgree =
          allModesAgree &&
          workspaceEqual(nonReuseResult.sourceWorkspace(sourceIndex),
                         serialWorkspaces[sourceIndex]) &&
          workspaceEqual((*parallel.workspaces[frequencyIndex])[sourceIndex],
                         serialWorkspaces[sourceIndex]);
    }
  }
  context.check(allModesAgree,
                "dual-source TL per-(frequency, source) workspaces agree "
                "bitwise across non-reuse, reuse, and parallel");

  // Per-source numerics: each source's field equals the field of an
  // otherwise identical single-source run at the same depth. The fan plans
  // must coincide for this comparison to be meaningful.
  for (std::size_t sourceIndex = 0U; sourceIndex < sourceCount; ++sourceIndex) {
    const double sourceDepth = dual.sources()[sourceIndex].depth;
    const SimulationCase single =
        makeCase({Source{.depth = sourceDepth, .amplitude = 1.0}},
                 SimulationRunMode::SemiCoherent, BeamFamily::CervenyGaussian,
                 {50.0, 100.0}, true);
    context.check(single.launchFanPlan().launchAngles ==
                          dual.launchFanPlan().launchAngles &&
                      single.launchFanPlan().launchAngleStep ==
                          dual.launchFanPlan().launchAngleStep,
                  "single-source reference plans the same launch fan");
    const SerialRayReuseResult singleReuse =
        SerialRayReuseSolver::solve(single, 1.0, 50.0, {}, true);
    bool sourceMatchesSingle = true;
    for (std::size_t frequencyIndex = 0U; frequencyIndex < 2U;
         ++frequencyIndex) {
      sourceMatchesSingle =
          sourceMatchesSingle &&
          workspaceEqual(
              reuse.frequencyResults[frequencyIndex].workspaces[sourceIndex],
              singleReuse.frequencyResults[frequencyIndex].workspaces.front());
    }
    context.check(sourceMatchesSingle,
                  "per-source TL equals a single-source run at the same "
                  "depth (Lloyd/epsilon/pattern from the current source)");
  }
}

// A/a: dual-source per-(frequency, source) arrivals agree across the three
// modes and match single-source reference runs per depth.
void testDualSourceArrivalThreeModes(Context& context) {
  const SimulationCase dual =
      makeCase(dualSources(), SimulationRunMode::AsciiArrivals,
               BeamFamily::GeometricHat, {50.0, 100.0}, true);
  const std::size_t fanCount = dual.launchFanPlan().launchAngleCount;
  using ModeCells = std::vector<std::vector<ArrivalCells>>;
  const auto capture = [](ModeCells& cells,
                          std::vector<std::vector<std::uint64_t>>& fingerprints,
                          std::size_t expectedSources) {
    return
        [&cells, &fingerprints, expectedSources](
            std::size_t frequencyIndex, const std::vector<RayPathCache>& caches,
            const std::vector<rayreuse::ArrivalWorkspace>& workspaces) {
          for (std::size_t sourceIndex = 0U; sourceIndex < expectedSources;
               ++sourceIndex) {
            cells[frequencyIndex].push_back(
                snapshotArrivals(workspaces[sourceIndex]));
            fingerprints[frequencyIndex].push_back(
                caches[sourceIndex].contentFingerprint());
          }
        };
  };

  ModeCells serial(2U), nonreuse(2U), parallel(2U);
  std::vector<std::vector<std::uint64_t>> serialFingerprints(2U),
      nonreuseFingerprints(2U), parallelFingerprints(2U);
  const ArrivalSolverStatistics serialStats =
      ArrivalSolver::solve(dual, capture(serial, serialFingerprints, 2U), true);
  const ArrivalSolverStatistics nonreuseStats = ArrivalSolver::solveNonReuse(
      dual, capture(nonreuse, nonreuseFingerprints, 2U), true);
  const ArrivalSolverStatistics parallelStats = ArrivalSolver::solveParallel(
      dual, capture(parallel, parallelFingerprints, 2U), 2U, true);

  context.check(serialStats.frequencyCount == 2U &&
                    nonreuseStats.frequencyCount == 2U &&
                    parallelStats.frequencyCount == 2U,
                "dual-source arrival modes process every frequency");
  context.check(serialStats.rayCount == 2U * fanCount &&
                    parallelStats.rayCount == 2U * fanCount &&
                    nonreuseStats.rayCount == 4U * fanCount,
                "dual-source arrival ray counts cover every source (and every "
                "frequency for non-reuse)");
  context.check(
      serialStats.sourceCacheFingerprintsBefore.size() == 2U &&
          serialStats.sourceCacheFingerprintsAfter ==
              serialStats.sourceCacheFingerprintsBefore &&
          parallelStats.sourceCacheFingerprintsBefore.size() == 2U &&
          parallelStats.sourceCacheFingerprintsAfter ==
              parallelStats.sourceCacheFingerprintsBefore,
      "arrival reuse modes verify per-source fingerprints before == after");

  bool allModesAgree = true;
  for (std::size_t frequencyIndex = 0U; frequencyIndex < 2U; ++frequencyIndex) {
    allModesAgree = allModesAgree && serial[frequencyIndex].size() == 2U &&
                    nonreuse[frequencyIndex].size() == 2U &&
                    parallel[frequencyIndex].size() == 2U &&
                    serialFingerprints[frequencyIndex] ==
                        nonreuseFingerprints[frequencyIndex] &&
                    serialFingerprints[frequencyIndex] ==
                        parallelFingerprints[frequencyIndex];
    for (std::size_t sourceIndex = 0U; sourceIndex < 2U && allModesAgree;
         ++sourceIndex) {
      const ArrivalCells& reference = serial[frequencyIndex][sourceIndex];
      const ArrivalCells& otherModes1 = nonreuse[frequencyIndex][sourceIndex];
      const ArrivalCells& otherModes2 = parallel[frequencyIndex][sourceIndex];
      if (reference.size() != otherModes1.size() ||
          reference.size() != otherModes2.size()) {
        allModesAgree = false;
        break;
      }
      for (std::size_t cell = 0U; cell < reference.size(); ++cell) {
        if (reference[cell].size() != otherModes1[cell].size() ||
            reference[cell].size() != otherModes2[cell].size()) {
          allModesAgree = false;
          break;
        }
        for (std::size_t arrival = 0U; arrival < reference[cell].size();
             ++arrival) {
          if (!sameArrival(reference[cell][arrival],
                           otherModes1[cell][arrival]) ||
              !sameArrival(reference[cell][arrival],
                           otherModes2[cell][arrival])) {
            allModesAgree = false;
            break;
          }
        }
        if (!allModesAgree) {
          break;
        }
      }
    }
  }
  context.check(allModesAgree,
                "dual-source arrivals agree per (frequency, source) across "
                "all modes with identical frozen geometry");

  for (std::size_t sourceIndex = 0U; sourceIndex < 2U; ++sourceIndex) {
    const double sourceDepth = dual.sources()[sourceIndex].depth;
    const SimulationCase single =
        makeCase({Source{.depth = sourceDepth, .amplitude = 1.0}},
                 SimulationRunMode::AsciiArrivals, BeamFamily::GeometricHat,
                 {50.0, 100.0}, true);
    ModeCells singleCells(2U);
    std::vector<std::vector<std::uint64_t>> singleFingerprints(2U);
    static_cast<void>(ArrivalSolver::solve(
        single, capture(singleCells, singleFingerprints, 1U), true));
    bool sourceMatchesSingle = true;
    for (std::size_t frequencyIndex = 0U; frequencyIndex < 2U;
         ++frequencyIndex) {
      const ArrivalCells& dualCells = serial[frequencyIndex][sourceIndex];
      const ArrivalCells& reference = singleCells[frequencyIndex].front();
      if (dualCells.size() != reference.size()) {
        sourceMatchesSingle = false;
        break;
      }
      for (std::size_t cell = 0U; cell < reference.size(); ++cell) {
        if (dualCells[cell].size() != reference[cell].size()) {
          sourceMatchesSingle = false;
          break;
        }
        for (std::size_t arrival = 0U; arrival < reference[cell].size();
             ++arrival) {
          if (!sameArrival(dualCells[cell][arrival],
                           reference[cell][arrival])) {
            sourceMatchesSingle = false;
            break;
          }
        }
        if (!sourceMatchesSingle) {
          break;
        }
      }
    }
    context.check(sourceMatchesSingle,
                  "per-source arrivals equal a single-source run at the same "
                  "depth (pattern amplitude from the current source)");
  }
}

// E: dual-source per-(frequency, source) eigenray hits agree across the
// three modes and match single-source reference runs per depth.
void testDualSourceEigenrayThreeModes(Context& context) {
  const SimulationCase dual =
      makeCase(dualSources(), SimulationRunMode::Eigenray,
               BeamFamily::GeometricGaussian, {50.0, 100.0}, false);
  const std::size_t fanCount = dual.launchFanPlan().launchAngleCount;
  using ModeHits = std::vector<std::vector<std::vector<HitIdentity>>>;
  const auto capture = [](ModeHits& hits) {
    return [&hits](std::size_t frequencyIndex, const std::vector<RayPathCache>&,
                   const std::vector<EigenraySourceHits>& sourceHits) {
      for (const EigenraySourceHits& source : sourceHits) {
        hits[frequencyIndex].push_back(snapshotHits(source));
      }
    };
  };

  ModeHits serial(2U), nonreuse(2U), parallel(2U);
  const EigenraySolverStatistics serialStats =
      EigenraySolver::solve(dual, capture(serial), true);
  const EigenraySolverStatistics nonreuseStats =
      EigenraySolver::solveNonReuse(dual, capture(nonreuse), true);
  const EigenraySolverStatistics parallelStats =
      EigenraySolver::solveParallel(dual, capture(parallel), 2U, true);

  context.check(serialStats.frequencyCount == 2U &&
                    nonreuseStats.frequencyCount == 2U &&
                    parallelStats.frequencyCount == 2U,
                "dual-source eigenray modes process every frequency");
  context.check(serialStats.rayCount == 2U * fanCount &&
                    parallelStats.rayCount == 2U * fanCount &&
                    nonreuseStats.rayCount == 4U * fanCount,
                "dual-source eigenray ray counts cover every source");
  context.check(
      serialStats.sourceCacheFingerprintsBefore.size() == 2U &&
          serialStats.sourceCacheFingerprintsAfter ==
              serialStats.sourceCacheFingerprintsBefore &&
          parallelStats.sourceCacheFingerprintsBefore.size() == 2U &&
          parallelStats.sourceCacheFingerprintsAfter ==
              parallelStats.sourceCacheFingerprintsBefore,
      "eigenray reuse modes verify per-source fingerprints before == after");

  bool allModesAgree = true;
  for (std::size_t frequencyIndex = 0U; frequencyIndex < 2U; ++frequencyIndex) {
    allModesAgree = allModesAgree && serial[frequencyIndex].size() == 2U &&
                    nonreuse[frequencyIndex].size() == 2U &&
                    parallel[frequencyIndex].size() == 2U &&
                    serial[frequencyIndex] == nonreuse[frequencyIndex] &&
                    serial[frequencyIndex] == parallel[frequencyIndex];
  }
  context.check(allModesAgree,
                "dual-source eigenray hits agree per (frequency, source) "
                "across all modes");

  for (std::size_t sourceIndex = 0U; sourceIndex < 2U; ++sourceIndex) {
    const double sourceDepth = dual.sources()[sourceIndex].depth;
    const SimulationCase single =
        makeCase({Source{.depth = sourceDepth, .amplitude = 1.0}},
                 SimulationRunMode::Eigenray, BeamFamily::GeometricGaussian,
                 {50.0, 100.0}, false);
    ModeHits singleHits(2U);
    static_cast<void>(EigenraySolver::solve(single, capture(singleHits), true));
    bool sourceMatchesSingle = true;
    for (std::size_t frequencyIndex = 0U; frequencyIndex < 2U;
         ++frequencyIndex) {
      sourceMatchesSingle =
          sourceMatchesSingle && serial[frequencyIndex][sourceIndex] ==
                                     singleHits[frequencyIndex].front();
    }
    context.check(sourceMatchesSingle,
                  "per-source eigenray hits equal a single-source run at the "
                  "same depth");
  }
}

// R: per-source trace products, equivalence with single-source R runs, and
// the NSz == 1 legacy entry point.
void testDualSourceRayProduct(Context& context) {
  const SimulationCase dual =
      makeCase(dualSources(), SimulationRunMode::RayTrace,
               BeamFamily::GeometricHat, {50.0}, false, 3U);
  const std::vector<RayPathCache> caches = traceRayProducts(dual);
  context.check(
      caches.size() == 2U && caches[0U].frozen() && caches[1U].frozen(),
      "R product traces one frozen fan per source");
  context.check(caches[0U].at(0U).points.front().position.depth == 30.0 &&
                    caches[1U].at(0U).points.front().position.depth == 70.0,
                "R per-source caches start at their own source depth");
  context.check(caches[0U].size() == dual.launchFanPlan().launchAngleCount &&
                    caches[1U].size() == dual.launchFanPlan().launchAngleCount,
                "R per-source caches carry the full shared fan");

  for (std::size_t sourceIndex = 0U; sourceIndex < 2U; ++sourceIndex) {
    const SimulationCase single = makeCase(
        {Source{.depth = dual.sources()[sourceIndex].depth, .amplitude = 1.0}},
        SimulationRunMode::RayTrace, BeamFamily::GeometricHat, {50.0}, false,
        3U);
    const RayPathCache singleCache = traceRayProduct(single);
    context.check(caches[sourceIndex].contentFingerprint() ==
                      singleCache.contentFingerprint(),
                  "R per-source cache matches a single-source R trace");
  }

  const SimulationCase single = makeCase(
      {Source{.depth = 30.0, .amplitude = 1.0}}, SimulationRunMode::RayTrace,
      BeamFamily::GeometricHat, {50.0}, false, 3U);
  context.check(traceRayProduct(single).contentFingerprint() ==
                    traceRayProducts(single).front().contentFingerprint(),
                "NSz == 1 legacy R entry matches the per-source product");
}

}  // namespace

int main() {
  Context context;
  testDualSourceTlThreeModes(context);
  testDualSourceArrivalThreeModes(context);
  testDualSourceEigenrayThreeModes(context);
  testDualSourceRayProduct(context);

  if (context.failureCount() != 0) {
    std::cerr << context.failureCount()
              << " multi-source product assertion(s) failed\n";
    return 1;
  }
  std::cout << "All Bellhop RayReuse multi-source product tests passed\n";
  return 0;
}
