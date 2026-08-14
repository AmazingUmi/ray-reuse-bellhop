#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <numeric>
#include <optional>
#include <type_traits>
#include <vector>

#include "bellhop/error.hpp"
#include "bellhop/field/arrival.hpp"
#include "bellhop/field/arrival_workspace.hpp"
#include "bellhop/model/simulation_case.hpp"
#include "support/test_harness.hpp"

using bellhop::Arrival;
using bellhop::ArrivalCandidate;
using bellhop::ArrivalCapacityPlan;
using bellhop::ArrivalWorkspace;
using bellhop::ReceiverGrid;
using bellhop::ReceiverGridLayout;
using bellhop::ValidationError;
using bellhop::kOriginArrivalStorageSlots;
using bellhop::kOriginMinimumArrivalsPerCell;
using bellhop::planArrivalCapacity;
using bellhop::test::Context;

static_assert(sizeof(float) == 4U);
static_assert(sizeof(std::complex<float>) == 8U);
static_assert(sizeof(std::int32_t) == 4U);
static_assert(sizeof(Arrival) == 32U);
static_assert(std::is_same_v<decltype(Arrival::amplitude), float>);
static_assert(std::is_same_v<decltype(Arrival::phaseRadians), float>);
static_assert(
    std::is_same_v<decltype(Arrival::delaySeconds), std::complex<float>>);
static_assert(std::is_same_v<decltype(Arrival::sourceDeclinationDegrees),
                             float>);
static_assert(std::is_same_v<decltype(Arrival::receiverDeclinationDegrees),
                             float>);
static_assert(std::is_same_v<decltype(Arrival::topBounceCount),
                             std::int32_t>);
static_assert(std::is_same_v<decltype(Arrival::bottomBounceCount),
                             std::int32_t>);
static_assert(std::is_same_v<decltype(ArrivalCandidate::amplitude), double>);
static_assert(
    std::is_same_v<decltype(ArrivalCandidate::phaseRadians), double>);
static_assert(std::is_same_v<decltype(ArrivalCandidate::delaySeconds),
                             std::complex<double>>);
static_assert(std::is_same_v<decltype(ArrivalCandidate::sourceDeclinationDegrees),
                             double>);
static_assert(std::is_same_v<decltype(ArrivalCandidate::receiverDeclinationDegrees),
                             double>);
static_assert(std::is_same_v<decltype(ArrivalCandidate::topBounceCount),
                             std::int32_t>);
static_assert(std::is_same_v<decltype(ArrivalCandidate::bottomBounceCount),
                             std::int32_t>);

namespace {

constexpr std::size_t kInt32MaxAsSizeT =
    static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max());

void testCapacityPlan(Context& context) {
  const ArrivalCapacityPlan one = planArrivalCapacity(1U);
  context.check(
      one.receiverCellCount == 1U &&
          one.arrivalsPerCell == kOriginArrivalStorageSlots &&
          one.logicalSlotCount == kOriginArrivalStorageSlots,
      "single-cell plan reserves the full Origin storage budget");

  const ArrivalCapacityPlan six = planArrivalCapacity(6U);
  context.check(
      six.receiverCellCount == 6U && six.arrivalsPerCell == 3'333'333U &&
          six.logicalSlotCount == 19'999'998U,
      "six-cell plan divides the Origin budget with integer truncation");

  const ArrivalCapacityPlan twoMillion = planArrivalCapacity(2'000'000U);
  context.check(
      twoMillion.arrivalsPerCell == 10U &&
          twoMillion.logicalSlotCount == kOriginArrivalStorageSlots,
      "two-million-cell plan reaches exactly ten arrivals per cell");

  const ArrivalCapacityPlan minTenBoundary =
      planArrivalCapacity(2'000'001U);
  context.check(
      minTenBoundary.arrivalsPerCell == kOriginMinimumArrivalsPerCell &&
          minTenBoundary.logicalSlotCount == 20'000'010U,
      "the minimum-ten rule takes effect one cell past the exact division");

  const ArrivalCapacityPlan int32Boundary =
      planArrivalCapacity(kInt32MaxAsSizeT);
  context.check(
      int32Boundary.receiverCellCount == kInt32MaxAsSizeT &&
          int32Boundary.arrivalsPerCell == kOriginMinimumArrivalsPerCell &&
          int32Boundary.logicalSlotCount == 21'474'836'470U,
      "an int32-boundary cell count plans ten arrivals per cell");

  context.expectThrows<ValidationError>(
      [] { static_cast<void>(planArrivalCapacity(0U)); },
      "zero receiver cell count is rejected");
  context.expectThrows<ValidationError>(
      [] {
        static_cast<void>(planArrivalCapacity(kInt32MaxAsSizeT + 1U));
      },
      "int32-incompatible receiver cell counts are rejected");
  context.expectThrows<ValidationError>(
      [] {
        static_cast<void>(planArrivalCapacity(
            std::numeric_limits<std::size_t>::max()));
      },
      "unrepresentable receiver cell counts are rejected");

  const ArrivalCapacityPlan overridden = planArrivalCapacity(6U, 3U);
  context.check(
      overridden.receiverCellCount == 6U &&
          overridden.arrivalsPerCell == 3U &&
          overridden.logicalSlotCount == 18U,
      "capacity override replaces the Origin per-cell capacity");
  context.expectThrows<ValidationError>(
      [] { static_cast<void>(planArrivalCapacity(6U, 0U)); },
      "a zero capacity override is rejected");
  context.expectThrows<ValidationError>(
      [] {
        static_cast<void>(planArrivalCapacity(6U, kInt32MaxAsSizeT + 1U));
      },
      "an int32-incompatible capacity override is rejected");
}

