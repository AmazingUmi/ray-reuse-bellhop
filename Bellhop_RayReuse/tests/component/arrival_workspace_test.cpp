#include "rayreuse/field/arrival_workspace.hpp"

#include <complex>
#include <cstring>
#include <iostream>
#include <limits>

#include "rayreuse/error.hpp"
#include "rayreuse/model/simulation_case.hpp"
#include "support/test_harness.hpp"

namespace {
using rayreuse::Arrival;
using rayreuse::ArrivalCandidate;
using rayreuse::ArrivalWorkspace;
using rayreuse::ReceiverGrid;
using rayreuse::ValidationError;
using rayreuse::test::Context;

ArrivalCandidate candidate(double amplitude, double phase, double delay,
                           double sourceAngle = 3.0,
                           double receiverAngle = 4.0) {
  return ArrivalCandidate{amplitude, phase, {delay, 0.0}, sourceAngle,
                          receiverAngle, 1, 2};
}

void testMergeAndCusp(Context& context) {
  ArrivalWorkspace merged(100.0, ReceiverGrid({50.0}, {100.0}), 3U);
  merged.addCandidate(100.0, candidate(1.0, 0.2, 1.0), 0U, 0U);
  merged.addCandidate(100.0,
                      candidate(2.0, 0.2, 1.0 + 1.0e-8, 9.0, 10.0), 0U,
                      0U);

  const Arrival record = merged.arrivalsAt(0U, 0U).front();
  context.check(merged.candidateCount() == 2U &&
                    merged.appendCount() == 1U &&
                    merged.mergeCount() == 1U &&
                    merged.arrivalCountAt(0U, 0U) == 1U,
                "AddArr merges only into the adjacent stored arrival");
  context.check(record.amplitude == 3.0F &&
                    record.sourceDeclinationDegrees == 7.0F &&
                    record.receiverDeclinationDegrees == 8.0F &&
                    record.phaseRadians == 0.2F && record.topBounceCount == 1 &&
                    record.bottomBounceCount == 2,
                "AddArr merge preserves float32 weights and legacy fields");

  ArrivalWorkspace cusp(100.0, ReceiverGrid({50.0}, {100.0}), 2U);
  cusp.addCandidate(100.0, candidate(1.0, 0.2, 1.0), 0U, 0U);
  const Arrival before = cusp.arrivalsAt(0U, 0U).front();
  cusp.addCandidate(100.0, candidate(-1.0, 0.2, 1.0), 0U, 0U);
  const Arrival after = cusp.arrivalsAt(0U, 0U).front();
  context.check(cusp.candidateCount() == 2U && cusp.mergeCount() == 0U &&
                    cusp.cuspGuardCount() == 1U &&
                    std::memcmp(&before, &after, sizeof(Arrival)) == 0,
                "cusp guard returns before merge accounting or record mutation");
}

void testNoMergeAndEncounterOrder(Context& context) {
  ArrivalWorkspace workspace(100.0, ReceiverGrid({50.0}, {100.0}), 4U);
  workspace.addCandidate(100.0, candidate(1.0, 0.0, 1.0), 0U, 0U);
  workspace.addCandidate(
      100.0, candidate(2.0, static_cast<double>(0.05F), 1.0), 0U, 0U);
  workspace.addCandidate(100.0, candidate(3.0, 0.0, 1.0 + 1.0e-8), 0U,
                         0U);

  const auto arrivals = workspace.arrivalsAt(0U, 0U);
  context.check(workspace.mergeCount() == 0U && arrivals.size() == 3U,
                "phase tolerance is strict and grouping compares only the last arrival");
  context.check(arrivals[0].amplitude == 1.0F &&
                    arrivals[1].amplitude == 2.0F &&
                    arrivals[2].amplitude == 3.0F,
                "non-grouped candidates retain encounter order without sorting");
}

void testCapacityReplacement(Context& context) {
  ArrivalWorkspace workspace(100.0, ReceiverGrid({50.0}, {100.0}), 3U);
  workspace.addCandidate(100.0, candidate(1.0, 0.0, 1.0), 0U, 0U);
  workspace.addCandidate(100.0, candidate(1.0, 1.0, 2.0), 0U, 0U);
  workspace.addCandidate(100.0, candidate(3.0, 2.0, 3.0), 0U, 0U);
  workspace.addCandidate(100.0, candidate(1.00000001, 3.0, 4.0), 0U, 0U);

  auto arrivals = workspace.arrivalsAt(0U, 0U);
  context.check(workspace.saturatedCellCount() == 1U &&
                    workspace.weakestReplacementCount() == 1U &&
                    arrivals[0].delaySeconds.real() == 4.0F &&
                    arrivals[1].delaySeconds.real() == 2.0F &&
                    arrivals[2].delaySeconds.real() == 3.0F,
                "capacity replacement uses candidate double strength and the first weakest slot");

  workspace.addCandidate(100.0, candidate(1.0, 4.0, 5.0), 0U, 0U);
  arrivals = workspace.arrivalsAt(0U, 0U);
  context.check(workspace.capacityDiscardCount() == 1U &&
                    arrivals[0].delaySeconds.real() == 4.0F,
                "equal-strength capacity candidate is discarded by strict comparison");
}

void testValidation(Context& context) {
  ArrivalWorkspace workspace(100.0, ReceiverGrid({50.0}, {100.0}), 2U);
  context.expectThrows<ValidationError>(
      [&workspace] { workspace.addCandidate(99.0, {}, 0U, 0U); },
      "arrival frequency mismatch is rejected");
  ArrivalCandidate invalid = candidate(1.0, 0.0, 1.0);
  invalid.amplitude = std::numeric_limits<double>::quiet_NaN();
  context.expectThrows<ValidationError>(
      [&workspace, &invalid] {
        workspace.addCandidate(100.0, invalid, 1U, 0U);
      },
      "candidate validation precedes receiver index validation");
}

}  // namespace

int main() {
  Context context;
  testMergeAndCusp(context);
  testNoMergeAndEncounterOrder(context);
  testCapacityReplacement(context);
  testValidation(context);
  if (context.failureCount() != 0) {
    std::cerr << context.failureCount()
              << " arrival-workspace assertion(s) failed\n";
    return 1;
  }
  std::cout << "All Bellhop RayReuse arrival-workspace tests passed\n";
  return 0;
}
