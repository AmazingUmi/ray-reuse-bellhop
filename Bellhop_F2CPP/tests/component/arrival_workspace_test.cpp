#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <numbers>
#include <numeric>
#include <optional>
#include <span>
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
constexpr double kArrivalPhaseTolerance = static_cast<double>(0.05F);
constexpr double kTestFrequency = 50.0;

ArrivalWorkspace makeSingleCellWorkspace(std::size_t perCellCapacity = 10U) {
  const ReceiverGrid receivers({10.0}, {100.0});
  return ArrivalWorkspace(kTestFrequency, receivers, perCellCapacity);
}

// Distinct serial ensures the candidate never groups with an earlier record:
// adjacent phases differ by 0.2 > 0.05 and omega * 0.01 > 0.05.
ArrivalCandidate isolatedCandidate(double amplitude, double phase,
                                   std::size_t serial) {
  return ArrivalCandidate{
      .amplitude = amplitude,
      .phaseRadians = phase,
      .delaySeconds = {0.01 * static_cast<double>(serial), 0.0},
      .sourceDeclinationDegrees = 10.0,
      .receiverDeclinationDegrees = 20.0,
      .topBounceCount = 1,
      .bottomBounceCount = 2};
}

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

void testStrictDelayThreshold(Context& context) {
  const double omega = 2.0 * std::numbers::pi * kTestFrequency;
  const double equalDelta = kArrivalPhaseTolerance / omega;
  context.check(omega * equalDelta == kArrivalPhaseTolerance,
                "delay-boundary constant lands exactly on the threshold");
  const double belowDelta = std::nextafter(equalDelta, 0.0);
  const double aboveDelta =
      std::nextafter(equalDelta, std::numeric_limits<double>::infinity());

  ArrivalWorkspace below = makeSingleCellWorkspace();
  below.addCandidate(kTestFrequency,
                     ArrivalCandidate{.amplitude = 1.0,
                                      .phaseRadians = 0.0,
                                      .delaySeconds = {0.0, 0.0},
                                      .sourceDeclinationDegrees = 10.0,
                                      .receiverDeclinationDegrees = 20.0,
                                      .topBounceCount = 1,
                                      .bottomBounceCount = 2},
                     0U, 0U);
  below.addCandidate(kTestFrequency,
                     ArrivalCandidate{.amplitude = 2.0,
                                      .phaseRadians = 0.0,
                                      .delaySeconds = {belowDelta, 0.0},
                                      .sourceDeclinationDegrees = 10.0,
                                      .receiverDeclinationDegrees = 20.0,
                                      .topBounceCount = 1,
                                      .bottomBounceCount = 2},
                     0U, 0U);
  context.check(below.cellAt(0U).size() == 1U &&
                    below.mergeCount() == 1U &&
                    below.appendCount() == 1U,
                "just-below delay difference groups with the last record");

  ArrivalWorkspace equal = makeSingleCellWorkspace();
  equal.addCandidate(kTestFrequency,
                     ArrivalCandidate{.amplitude = 1.0,
                                      .phaseRadians = 0.0,
                                      .delaySeconds = {0.0, 0.0},
                                      .sourceDeclinationDegrees = 10.0,
                                      .receiverDeclinationDegrees = 20.0,
                                      .topBounceCount = 1,
                                      .bottomBounceCount = 2},
                     0U, 0U);
  equal.addCandidate(kTestFrequency,
                     ArrivalCandidate{.amplitude = 2.0,
                                      .phaseRadians = 0.0,
                                      .delaySeconds = {equalDelta, 0.0},
                                      .sourceDeclinationDegrees = 10.0,
                                      .receiverDeclinationDegrees = 20.0,
                                      .topBounceCount = 1,
                                      .bottomBounceCount = 2},
                     0U, 0U);
  context.check(equal.cellAt(0U).size() == 2U && equal.mergeCount() == 0U,
                "exactly-equal delay product does not group (strict less)");

  ArrivalWorkspace above = makeSingleCellWorkspace();
  above.addCandidate(kTestFrequency,
                     ArrivalCandidate{.amplitude = 1.0,
                                      .phaseRadians = 0.0,
                                      .delaySeconds = {0.0, 0.0},
                                      .sourceDeclinationDegrees = 10.0,
                                      .receiverDeclinationDegrees = 20.0,
                                      .topBounceCount = 1,
                                      .bottomBounceCount = 2},
                     0U, 0U);
  above.addCandidate(kTestFrequency,
                     ArrivalCandidate{.amplitude = 2.0,
                                      .phaseRadians = 0.0,
                                      .delaySeconds = {aboveDelta, 0.0},
                                      .sourceDeclinationDegrees = 10.0,
                                      .receiverDeclinationDegrees = 20.0,
                                      .topBounceCount = 1,
                                      .bottomBounceCount = 2},
                     0U, 0U);
  context.check(above.cellAt(0U).size() == 2U && above.mergeCount() == 0U,
                "just-above delay difference does not group");
}