void testWorkspaceRectilinear(Context& context) {
  const ReceiverGrid receivers({10.0, 20.0}, {100.0, 200.0, 300.0});
  const ArrivalWorkspace workspace(50.0, receivers);
  context.check(workspace.frequency() == 50.0,
                "workspace retains its frequency");
  context.check(
      workspace.depthCount() == 2U && workspace.rangeCount() == 3U &&
          workspace.receiverCellCount() == 6U,
      "rectilinear cell count is receivers-per-range times range count");
  context.check(
      workspace.capacity().receiverCellCount == 6U &&
          workspace.capacity().arrivalsPerCell == 3'333'333U &&
          workspace.capacity().logicalSlotCount == 19'999'998U,
      "workspace plans the Origin capacity for its actual cells");

  context.check(workspace.flatIndex(1U, 2U) == 5U,
                "flat index is depth-major and range-minor");
  context.check(workspace.flatIndex(0U, 0U) == 0U &&
                    workspace.flatIndex(1U, 0U) == 3U,
                "flat index maps depth-major boundaries");
  context.check(workspace.arrivalsAt(1U, 2U).empty() &&
                    workspace.cellAt(5U).empty() &&
                    workspace.arrivalCountAt(1U, 2U) == 0U,
                "fresh cells hold zero arrivals through every accessor");
  for (std::size_t cell = 0U; cell < workspace.receiverCellCount(); ++cell) {
    context.check(workspace.cellAt(cell).empty(),
                  "empty workspace allocates zero Arrival records per cell");
  }

  context.expectThrows<std::out_of_range>(
      [&workspace] { static_cast<void>(workspace.flatIndex(2U, 0U)); },
      "rectilinear workspace rejects an out-of-range depth index");
  context.expectThrows<std::out_of_range>(
      [&workspace] { static_cast<void>(workspace.arrivalsAt(0U, 3U)); },
      "rectilinear workspace rejects an out-of-range range index");
  context.expectThrows<std::out_of_range>(
      [&workspace] { static_cast<void>(workspace.cellAt(6U)); },
      "rectilinear workspace rejects an out-of-range cell index");

  const ReceiverGrid singleCell({10.0}, {100.0});
  const ArrivalWorkspace singleWorkspace(50.0, singleCell);
  context.check(
      singleWorkspace.receiverCellCount() == 1U &&
          singleWorkspace.capacity().arrivalsPerCell ==
              kOriginArrivalStorageSlots,
      "single-cell workspace uses the full Origin storage budget");
}

void testWorkspaceIrregular(Context& context) {
  const ReceiverGrid irregular(
      {15.0, 25.0, 35.0}, {100.0, 200.0, 300.0},
      ReceiverGridLayout::Irregular);
  const ArrivalWorkspace workspace(50.0, irregular);
  context.check(
      workspace.depthCount() == 1U && workspace.rangeCount() == 3U &&
          workspace.receiverCellCount() == 3U,
      "irregular grid accumulates one cell per range");
  context.check(
      workspace.capacity().arrivalsPerCell == 6'666'666U &&
          workspace.capacity().logicalSlotCount == 19'999'998U,
      "irregular capacity plan ignores the header depth count");
  context.check(workspace.flatIndex(0U, 0U) == 0U &&
                    workspace.flatIndex(0U, 2U) == 2U,
                "irregular flat order is one cell per range");
  context.check(workspace.arrivalCountAt(0U, 2U) == 0U &&
                    workspace.cellAt(2U).empty(),
                "irregular cells start empty");
  context.expectThrows<std::out_of_range>(
      [&workspace] { static_cast<void>(workspace.arrivalsAt(1U, 0U)); },
      "irregular workspace rejects depth indices past one cell per range");
}

