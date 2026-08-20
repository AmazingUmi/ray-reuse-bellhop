#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <numbers>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

#include "rayreuse/error.hpp"
#include "rayreuse/io/arrival_writer.hpp"
#include "rayreuse/io/eigenray_writer.hpp"
#include "rayreuse/model/environment.hpp"
#include "rayreuse/solver/arrival_solver.hpp"
#include "rayreuse/solver/eigenray_solver.hpp"
#include "support/test_harness.hpp"

namespace {
using namespace rayreuse;
using rayreuse::test::Context;

SimulationCase makeSimulation(
    BeamFamily beamFamily = BeamFamily::GeometricHat,
    SimulationRunMode runMode = SimulationRunMode::AsciiArrivals,
    bool directional = false) {
  Environment environment(
      SoundSpeedProfile({{0.0, 1500.0, 1000.0}, {100.0, 1500.0, 1000.0}}),
      BoundaryModel::vacuum(0.0), BoundaryModel::rigid(100.0));
  SourceBeamPattern sourceBeamPattern =
      directional ? SourceBeamPattern::directional(
                        {{-10.0, -20.0}, {0.0, 0.0}, {10.0, -20.0}})
                  : SourceBeamPattern::omnidirectional();
  return SimulationCase(std::move(environment), Source{50.0, 1.0},
                        ReceiverGrid({50.0}, {0.0, 10.0, 20.0}),
                        FrequencyGrid({50.0, 100.0}), LaunchFan{-0.1, 0.1, 3U},
                        IntegratorSettings{5.0, 25.0, 110.0, 1000U},
                        std::move(sourceBeamPattern), runMode, beamFamily);
}

SimulationCase makeZeroEigenraySimulation() {
  Environment environment(
      SoundSpeedProfile({{0.0, 1500.0, 1000.0}, {100.0, 1500.0, 1000.0}}),
      BoundaryModel::vacuum(0.0), BoundaryModel::rigid(100.0));
  return SimulationCase(std::move(environment), Source{50.0, 1.0},
                        ReceiverGrid({90.0}, {5.0, 10.0, 20.0}),
                        FrequencyGrid({50.0, 100.0}), LaunchFan{-0.1, 0.1, 3U},
                        IntegratorSettings{5.0, 25.0, 110.0, 1000U},
                        SourceBeamPattern::omnidirectional(),
                        SimulationRunMode::Eigenray, BeamFamily::GeometricHat);
}

SimulationCase makeGaussianEigenrayStandardCase() {
  Environment environment(
      SoundSpeedProfile({{0.0, 1500.0, 1000.0}, {100.0, 1500.0, 1000.0}}),
      BoundaryModel::vacuum(0.0), BoundaryModel::rigid(100.0));
  return SimulationCase(
      std::move(environment), Source{50.0, 1.0},
      ReceiverGrid({20.0, 50.0, 80.0}, {50.0, 100.0, 200.0, 400.0}),
      FrequencyGrid({1000.0}),
      LaunchFan{
          -60.0 * std::numbers::pi / 180.0,
          60.0 * std::numbers::pi / 180.0,
          300U,
          LaunchAngleDegreeBounds{-60.0, 60.0},
      },
      IntegratorSettings{1.0, 410.0, 101.0, 2'000'000U},
      SourceBeamPattern::omnidirectional(), SimulationRunMode::Eigenray,
      BeamFamily::GeometricGaussian);
}

void testArrivalModes(Context& context) {
  const SimulationCase simulation = makeSimulation(
      BeamFamily::GeometricHat, SimulationRunMode::AsciiArrivals);
  using ArrivalCells = std::vector<std::vector<Arrival>>;
  const auto snapshot = [](const ArrivalWorkspace& workspace) {
    ArrivalCells cells(workspace.receiverCellCount());
    for (std::size_t cell = 0U; cell < cells.size(); ++cell)
      cells[cell].assign(workspace.cellAt(cell).begin(),
                         workspace.cellAt(cell).end());
    return cells;
  };
  const auto sameArrival = [](const Arrival& left, const Arrival& right) {
    return left.amplitude == right.amplitude &&
           left.phaseRadians == right.phaseRadians &&
           left.delaySeconds == right.delaySeconds &&
           left.sourceDeclinationDegrees == right.sourceDeclinationDegrees &&
           left.receiverDeclinationDegrees ==
               right.receiverDeclinationDegrees &&
           left.topBounceCount == right.topBounceCount &&
           left.bottomBounceCount == right.bottomBounceCount;
  };
  std::vector<ArrivalCells> serial(2U), nonreuse(2U), parallel(2U);
  std::vector<std::uint64_t> serialFingerprint(2U), nonreuseFingerprint(2U),
      parallelFingerprint(2U);
  std::vector<bool> serialUnchanged(2U), nonreuseUnchanged(2U),
      parallelUnchanged(2U);
  const ArrivalSolverStatistics reuse = ArrivalSolver::solve(
      simulation,
      [&](std::size_t index, const RayPathCache& cache,
          const ArrivalWorkspace& workspace) {
        const std::uint64_t before = cache.contentFingerprint();
        serial[index] = snapshot(workspace);
        serialFingerprint[index] = cache.contentFingerprint();
        serialUnchanged[index] = before == serialFingerprint[index];
      },
      true);
  const ArrivalSolverStatistics nonreuseStats = ArrivalSolver::solveNonReuse(
      simulation,
      [&](std::size_t index, const RayPathCache& cache,
          const ArrivalWorkspace& workspace) {
        const std::uint64_t before = cache.contentFingerprint();
        nonreuse[index] = snapshot(workspace);
        nonreuseFingerprint[index] = cache.contentFingerprint();
        nonreuseUnchanged[index] = before == nonreuseFingerprint[index];
      },
      true);
  const ArrivalSolverStatistics parallelStats = ArrivalSolver::solveParallel(
      simulation,
      [&](std::size_t index, const RayPathCache& cache,
          const ArrivalWorkspace& workspace) {
        const std::uint64_t before = cache.contentFingerprint();
        parallel[index] = snapshot(workspace);
        parallelFingerprint[index] = cache.contentFingerprint();
        parallelUnchanged[index] = before == parallelFingerprint[index];
      },
      2U, true);
  context.check(reuse.frequencyCount == 2U &&
                    nonreuseStats.frequencyCount == 2U &&
                    parallelStats.frequencyCount == 2U,
                "arrival modes process every frequency");
  context.check(
      reuse.cacheFingerprintVerified &&
          nonreuseStats.cacheFingerprintVerified &&
          parallelStats.cacheFingerprintVerified &&
          reuse.cacheFingerprintBefore == reuse.cacheFingerprintAfter &&
          nonreuseStats.cacheFingerprintBefore ==
              nonreuseStats.cacheFingerprintAfter &&
          parallelStats.cacheFingerprintBefore ==
              parallelStats.cacheFingerprintAfter,
      "arrival solver verification spans trace-once projection and consume");
  for (std::size_t frequency = 0U; frequency < 2U; ++frequency) {
    bool recordsEqual =
        serial[frequency].size() == nonreuse[frequency].size() &&
        serial[frequency].size() == parallel[frequency].size();
    if (recordsEqual) {
      for (std::size_t cell = 0U;
           cell < serial[frequency].size() && recordsEqual; ++cell) {
        const auto& left = serial[frequency][cell];
        const auto& middle = nonreuse[frequency][cell];
        const auto& right = parallel[frequency][cell];
        if (left.size() != middle.size() || left.size() != right.size()) {
          recordsEqual = false;
          break;
        }
        for (std::size_t arrival = 0U; arrival < left.size(); ++arrival)
          if (!sameArrival(left[arrival], middle[arrival]) ||
              !sameArrival(left[arrival], right[arrival])) {
            recordsEqual = false;
            break;
          }
      }
    }
    context.check(recordsEqual, "arrival records agree across all modes");
    context.check(serialUnchanged[frequency] && nonreuseUnchanged[frequency] &&
                      parallelUnchanged[frequency],
                  "arrival projection does not mutate frozen cache");
    context.check(
        serialFingerprint[frequency] != 0U &&
            serialFingerprint[frequency] == nonreuseFingerprint[frequency] &&
            serialFingerprint[frequency] == parallelFingerprint[frequency],
        "arrival modes consume identical frozen geometry");
  }
  static_cast<void>(reuse);
}

void testDirectionalArrivalProjection(Context& context) {
  const SimulationCase omnidirectional = makeSimulation();
  const SimulationCase directional = makeSimulation(
      BeamFamily::GeometricHat, SimulationRunMode::AsciiArrivals, true);
  using FrequencyArrivals = std::vector<std::vector<Arrival>>;
  const auto capture = [](const SimulationCase& simulation) {
    FrequencyArrivals records(simulation.frequencies().size());
    static_cast<void>(ArrivalSolver::solve(
        simulation, [&](std::size_t frequencyIndex, const RayPathCache&,
                        const ArrivalWorkspace& workspace) {
          for (std::size_t cell = 0U; cell < workspace.receiverCellCount();
               ++cell) {
            const auto arrivals = workspace.cellAt(cell);
            records[frequencyIndex].insert(records[frequencyIndex].end(),
                                           arrivals.begin(), arrivals.end());
          }
        }));
    return records;
  };
  const FrequencyArrivals omniRecords = capture(omnidirectional);
  const FrequencyArrivals directionalRecords = capture(directional);
  bool observedDirectionalAmplitude = false;
  bool matchingProductShape = omniRecords.size() == directionalRecords.size();
  for (std::size_t frequency = 0U;
       frequency < omniRecords.size() && matchingProductShape; ++frequency) {
    if (omniRecords[frequency].size() != directionalRecords[frequency].size()) {
      matchingProductShape = false;
      break;
    }
    for (std::size_t arrival = 0U; arrival < omniRecords[frequency].size();
         ++arrival) {
      if (omniRecords[frequency][arrival].amplitude !=
          directionalRecords[frequency][arrival].amplitude) {
        observedDirectionalAmplitude = true;
      }
    }
  }
  context.check(matchingProductShape && observedDirectionalAmplitude,
                "directional SBP changes per-frequency Arrival amplitudes");
}

void testEigenrayModes(Context& context) {
  const SimulationCase simulation = makeSimulation(
      BeamFamily::GeometricGaussian, SimulationRunMode::Eigenray);
  using HitIdentity =
      std::tuple<std::size_t, std::size_t, std::size_t, std::size_t>;
  std::vector<std::vector<HitIdentity>> reuse(2U), nonreuse(2U), parallel(2U);
  std::vector<std::uint64_t> reuseFingerprint(2U), nonreuseFingerprint(2U),
      parallelFingerprint(2U);
  std::vector<bool> reuseUnchanged(2U), nonreuseUnchanged(2U),
      parallelUnchanged(2U);
  const EigenraySolverStatistics reuseStats = EigenraySolver::solve(
      simulation,
      [&](std::size_t f, const RayPathCache& cache, const auto& hits) {
        const std::uint64_t before = cache.contentFingerprint();
        for (const auto& [launch, hit] : hits)
          reuse[f].emplace_back(launch, hit.receiverRangeIndex,
                                hit.receiverDepthIndex, hit.prefixPointCount);
        reuseFingerprint[f] = cache.contentFingerprint();
        reuseUnchanged[f] = before == reuseFingerprint[f];
      },
      true);
  const EigenraySolverStatistics nonreuseStats = EigenraySolver::solveNonReuse(
      simulation,
      [&](std::size_t f, const RayPathCache& cache, const auto& hits) {
        const std::uint64_t before = cache.contentFingerprint();
        for (const auto& [launch, hit] : hits)
          nonreuse[f].emplace_back(launch, hit.receiverRangeIndex,
                                   hit.receiverDepthIndex,
                                   hit.prefixPointCount);
        nonreuseFingerprint[f] = cache.contentFingerprint();
        nonreuseUnchanged[f] = before == nonreuseFingerprint[f];
      },
      true);
  const EigenraySolverStatistics parallelStats = EigenraySolver::solveParallel(
      simulation,
      [&](std::size_t f, const RayPathCache& cache, const auto& hits) {
        const std::uint64_t before = cache.contentFingerprint();
        for (const auto& [launch, hit] : hits)
          parallel[f].emplace_back(launch, hit.receiverRangeIndex,
                                   hit.receiverDepthIndex,
                                   hit.prefixPointCount);
        parallelFingerprint[f] = cache.contentFingerprint();
        parallelUnchanged[f] = before == parallelFingerprint[f];
      },
      2U, true);
  context.check(reuseStats.frequencyCount == 2U &&
                    nonreuseStats.frequencyCount == 2U &&
                    parallelStats.frequencyCount == 2U,
                "eigenray modes process every frequency");
  context.check(
      reuseStats.cacheFingerprintVerified &&
          nonreuseStats.cacheFingerprintVerified &&
          parallelStats.cacheFingerprintVerified &&
          reuseStats.cacheFingerprintBefore ==
              reuseStats.cacheFingerprintAfter &&
          nonreuseStats.cacheFingerprintBefore ==
              nonreuseStats.cacheFingerprintAfter &&
          parallelStats.cacheFingerprintBefore ==
              parallelStats.cacheFingerprintAfter,
      "eigenray solver verification spans trace-once projection and consume");
  for (std::size_t frequency = 0U; frequency < 2U; ++frequency) {
    context.check(reuse[frequency] == nonreuse[frequency] &&
                      reuse[frequency] == parallel[frequency],
                  "eigenray hit identities and prefixes agree across modes");
    context.check(reuseUnchanged[frequency] && nonreuseUnchanged[frequency] &&
                      parallelUnchanged[frequency],
                  "eigenray traversal does not mutate frozen cache");
    context.check(
        reuseFingerprint[frequency] != 0U &&
            reuseFingerprint[frequency] == nonreuseFingerprint[frequency] &&
            reuseFingerprint[frequency] == parallelFingerprint[frequency],
        "eigenray modes consume identical frozen geometry");
  }
}

void testGaussianEigenraySegmentEnvelope(Context& context) {
  const SimulationCase simulation = makeGaussianEigenrayStandardCase();
  std::size_t hitCount = 0U;
  const EigenraySolverStatistics statistics = EigenraySolver::solve(
      simulation,
      [&](std::size_t frequencyIndex, const RayPathCache&, const auto& hits) {
        context.check(frequencyIndex == 0U,
                      "Gaussian eigenray standard-case frequency identity");
        hitCount += hits.size();
      });
  context.check(statistics.totalHitCount == 1418U && hitCount == 1418U,
                "existing eigenray_geometric_gaussian case retains the "
                "Origin/F2CPP segment-envelope hit count");
}

void testEmptyProducts(Context& context) {
  const SimulationCase asciiSimulation = makeSimulation();
  const SimulationCase binarySimulation = makeSimulation(
      BeamFamily::GeometricHat, SimulationRunMode::BinaryArrivals);
  const SimulationCase eigenraySimulation = makeZeroEigenraySimulation();
  const ArrivalWorkspace asciiWorkspace(
      asciiSimulation.frequencies().values().front(),
      asciiSimulation.receivers());
  const ArrivalWorkspace binaryWorkspace(
      binarySimulation.frequencies().values().front(),
      binarySimulation.receivers());
  const auto base = std::filesystem::temp_directory_path();
  const auto asciiPath = base / "rayreuse_b3_empty.arr";
  const auto binaryPath = base / "rayreuse_b3_empty_binary.arr";
  const auto eigenrayPath = base / "rayreuse_b3_empty.e";
  ArrivalWriter::write(asciiPath, "empty", asciiSimulation, asciiWorkspace,
                       ArrivalEncoding::Ascii);
  ArrivalWriter::write(binaryPath, "empty", binarySimulation, binaryWorkspace,
                       ArrivalEncoding::Binary);
  bool sawZeroHitFrequency = false;
  const EigenraySolverStatistics emptyStats = EigenraySolver::solve(
      eigenraySimulation, [&](std::size_t frequencyIndex,
                              const RayPathCache& cache, const auto& hits) {
        if (frequencyIndex != 1U) return;
        sawZeroHitFrequency = hits.empty();
        EigenrayWriter::write(eigenrayPath, "empty", eigenraySimulation, 100.0,
                              cache, hits);
      });
  static_cast<void>(emptyStats);
  std::ifstream ascii(asciiPath);
  std::ifstream binary(binaryPath, std::ios::binary);
  std::ifstream eigenray(eigenrayPath);
  context.check(static_cast<bool>(ascii) && static_cast<bool>(binary) &&
                    static_cast<bool>(eigenray),
                "empty Arrival/Eigenray products are writable");
  context.check(sawZeroHitFrequency,
                "Eigenray consumer receives an explicit zero-hit product");
  context.check(ascii.peek() != std::ifstream::traits_type::eof() &&
                    binary.peek() != std::ifstream::traits_type::eof() &&
                    eigenray.peek() != std::ifstream::traits_type::eof(),
                "empty Arrival/Eigenray products contain headers");
  std::string asciiTag;
  std::getline(ascii, asciiTag);
  std::string eigenrayTag;
  std::getline(eigenray, eigenrayTag);
  std::string eigenrayFrequency;
  std::getline(eigenray, eigenrayFrequency);
  context.check(asciiTag == "'2D'", "Arrival ASCII uses canonical 2D header");
  context.check(eigenrayTag == "'BELLHOP- empty'" && eigenrayFrequency == "100",
                "Eigenray writer emits the explicit product frequency");
  std::uint32_t binaryRecordLength = 0U;
  char binaryTag[4]{};
  binary.read(reinterpret_cast<char*>(&binaryRecordLength),
              sizeof(binaryRecordLength));
  binary.read(binaryTag, sizeof(binaryTag));
  context.check(
      binaryRecordLength == 4U && std::string(binaryTag, 4U) == "'2D'",
      "Arrival binary uses canonical 2D header record");
  std::filesystem::remove(asciiPath);
  std::filesystem::remove(binaryPath);
  std::filesystem::remove(eigenrayPath);
}

void testProductValidation(Context& context) {
  const SimulationCase asciiSimulation = makeSimulation();
  const SimulationCase binarySimulation = makeSimulation(
      BeamFamily::GeometricHat, SimulationRunMode::BinaryArrivals);
  const SimulationCase eigenraySimulation = makeSimulation(
      BeamFamily::GeometricGaussian, SimulationRunMode::Eigenray);
  const ArrivalWorkspace asciiWorkspace(
      asciiSimulation.frequencies().values().front(),
      asciiSimulation.receivers());
  const ArrivalWorkspace unknownFrequency(75.0, asciiSimulation.receivers());
  const auto rejectedPath =
      std::filesystem::temp_directory_path() / "rayreuse_b3_rejected.arr";
  context.expectThrows<ValidationError>(
      [&] {
        ArrivalWriter::write(rejectedPath, "wrong mode", asciiSimulation,
                             asciiWorkspace, ArrivalEncoding::Binary);
      },
      "Arrival writer rejects an encoding/run-mode mismatch");
  context.expectThrows<ValidationError>(
      [&] {
        ArrivalWriter::write(rejectedPath, "wrong frequency", asciiSimulation,
                             unknownFrequency, ArrivalEncoding::Ascii);
      },
      "Arrival writer rejects a frequency outside the simulation grid");
  context.expectThrows<ValidationError>(
      [&] {
        static_cast<void>(ArrivalSolver::solve(
            eigenraySimulation,
            [](std::size_t, const RayPathCache&, const ArrivalWorkspace&) {}));
      },
      "Arrival solver rejects a non-Arrival run mode");
  context.expectThrows<ValidationError>(
      [&] {
        static_cast<void>(EigenraySolver::solve(
            binarySimulation,
            [](std::size_t, const RayPathCache&, const auto&) {}));
      },
      "Eigenray solver rejects a non-Eigenray run mode");
  std::error_code ignored;
  std::filesystem::remove(rejectedPath, ignored);
}
}  // namespace

int main() {
  Context context;
  testArrivalModes(context);
  testDirectionalArrivalProjection(context);
  testEigenrayModes(context);
  testGaussianEigenraySegmentEnvelope(context);
  testEmptyProducts(context);
  testProductValidation(context);
  if (context.failureCount() != 0) {
    std::cerr << context.failureCount()
              << " arrival/eigenray assertion(s) failed\n";
    return 1;
  }
  std::cout << "All Bellhop RayReuse arrival/eigenray solver tests passed\n";
  return 0;
}