void testStrictPhaseThreshold(Context& context) {
  const double belowPhase = std::nextafter(kArrivalPhaseTolerance, 0.0);
  const double abovePhase =
      std::nextafter(kArrivalPhaseTolerance,
                     std::numeric_limits<double>::infinity());

  ArrivalWorkspace below = makeSingleCellWorkspace();
  below.addCandidate(kTestFrequency,
                     ArrivalCandidate{.amplitude = 1.0,
                                      .phaseRadians = 0.0,
                                      .delaySeconds = {0.0, 0.0},
                                      .sourceDeclinationDegrees = 10.0,
                                      .receiverDeclinationDegrees = 20.0,
                                      .topBounceCount = 1,
                                      .bottomBounceCount = 2},
                     0U, 0U);
  below.addCandidate(kTestFrequency,
                     ArrivalCandidate{.amplitude = 2.0,
                                      .phaseRadians = belowPhase,
                                      .delaySeconds = {0.0, 0.0},
                                      .sourceDeclinationDegrees = 10.0,
                                      .receiverDeclinationDegrees = 20.0,
                                      .topBounceCount = 1,
                                      .bottomBounceCount = 2},
                     0U, 0U);
  context.check(below.cellAt(0U).size() == 1U &&
                    below.mergeCount() == 1U &&
                    below.appendCount() == 1U,
                "just-below phase difference groups with the last record");

  ArrivalWorkspace equal = makeSingleCellWorkspace();
  equal.addCandidate(kTestFrequency,
                     ArrivalCandidate{.amplitude = 1.0,
                                      .phaseRadians = 0.0,
                                      .delaySeconds = {0.0, 0.0},
                                      .sourceDeclinationDegrees = 10.0,
                                      .receiverDeclinationDegrees = 20.0,
                                      .topBounceCount = 1,
                                      .bottomBounceCount = 2},
                     0U, 0U);
  equal.addCandidate(kTestFrequency,
                     ArrivalCandidate{.amplitude = 2.0,
                                      .phaseRadians = kArrivalPhaseTolerance,
                                      .delaySeconds = {0.0, 0.0},
                                      .sourceDeclinationDegrees = 10.0,
                                      .receiverDeclinationDegrees = 20.0,
                                      .topBounceCount = 1,
                                      .bottomBounceCount = 2},
                     0U, 0U);
  context.check(equal.cellAt(0U).size() == 2U && equal.mergeCount() == 0U,
                "exactly-equal phase difference does not group (strict less)");

  ArrivalWorkspace above = makeSingleCellWorkspace();
  above.addCandidate(kTestFrequency,
                     ArrivalCandidate{.amplitude = 1.0,
                                      .phaseRadians = 0.0,
                                      .delaySeconds = {0.0, 0.0},
                                      .sourceDeclinationDegrees = 10.0,
                                      .receiverDeclinationDegrees = 20.0,
                                      .topBounceCount = 1,
                                      .bottomBounceCount = 2},
                     0U, 0U);
  above.addCandidate(kTestFrequency,
                     ArrivalCandidate{.amplitude = 2.0,
                                      .phaseRadians = abovePhase,
                                      .delaySeconds = {0.0, 0.0},
                                      .sourceDeclinationDegrees = 10.0,
                                      .receiverDeclinationDegrees = 20.0,
                                      .topBounceCount = 1,
                                      .bottomBounceCount = 2},
                     0U, 0U);
  context.check(above.cellAt(0U).size() == 2U && above.mergeCount() == 0U,
                "just-above phase difference does not group");
}