void testWorkspaceLargeGrids(Context& context) {
  std::vector<double> manyDepths(2'000'000U);
  std::iota(manyDepths.begin(), manyDepths.end(), 0.0);
  const ReceiverGrid twoMillionCells(manyDepths, {0.0});
  const ArrivalWorkspace largeWorkspace(50.0, twoMillionCells);
  context.check(
      largeWorkspace.receiverCellCount() == 2'000'000U &&
          largeWorkspace.depthCount() == 2'000'000U &&
          largeWorkspace.rangeCount() == 1U &&
          largeWorkspace.capacity().arrivalsPerCell == 10U &&
          largeWorkspace.capacity().logicalSlotCount ==
              kOriginArrivalStorageSlots,
      "two-million-cell workspace keeps ten arrivals per cell");
  for (std::size_t cell = 0U; cell < largeWorkspace.receiverCellCount();
       ++cell) {
    context.check(largeWorkspace.cellAt(cell).empty(),
                  "large empty workspace allocates zero Arrival records");
  }

  std::vector<double> boundaryDepths(2'000'001U);
  std::vector<double> boundaryRanges(2'000'001U);
  std::iota(boundaryDepths.begin(), boundaryDepths.end(), 0.0);
  std::iota(boundaryRanges.begin(), boundaryRanges.end(), 0.0);
  const ReceiverGrid boundaryGrid(boundaryDepths, boundaryRanges,
                                  ReceiverGridLayout::Irregular);
  const ArrivalWorkspace boundaryWorkspace(50.0, boundaryGrid);
  context.check(
      boundaryWorkspace.receiverCellCount() == 2'000'001U &&
          boundaryWorkspace.depthCount() == 1U &&
          boundaryWorkspace.capacity().arrivalsPerCell == 10U &&
          boundaryWorkspace.capacity().logicalSlotCount == 20'000'010U,
      "overflow-boundary irregular workspace hits the minimum-ten rule");
  context.check(boundaryWorkspace.flatIndex(0U, 2'000'000U) == 2'000'000U,
                "overflow-boundary flat order stays range-linear");
}

void testWorkspaceOverride(Context& context) {
  const ReceiverGrid receivers({10.0, 20.0}, {100.0, 200.0, 300.0});
  const ArrivalWorkspace overridden(50.0, receivers, 3U);
  context.check(
      overridden.capacity().arrivalsPerCell == 3U &&
          overridden.capacity().logicalSlotCount == 18U,
      "workspace capacity override replaces the planned per-cell capacity");
  context.check(overridden.capacity().receiverCellCount == 6U &&
                    overridden.receiverCellCount() == 6U,
                "workspace capacity override keeps the actual cell count");
  context.expectThrows<ValidationError>(
      [&receivers] {
        static_cast<void>(ArrivalWorkspace(50.0, receivers, 0U));
      },
      "workspace rejects a zero capacity override");
  context.expectThrows<ValidationError>(
      [&receivers] {
        static_cast<void>(ArrivalWorkspace(50.0, receivers,
                                           kInt32MaxAsSizeT + 1U));
      },
      "workspace rejects an int32-incompatible capacity override");
}

void testWorkspaceValidation(Context& context) {
  const ReceiverGrid receivers({10.0, 20.0}, {100.0, 200.0, 300.0});
  context.expectThrows<ValidationError>(
      [&receivers] {
        static_cast<void>(ArrivalWorkspace(0.0, receivers));
      },
      "workspace rejects a non-positive frequency");
  context.expectThrows<ValidationError>(
      [&receivers] {
        static_cast<void>(ArrivalWorkspace(
            std::numeric_limits<double>::quiet_NaN(), receivers));
      },
      "workspace rejects a non-finite frequency");
}

}  // namespace

int main() {
  Context context;
  testCapacityPlan(context);
  testWorkspaceRectilinear(context);
  testWorkspaceIrregular(context);
  testWorkspaceLargeGrids(context);
  testWorkspaceOverride(context);
  testWorkspaceValidation(context);

  if (context.failureCount() != 0) {
    std::cerr << context.failureCount() << " test assertion(s) failed\n";
    return 1;
  }
  std::cout << "All Bellhop F2CPP arrival workspace tests passed\n";
  return 0;
}
