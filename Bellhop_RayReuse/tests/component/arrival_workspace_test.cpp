#include "rayreuse/field/arrival_workspace.hpp"

#include <complex>
#include <iostream>
#include <vector>

#include "rayreuse/error.hpp"
#include "rayreuse/model/simulation_case.hpp"
#include "support/test_harness.hpp"

namespace {
using rayreuse::ArrivalCandidate;
using rayreuse::ArrivalWorkspace;
using rayreuse::ReceiverGrid;
using rayreuse::ValidationError;
using rayreuse::test::Context;

void testCapacityAndGrouping(Context& context) {
  ArrivalWorkspace workspace(100.0, ReceiverGrid({50.0}, {100.0, 200.0}), 2U);
  const ArrivalCandidate first{1.0, 0.2, {1.0, 0.0}, 3.0, 4.0, 0, 1};
  workspace.addCandidate(100.0, first, 0U, 0U);
  ArrivalCandidate grouped = first;
  grouped.amplitude = 2.0;
  grouped.delaySeconds = {1.0 + 1.0e-8, 0.0};
  grouped.sourceDeclinationDegrees = 9.0;
  workspace.addCandidate(100.0, grouped, 0U, 0U);
  context.check(
      workspace.candidateCount() == 2U && workspace.mergeCount() == 1U,
      "arrival AddArr groups adjacent candidates");
  context.check(workspace.arrivalCountAt(0U, 0U) == 1U,
                "grouped candidates retain one stored arrival");
  workspace.addCandidate(
      100.0, ArrivalCandidate{0.5, 1.0, {2.0, 0.0}, 0.0, 0.0, 0, 0}, 0U, 0U);
  workspace.addCandidate(
      100.0, ArrivalCandidate{0.1, 2.0, {3.0, 0.0}, 0.0, 0.0, 0, 0}, 0U, 0U);
  context.check(workspace.arrivalCountAt(0U, 0U) == 2U,
                "arrival workspace enforces per-cell capacity");
  context.expectThrows<ValidationError>(
      [&workspace] { workspace.addCandidate(99.0, {}, 0U, 0U); },
      "arrival frequency mismatch is rejected");
}
}  // namespace

int main() {
  Context context;
  testCapacityAndGrouping(context);
  if (context.failureCount() != 0) {
    std::cerr << context.failureCount()
              << " arrival-workspace assertion(s) failed\n";
    return 1;
  }
  std::cout << "All Bellhop RayReuse arrival-workspace tests passed\n";
  return 0;
}