void testAndSemantics(Context& context) {
  // Phase within tolerance but delay beyond tolerance: no grouping.
  ArrivalWorkspace phaseOnly = makeSingleCellWorkspace();
  phaseOnly.addCandidate(kTestFrequency,
                         ArrivalCandidate{.amplitude = 1.0,
                                          .phaseRadians = 0.0,
                                          .delaySeconds = {0.0, 0.0},
                                          .sourceDeclinationDegrees = 10.0,
                                          .receiverDeclinationDegrees = 20.0,
                                          .topBounceCount = 1,
                                          .bottomBounceCount = 2},
                         0U, 0U);
  phaseOnly.addCandidate(kTestFrequency,
                         ArrivalCandidate{.amplitude = 2.0,
                                          .phaseRadians = 0.02,
                                          .delaySeconds = {0.1, 0.0},
                                          .sourceDeclinationDegrees = 10.0,
                                          .receiverDeclinationDegrees = 20.0,
                                          .topBounceCount = 1,
                                          .bottomBounceCount = 2},
                         0U, 0U);
  context.check(phaseOnly.cellAt(0U).size() == 2U &&
                    phaseOnly.mergeCount() == 0U,
                "close phase with a distant delay does not group");

  // Delay within tolerance but phase beyond tolerance: no grouping.
  ArrivalWorkspace delayOnly = makeSingleCellWorkspace();
  delayOnly.addCandidate(kTestFrequency,
                         ArrivalCandidate{.amplitude = 1.0,
                                          .phaseRadians = 0.0,
                                          .delaySeconds = {0.0, 0.0},
                                          .sourceDeclinationDegrees = 10.0,
                                          .receiverDeclinationDegrees = 20.0,
                                          .topBounceCount = 1,
                                          .bottomBounceCount = 2},
                         0U, 0U);
  delayOnly.addCandidate(kTestFrequency,
                         ArrivalCandidate{.amplitude = 2.0,
                                          .phaseRadians = 1.0,
                                          .delaySeconds = {1.0e-5, 0.0},
                                          .sourceDeclinationDegrees = 10.0,
                                          .receiverDeclinationDegrees = 20.0,
                                          .topBounceCount = 1,
                                          .bottomBounceCount = 2},
                         0U, 0U);
  context.check(delayOnly.cellAt(0U).size() == 2U &&
                    delayOnly.mergeCount() == 0U,
                "close delay with a distant phase does not group");
}

void testFloatMergeIntermediates(Context& context) {
  ArrivalWorkspace workspace = makeSingleCellWorkspace();
  workspace.addCandidate(
      kTestFrequency,
      ArrivalCandidate{.amplitude = 1.1,
                       .phaseRadians = 0.3,
                       .delaySeconds = {1.0, 2.0},
                       .sourceDeclinationDegrees = 15.1,
                       .receiverDeclinationDegrees = 25.2,
                       .topBounceCount = 2,
                       .bottomBounceCount = 3},
      0U, 0U);
  workspace.addCandidate(
      kTestFrequency,
      ArrivalCandidate{.amplitude = 2.2,
                       .phaseRadians = 0.3,
                       .delaySeconds = {1.0001, 2.0},
                       .sourceDeclinationDegrees = 30.3,
                       .receiverDeclinationDegrees = 40.4,
                       .topBounceCount = 5,
                       .bottomBounceCount = 7},
      0U, 0U);

  const float firstAmplitude = static_cast<float>(1.1);
  const float secondAmplitude = static_cast<float>(2.2);
  const float ampTot = firstAmplitude + secondAmplitude;
  const float w1 = firstAmplitude / ampTot;
  const float w2 = secondAmplitude / ampTot;
  const std::complex<float> firstDelay{1.0F, 2.0F};
  const std::complex<float> secondDelay{static_cast<float>(1.0001), 2.0F};
  const std::complex<float> expectedDelay{
      w1 * firstDelay.real() + w2 * secondDelay.real(),
      w1 * firstDelay.imag() + w2 * secondDelay.imag()};
  const float expectedSourceAngle =
      w1 * static_cast<float>(15.1) + w2 * static_cast<float>(30.3);
  const float expectedReceiverAngle =
      w1 * static_cast<float>(25.2) + w2 * static_cast<float>(40.4);

  const std::span<const Arrival> cell = workspace.cellAt(0U);
  context.check(cell.size() == 1U,
                "grouped candidate merges into the last record slot");
  context.check(cell[0].amplitude == ampTot,
                "merged amplitude equals the float AmpTot");
  context.check(cell[0].delaySeconds == expectedDelay,
                "merged delay equals the explicitly computed float sum");
  context.check(
      cell[0].sourceDeclinationDegrees == expectedSourceAngle,
      "merged source angle equals the explicitly computed float sum");
  context.check(
      cell[0].receiverDeclinationDegrees == expectedReceiverAngle,
      "merged receiver angle equals the explicitly computed float sum");
  context.check(cell[0].phaseRadians == static_cast<float>(0.3),
                "merge preserves the old phase");
  context.check(cell[0].topBounceCount == 2 &&
                    cell[0].bottomBounceCount == 3,
                "merge preserves the old bounce counts");
  context.check(workspace.candidateCount() == 2U &&
                    workspace.appendCount() == 1U &&
                    workspace.mergeCount() == 1U &&
                    workspace.cuspGuardCount() == 0U,
                "merge statistics stay consistent");
}

