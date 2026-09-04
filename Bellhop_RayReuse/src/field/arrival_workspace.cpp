#include "rayreuse/field/arrival_workspace.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string>

#include "rayreuse/error.hpp"

namespace rayreuse {
namespace {
std::size_t checkedMultiply(std::size_t left, std::size_t right,
                            const char* label) {
  if (left != 0U && right > std::numeric_limits<std::size_t>::max() / left) {
    throw ValidationError(std::string(label) + " exceeds size_t capacity");
  }
  return left * right;
}

void requireOriginInt32(std::size_t value, const char* label) {
  if (value >
      static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())) {
    throw ValidationError(std::string(label) + " exceeds the ARR int32 limit");
  }
}

}  // namespace

ArrivalCapacityPlan planArrivalCapacity(
    std::size_t receiverCellCount,
    std::optional<std::size_t> arrivalsPerCellOverride) {
  if (receiverCellCount == 0U) {
    throw ValidationError("ARR receiver cell count must be positive");
  }
  requireOriginInt32(receiverCellCount, "ARR receiver cell count");
  std::size_t arrivalsPerCell = 0U;
  if (arrivalsPerCellOverride.has_value()) {
    arrivalsPerCell = *arrivalsPerCellOverride;
    if (arrivalsPerCell == 0U) {
      throw ValidationError("ARR arrivals-per-cell override must be positive");
    }
    requireOriginInt32(arrivalsPerCell, "ARR arrivals-per-cell override");
  } else {
    arrivalsPerCell = std::max(kOriginArrivalStorageSlots / receiverCellCount,
                               kOriginMinimumArrivalsPerCell);
  }
  const std::size_t logicalSlotCount = checkedMultiply(
      receiverCellCount, arrivalsPerCell, "ARR logical slot count");
  checkedMultiply(arrivalsPerCell, sizeof(Arrival),
                  "ARR per-cell storage bytes");
  checkedMultiply(logicalSlotCount, sizeof(Arrival),
                  "ARR logical storage bytes");
  return {receiverCellCount, arrivalsPerCell, logicalSlotCount};
}

ArrivalWorkspace::ArrivalWorkspace(double frequency,
                                   const ReceiverGrid& receivers,
                                   std::optional<std::size_t> capacityOverride)
    : frequency_(frequency),
      depthCount_(receivers.receiversPerRange()),
      rangeCount_(receivers.rangeCount()) {
  if (!std::isfinite(frequency_) || frequency_ <= 0.0) {
    throw ValidationError(
        "arrival-workspace frequency must be positive and finite");
  }
  capacity_ = planArrivalCapacity(
      checkedMultiply(depthCount_, rangeCount_, "ARR receiver cell count"),
      capacityOverride);
  cells_.resize(capacity_.receiverCellCount);
}

double ArrivalWorkspace::frequency() const noexcept { return frequency_; }
double ArrivalWorkspace::omega() const noexcept {
  return 2.0 * std::numbers::pi * frequency_;
}
std::size_t ArrivalWorkspace::depthCount() const noexcept {
  return depthCount_;
}
std::size_t ArrivalWorkspace::rangeCount() const noexcept {
  return rangeCount_;
}
std::size_t ArrivalWorkspace::receiverCellCount() const noexcept {
  return cells_.size();
}
const ArrivalCapacityPlan& ArrivalWorkspace::capacity() const noexcept {
  return capacity_;
}

std::size_t ArrivalWorkspace::flatIndex(std::size_t depthIndex,
                                        std::size_t rangeIndex) const {
  if (depthIndex >= depthCount_ || rangeIndex >= rangeCount_) {
    throw std::out_of_range("arrival-workspace index is out of range");
  }
  return depthIndex * rangeCount_ + rangeIndex;
}
std::span<const Arrival> ArrivalWorkspace::cellAt(std::size_t cellIndex) const {
  return cells_.at(cellIndex);
}
std::span<const Arrival> ArrivalWorkspace::arrivalsAt(std::size_t d,
                                                      std::size_t r) const {
  return cellAt(flatIndex(d, r));
}
std::size_t ArrivalWorkspace::arrivalCountAt(std::size_t d,
                                             std::size_t r) const {
  return arrivalsAt(d, r).size();
}

void ArrivalWorkspace::addCandidate(double frequency,
                                    const ArrivalCandidate& candidate,
                                    std::size_t depthIndex,
                                    std::size_t rangeIndex) {
  if (frequency != frequency_)
    throw ValidationError(
        "arrival candidate frequency does not match the workspace");
  validateArrivalCandidate(candidate);
  auto& cell = cells_.at(flatIndex(depthIndex, rangeIndex));
  addArrivalCandidate(cell, capacity_.arrivalsPerCell, omega(), candidate,
                      statistics_);
}

std::size_t ArrivalWorkspace::candidateCount() const noexcept {
  return statistics_.candidateCount;
}
std::size_t ArrivalWorkspace::appendCount() const noexcept {
  return statistics_.appendCount;
}
std::size_t ArrivalWorkspace::mergeCount() const noexcept {
  return statistics_.mergeCount;
}
std::size_t ArrivalWorkspace::cuspGuardCount() const noexcept {
  return statistics_.cuspGuardCount;
}
std::size_t ArrivalWorkspace::weakestReplacementCount() const noexcept {
  return statistics_.weakestReplacementCount;
}
std::size_t ArrivalWorkspace::capacityDiscardCount() const noexcept {
  return statistics_.capacityDiscardCount;
}
std::size_t ArrivalWorkspace::saturatedCellCount() const noexcept {
  return statistics_.saturatedCellCount;
}
}  // namespace rayreuse
