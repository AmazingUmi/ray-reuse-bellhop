#include "rayreuse/field/broadband_arrival_workspace.hpp"
#include "rayreuse/field/arrival_workspace.hpp"

#include <array>
#include <cstring>
#include <iostream>
#include <limits>

#include "rayreuse/error.hpp"
#include "rayreuse/model/simulation_case.hpp"
#include "support/test_harness.hpp"

namespace {
using rayreuse::Arrival;
using rayreuse::ArrivalAccumulationStatistics;
using rayreuse::ArrivalCandidate;
using rayreuse::ArrivalWorkspace;
using rayreuse::BroadbandArrivalWorkspace;
using rayreuse::ReceiverGrid;
using rayreuse::ValidationError;
using rayreuse::test::Context;

Arrival taggedArrival(float tag) {
  return Arrival{tag, tag + 0.25F, {tag + 0.5F, tag + 0.75F}, tag + 1.0F,
                 tag + 1.25F, 1, 2};
}

void testLayoutAndFrequencyView(Context& context) {
  const std::array frequencies{50.0, 100.0, 200.0};
  BroadbandArrivalWorkspace workspace(
      frequencies, ReceiverGrid({10.0, 20.0}, {100.0, 200.0}), 4U);

  context.check(workspace.rangeCount() == 2U &&
                    workspace.depthCount() == 2U &&
                    workspace.frequencyCount() == 3U &&
                    workspace.receiverCellCount() == 4U &&
                    workspace.laneCount() == 12U,
                "broadband Arrival dimensions form one [R][D][F] lane set");
  context.check(workspace.capacity().receiverCellCount == 4U &&
                    workspace.capacity().arrivalsPerCell == 4U &&
                    workspace.capacity().logicalSlotCount == 16U,
                "all frequency lanes share the legacy Origin capacity plan");

  workspace.laneAt(0U, 0U, 1U).push_back(taggedArrival(1.0F));
  workspace.laneAt(1U, 0U, 1U).push_back(taggedArrival(2.0F));
  workspace.laneAt(0U, 1U, 1U).push_back(taggedArrival(3.0F));
  workspace.laneAt(1U, 1U, 1U).push_back(taggedArrival(4.0F));
  workspace.laneAt(1U, 1U, 1U).push_back(taggedArrival(5.0F));

  const auto view = workspace.frequencyView(1U);
  context.check(view.frequency() == 100.0 && view.depthCount() == 2U &&
                    view.rangeCount() == 2U &&
                    view.receiverCellCount() == 4U &&
                    view.capacity().arrivalsPerCell == 4U,
                "frequency view exposes legacy-compatible metadata");
  context.check(view.cellAt(0U).front().amplitude == 1.0F &&
                    view.cellAt(1U).front().amplitude == 2.0F &&
                    view.cellAt(2U).front().amplitude == 3.0F &&
                    view.cellAt(3U).front().amplitude == 4.0F &&
                    view.arrivalCountAt(1U, 1U) == 2U,
                "frequency view maps legacy depth-major cell traversal onto [R][D][F]");
  context.check(view.arrivalsAt(1U, 1U).data() ==
                    workspace.laneAt(1U, 1U, 1U).data(),
                "frequency view aliases the fused lane without Arrival copies");

  const auto storage = workspace.storageStatistics();
  context.check(storage.laneCount == 12U &&
                    storage.nonEmptyLaneCount == 4U &&
                    storage.storedArrivalCount == 5U &&
                    storage.allocatedArrivalSlots >= 5U &&
                    storage.logicalArrivalSlots == 48U &&
                    storage.laneHeaderBytes >=
                        12U * sizeof(std::vector<Arrival>) &&
                    storage.allocatedArrivalBytes >= 5U * sizeof(Arrival) &&
                    storage.memoryFootprintBytes >=
                        storage.laneHeaderBytes + storage.allocatedArrivalBytes,
                "source-local storage accounting covers lanes and payloads");
}

void testValidation(Context& context) {
  const ReceiverGrid receivers({10.0}, {100.0});
  const std::array validFrequencies{50.0, 100.0};
  BroadbandArrivalWorkspace workspace(validFrequencies, receivers);

  context.expectThrows<std::out_of_range>(
      [&workspace] { static_cast<void>(workspace.frequencyView(2U)); },
      "out-of-range frequency view is rejected");
  context.expectThrows<std::out_of_range>(
      [&workspace] { static_cast<void>(workspace.laneAt(1U, 0U, 0U)); },
      "out-of-range fused lane is rejected");
  context.expectThrows<std::out_of_range>(
      [&workspace] { static_cast<void>(workspace.frequencyView(0U).cellAt(1U)); },
      "out-of-range legacy cell is rejected");

  const std::span<const double> empty;
  context.expectThrows<ValidationError>(
      [&receivers, empty] {
        static_cast<void>(BroadbandArrivalWorkspace(empty, receivers));
      },
      "empty frequency dimension is rejected");
  const std::array invalidFrequency{
      std::numeric_limits<double>::infinity()};
  context.expectThrows<ValidationError>(
      [&receivers, &invalidFrequency] {
        static_cast<void>(
            BroadbandArrivalWorkspace(invalidFrequency, receivers));
      },
      "non-finite frequency is rejected");
}

void testSharedAddArrSemantics(Context& context) {
  const ReceiverGrid receivers({10.0}, {100.0});
  const std::array frequencies{100.0};
  BroadbandArrivalWorkspace fused(frequencies, receivers, 2U);
  ArrivalWorkspace legacy(100.0, receivers, 2U);
  ArrivalAccumulationStatistics fusedStatistics;
  const std::array candidates{
      ArrivalCandidate{1.0, 0.2, {1.0, 0.0}, 3.0, 4.0, 1, 2},
      ArrivalCandidate{2.0, 0.2, {1.0 + 1.0e-8, 0.0}, 9.0, 10.0, 3, 4},
      ArrivalCandidate{0.5, 1.0, {2.0, 0.0}, 5.0, 6.0, 5, 6},
      ArrivalCandidate{4.0, 2.0, {3.0, 0.0}, 7.0, 8.0, 7, 8}};

  for (const ArrivalCandidate& value : candidates) {
    legacy.addCandidate(100.0, value, 0U, 0U);
    fused.addCandidate(0U, value, 0U, 0U, fusedStatistics);
  }

  const auto legacyLane = legacy.arrivalsAt(0U, 0U);
  const auto fusedLane = fused.frequencyView(0U).arrivalsAt(0U, 0U);
  context.check(legacyLane.size() == fusedLane.size() &&
                    std::memcmp(legacyLane.data(), fusedLane.data(),
                                legacyLane.size_bytes()) == 0,
                "legacy and fused lanes share byte-identical AddArr results");
  context.check(
      fusedStatistics.candidateCount == legacy.candidateCount() &&
          fusedStatistics.appendCount == legacy.appendCount() &&
          fusedStatistics.mergeCount == legacy.mergeCount() &&
          fusedStatistics.cuspGuardCount == legacy.cuspGuardCount() &&
          fusedStatistics.weakestReplacementCount ==
              legacy.weakestReplacementCount() &&
          fusedStatistics.capacityDiscardCount ==
              legacy.capacityDiscardCount() &&
          fusedStatistics.saturatedCellCount == legacy.saturatedCellCount(),
      "legacy aggregation and fused local statistics are identical");
}

}  // namespace

int main() {
  Context context;
  testLayoutAndFrequencyView(context);
  testValidation(context);
  testSharedAddArrSemantics(context);
  if (context.failureCount() != 0) {
    std::cerr << context.failureCount()
              << " broadband-arrival-workspace assertion(s) failed\n";
    return 1;
  }
  std::cout << "All Bellhop RayReuse broadband-arrival-workspace tests passed\n";
  return 0;
}