void testCuspGuard(Context& context) {
  ArrivalWorkspace workspace = makeSingleCellWorkspace();
  workspace.addCandidate(
      kTestFrequency,
      ArrivalCandidate{.amplitude = 5.0e-8,
                       .phaseRadians = 0.1,
                       .delaySeconds = {1.0e-5, 2.0e-5},
                       .sourceDeclinationDegrees = 10.0,
                       .receiverDeclinationDegrees = 20.0,
                       .topBounceCount = 1,
                       .bottomBounceCount = 2},
      0U, 0U);
  const Arrival before = workspace.cellAt(0U)[0];
  workspace.addCandidate(
      kTestFrequency,
      ArrivalCandidate{.amplitude = 5.0e-8,
                       .phaseRadians = 0.1,
                       .delaySeconds = {1.0e-5, 2.0e-5},
                       .sourceDeclinationDegrees = 10.0,
                       .receiverDeclinationDegrees = 20.0,
                       .topBounceCount = 1,
                       .bottomBounceCount = 2},
      0U, 0U);
  const Arrival after = workspace.cellAt(0U)[0];
  context.check(
      before.amplitude == after.amplitude &&
          before.phaseRadians == after.phaseRadians &&
          before.delaySeconds == after.delaySeconds &&
          before.sourceDeclinationDegrees == after.sourceDeclinationDegrees &&
          before.receiverDeclinationDegrees == after.receiverDeclinationDegrees &&
          before.topBounceCount == after.topBounceCount &&
          before.bottomBounceCount == after.bottomBounceCount,
      "axial-cusp cancellation leaves the record byte-for-byte unchanged");
  context.check(workspace.cuspGuardCount() == 1U &&
                    workspace.mergeCount() == 0U &&
                    workspace.cellAt(0U).size() == 1U,
                "cusp guard counts a suppressed merge");

  workspace.addCandidate(kTestFrequency,
                         ArrivalCandidate{.amplitude = 1.2e-7,
                                          .phaseRadians = 0.1,
                                          .delaySeconds = {1.0e-5, 2.0e-5},
                                          .sourceDeclinationDegrees = 10.0,
                                          .receiverDeclinationDegrees = 20.0,
                                          .topBounceCount = 1,
                                          .bottomBounceCount = 2},
                         0U, 0U);
  context.check(
      workspace.cellAt(0U)[0].amplitude ==
          static_cast<float>(5.0e-8) + static_cast<float>(1.2e-7),
      "an above-epsilon sum still merges after the guard");
  context.check(workspace.mergeCount() == 1U &&
                    workspace.cuspGuardCount() == 1U,
                "merge after guard keeps both counters");

  ArrivalWorkspace zero = makeSingleCellWorkspace();
  zero.addCandidate(kTestFrequency,
                    ArrivalCandidate{.amplitude = 0.0,
                                     .phaseRadians = 0.0,
                                     .delaySeconds = {0.0, 0.0},
                                     .sourceDeclinationDegrees = 0.0,
                                     .receiverDeclinationDegrees = 0.0,
                                     .topBounceCount = 0,
                                     .bottomBounceCount = 0},
                    0U, 0U);
  zero.addCandidate(kTestFrequency,
                    ArrivalCandidate{.amplitude = 0.0,
                                     .phaseRadians = 0.0,
                                     .delaySeconds = {0.0, 0.0},
                                     .sourceDeclinationDegrees = 0.0,
                                     .receiverDeclinationDegrees = 0.0,
                                     .topBounceCount = 0,
                                     .bottomBounceCount = 0},
                    0U, 0U);
  context.check(zero.cuspGuardCount() == 1U &&
                    zero.cellAt(0U)[0].amplitude == 0.0F &&
                    zero.cellAt(0U).size() == 1U,
                "zero-amplitude grouping hits the cusp guard");
}

void testFullCellRetention(Context& context) {
  ArrivalWorkspace workspace = makeSingleCellWorkspace(3U);
  workspace.addCandidate(kTestFrequency, isolatedCandidate(2.0, 0.1, 1U),
                         0U, 0U);
  workspace.addCandidate(kTestFrequency, isolatedCandidate(1.0, 0.3, 2U),
                         0U, 0U);
  workspace.addCandidate(kTestFrequency, isolatedCandidate(1.0, 0.5, 3U),
                         0U, 0U);
  context.check(workspace.cellAt(0U).size() == 3U &&
                    workspace.appendCount() == 3U &&
                    workspace.saturatedCellCount() == 1U,
                "cell reaches capacity through three appends");

  workspace.addCandidate(kTestFrequency, isolatedCandidate(1.5, 0.7, 4U),
                         0U, 0U);
  context.check(
      workspace.cellAt(0U)[0].amplitude == 2.0F &&
          workspace.cellAt(0U)[1].amplitude == 1.5F &&
          workspace.cellAt(0U)[2].amplitude == 1.0F,
      "capacity replacement targets the first minimum-amplitude slot");
  context.check(workspace.weakestReplacementCount() == 1U &&
                    workspace.capacityDiscardCount() == 0U,
                "first-minimum tie selects the earlier slot");

  workspace.addCandidate(kTestFrequency, isolatedCandidate(1.5, 0.9, 5U),
                         0U, 0U);
  context.check(
      workspace.cellAt(0U)[0].amplitude == 2.0F &&
          workspace.cellAt(0U)[1].amplitude == 1.5F &&
          workspace.cellAt(0U)[2].amplitude == 1.5F,
      "second replacement targets the remaining minimum slot");
  context.check(workspace.weakestReplacementCount() == 2U,
                "second weakest replacement is counted");

  workspace.addCandidate(kTestFrequency, isolatedCandidate(1.5, 1.1, 6U),
                         0U, 0U);
  context.check(
      workspace.cellAt(0U).size() == 3U &&
          workspace.cellAt(0U)[1].amplitude == 1.5F &&
          workspace.weakestReplacementCount() == 2U &&
          workspace.capacityDiscardCount() == 1U,
      "equal-amplitude candidate is discarded at capacity");

  workspace.addCandidate(kTestFrequency, isolatedCandidate(1.0, 1.3, 7U),
                         0U, 0U);
  context.check(
      workspace.cellAt(0U).size() == 3U &&
          workspace.cellAt(0U)[1].amplitude == 1.5F &&
          workspace.weakestReplacementCount() == 2U &&
          workspace.capacityDiscardCount() == 2U,
      "weaker candidate is discarded at capacity");

  workspace.addCandidate(kTestFrequency, isolatedCandidate(3.0, 1.5, 8U),
                         0U, 0U);
  context.check(
      workspace.cellAt(0U).size() == 3U &&
          workspace.cellAt(0U)[0].amplitude == 2.0F &&
          workspace.cellAt(0U)[1].amplitude == 3.0F &&
          workspace.cellAt(0U)[2].amplitude == 1.5F,
      "strictly stronger candidate replaces the weakest without reordering");
  context.check(
      workspace.candidateCount() == 8U && workspace.appendCount() == 3U &&
          workspace.mergeCount() == 0U &&
          workspace.cuspGuardCount() == 0U &&
          workspace.weakestReplacementCount() == 3U &&
          workspace.capacityDiscardCount() == 2U &&
          workspace.saturatedCellCount() == 1U,
      "full-cell statistics distinguish replacement from discard");
}

void testNonLastGrouping(Context& context) {
  ArrivalWorkspace workspace = makeSingleCellWorkspace(2U);
  workspace.addCandidate(
      kTestFrequency,
      ArrivalCandidate{.amplitude = 1.0,
                       .phaseRadians = 0.0,
                       .delaySeconds = {0.0, 0.0},
                       .sourceDeclinationDegrees = 10.0,
                       .receiverDeclinationDegrees = 20.0,
                       .topBounceCount = 1,
                       .bottomBounceCount = 2},
      0U, 0U);
  workspace.addCandidate(
      kTestFrequency,
      ArrivalCandidate{.amplitude = 1.0,
                       .phaseRadians = 1.0,
                       .delaySeconds = {0.5, 0.0},
                       .sourceDeclinationDegrees = 10.0,
                       .receiverDeclinationDegrees = 20.0,
                       .topBounceCount = 1,
                       .bottomBounceCount = 2},
      0U, 0U);
  workspace.addCandidate(
      kTestFrequency,
      ArrivalCandidate{.amplitude = 2.0,
                       .phaseRadians = 0.02,
                       .delaySeconds = {1.0e-5, 0.0},
                       .sourceDeclinationDegrees = 10.0,
                       .receiverDeclinationDegrees = 20.0,
                       .topBounceCount = 5,
                       .bottomBounceCount = 7},
      0U, 0U);
  const std::span<const Arrival> cell = workspace.cellAt(0U);
  context.check(
      cell.size() == 2U && cell[0].amplitude == 2.0F &&
          cell[0].phaseRadians == static_cast<float>(0.02) &&
          cell[0].topBounceCount == 5 && cell[0].bottomBounceCount == 7,
      "candidate similar to a non-last record is not merged");
  context.check(
      cell[1].amplitude == 1.0F && cell[1].phaseRadians == 1.0F &&
          cell[1].delaySeconds == std::complex<float>{0.5F, 0.0F},
      "last record stays in place after the replacement");
  context.check(workspace.appendCount() == 2U &&
                    workspace.mergeCount() == 0U &&
                    workspace.weakestReplacementCount() == 1U,
                "non-last grouping takes the capacity path instead");
}

void testSaturatedAndZeroCells(Context& context) {
  const ReceiverGrid receivers({10.0, 20.0}, {100.0, 200.0});
  ArrivalWorkspace workspace(kTestFrequency, receivers, 2U);
  workspace.addCandidate(kTestFrequency, isolatedCandidate(1.0, 0.1, 1U),
                         0U, 0U);
  workspace.addCandidate(kTestFrequency, isolatedCandidate(1.0, 0.3, 2U),
                         0U, 0U);
  workspace.addCandidate(kTestFrequency, isolatedCandidate(1.0, 0.5, 3U),
                         1U, 1U);
  workspace.addCandidate(kTestFrequency, isolatedCandidate(1.0, 0.7, 4U),
                         1U, 1U);
  context.check(workspace.saturatedCellCount() == 2U,
                "saturated-cell count tracks distinct full cells");
  context.check(workspace.candidateCount() == 4U &&
                    workspace.appendCount() == 4U,
                "multi-cell candidates accumulate across cells");
  context.check(workspace.arrivalCountAt(0U, 1U) == 0U &&
                    workspace.arrivalCountAt(1U, 0U) == 0U &&
                    workspace.cellAt(1U).empty() &&
                    workspace.cellAt(2U).empty(),
                "zero-arrival cells remain valid and empty");
  workspace.addCandidate(kTestFrequency, isolatedCandidate(1.0, 0.9, 5U),
                         0U, 1U);
  context.check(workspace.arrivalCountAt(0U, 1U) == 1U &&
                    workspace.appendCount() == 5U &&
                    workspace.saturatedCellCount() == 2U,
                "an empty cell accepts its first candidate");
}

void testCandidateValidation(Context& context) {
  ArrivalWorkspace workspace = makeSingleCellWorkspace();
  const ArrivalCandidate valid{.amplitude = 1.0,
                               .phaseRadians = 0.0,
                               .delaySeconds = {0.0, 0.0},
                               .sourceDeclinationDegrees = 10.0,
                               .receiverDeclinationDegrees = 20.0,
                               .topBounceCount = 1,
                               .bottomBounceCount = 2};
  context.expectThrows<ValidationError>(
      [&workspace] {
        ArrivalCandidate invalid = {};
        invalid.amplitude = std::numeric_limits<double>::quiet_NaN();
        workspace.addCandidate(kTestFrequency, invalid, 0U, 0U);
      },
      "non-finite candidate amplitude is rejected before mutation");
  context.expectThrows<ValidationError>(
      [&workspace] {
        ArrivalCandidate invalid = {};
        invalid.phaseRadians = std::numeric_limits<double>::infinity();
        workspace.addCandidate(kTestFrequency, invalid, 0U, 0U);
      },
      "non-finite candidate phase is rejected before mutation");
  context.expectThrows<ValidationError>(
      [&workspace] {
        ArrivalCandidate invalid = {};
        invalid.delaySeconds.imag(std::numeric_limits<double>::quiet_NaN());
        workspace.addCandidate(kTestFrequency, invalid, 0U, 0U);
      },
      "non-finite candidate delay is rejected before mutation");
  context.expectThrows<ValidationError>(
      [&workspace] {
        ArrivalCandidate invalid = {};
        invalid.sourceDeclinationDegrees =
            std::numeric_limits<double>::infinity();
        workspace.addCandidate(kTestFrequency, invalid, 0U, 0U);
      },
      "non-finite candidate angle is rejected before mutation");
  context.expectThrows<ValidationError>(
      [&workspace, &valid] {
        ArrivalCandidate invalid = valid;
        invalid.amplitude = -1.0;
        workspace.addCandidate(kTestFrequency, invalid, 0U, 0U);
      },
      "negative candidate amplitude is rejected before mutation");
  context.expectThrows<ValidationError>(
      [&workspace, &valid] {
        ArrivalCandidate invalid = valid;
        invalid.topBounceCount = -1;
        workspace.addCandidate(kTestFrequency, invalid, 0U, 0U);
      },
      "negative candidate bounce count is rejected before mutation");
  context.expectThrows<ValidationError>(
      [&workspace, &valid] {
        workspace.addCandidate(kTestFrequency + 1.0, valid, 0U, 0U);
      },
      "frequency mismatch is rejected before mutation");
  context.expectThrows<ValidationError>(
      [&workspace, &valid] {
        workspace.addCandidate(std::numeric_limits<double>::quiet_NaN(),
                               valid, 0U, 0U);
      },
      "non-finite candidate frequency is rejected before mutation");
  context.expectThrows<std::out_of_range>(
      [&workspace, &valid] { workspace.addCandidate(kTestFrequency, valid, 1U, 0U); },
      "invalid depth index is rejected before mutation");
  context.expectThrows<std::out_of_range>(
      [&workspace, &valid] { workspace.addCandidate(kTestFrequency, valid, 0U, 1U); },
      "invalid range index is rejected before mutation");

  context.check(
      workspace.candidateCount() == 0U && workspace.appendCount() == 0U &&
          workspace.mergeCount() == 0U && workspace.cuspGuardCount() == 0U &&
          workspace.weakestReplacementCount() == 0U &&
          workspace.capacityDiscardCount() == 0U &&
          workspace.saturatedCellCount() == 0U &&
          workspace.cellAt(0U).empty(),
      "rejected candidates leave cells and statistics untouched");
  workspace.addCandidate(kTestFrequency, valid, 0U, 0U);
  context.check(workspace.candidateCount() == 1U &&
                    workspace.arrivalCountAt(0U, 0U) == 1U,
                "the workspace accepts a valid candidate afterwards");
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
  testStrictDelayThreshold(context);
  testStrictPhaseThreshold(context);
  testAndSemantics(context);
  testFloatMergeIntermediates(context);
  testCuspGuard(context);
  testFullCellRetention(context);
  testNonLastGrouping(context);
  testSaturatedAndZeroCells(context);
  testCandidateValidation(context);

  if (context.failureCount() != 0) {
    std::cerr << context.failureCount() << " test assertion(s) failed\n";
    return 1;
  }
  std::cout << "All Bellhop F2CPP arrival workspace tests passed\n";
  return 0;
}
