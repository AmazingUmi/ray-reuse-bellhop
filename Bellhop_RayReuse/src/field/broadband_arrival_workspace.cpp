#include "rayreuse/field/broadband_arrival_workspace.hpp"

#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string>

#include "rayreuse/error.hpp"

namespace rayreuse {
namespace {

[[nodiscard]] std::size_t checkedMultiply(std::size_t left,
                                          std::size_t right,
                                          const char* label) {
  if (left != 0U && right > std::numeric_limits<std::size_t>::max() / left) {
    throw ValidationError(std::string(label) + " exceeds size_t capacity");
  }
  return left * right;
}

[[nodiscard]] std::size_t checkedAdd(std::size_t left, std::size_t right,
                                     const char* label) {
  if (right > std::numeric_limits<std::size_t>::max() - left) {
    throw ValidationError(std::string(label) + " exceeds size_t capacity");
  }
  return left + right;
}

}  // namespace

BroadbandArrivalWorkspace::BroadbandArrivalWorkspace(
    std::span<const double> frequencies, const ReceiverGrid& receivers,
    std::optional<std::size_t> capacityOverride)
    : rangeCount_(receivers.rangeCount()),
      depthCount_(receivers.receiversPerRange()),
      frequencies_(frequencies.begin(), frequencies.end()) {
  if (rangeCount_ == 0U || depthCount_ == 0U || frequencies_.empty()) {
    throw ValidationError(
        "broadband arrival workspace requires non-empty dimensions");
  }
  for (const double value : frequencies_) {
    if (!std::isfinite(value) || value <= 0.0) {
      throw ValidationError(
          "broadband arrival-workspace frequencies must be positive and finite");
    }
  }

  const std::size_t receiverCells = checkedMultiply(
      rangeCount_, depthCount_, "broadband ARR receiver cell count");
  capacity_ = planArrivalCapacity(receiverCells, capacityOverride);
  logicalArrivalSlots_ =
      checkedMultiply(capacity_.logicalSlotCount, frequencies_.size(),
                      "broadband ARR logical slot count");
  static_cast<void>(checkedMultiply(logicalArrivalSlots_, sizeof(Arrival),
                                    "broadband ARR logical storage bytes"));
  lanes_.resize(checkedMultiply(receiverCells, frequencies_.size(),
                                "broadband ARR lane count"));
}

std::size_t BroadbandArrivalWorkspace::rangeCount() const noexcept {
  return rangeCount_;
}

std::size_t BroadbandArrivalWorkspace::depthCount() const noexcept {
  return depthCount_;
}

std::size_t BroadbandArrivalWorkspace::frequencyCount() const noexcept {
  return frequencies_.size();
}

std::size_t BroadbandArrivalWorkspace::receiverCellCount() const noexcept {
  return capacity_.receiverCellCount;
}

std::size_t BroadbandArrivalWorkspace::laneCount() const noexcept {
  return lanes_.size();
}

double BroadbandArrivalWorkspace::frequency(
    std::size_t frequencyIndex) const {
  return frequencies_.at(frequencyIndex);
}

const ArrivalCapacityPlan& BroadbandArrivalWorkspace::capacity() const
    noexcept {
  return capacity_;
}

void BroadbandArrivalWorkspace::addCandidate(
    std::size_t frequencyIndex, const ArrivalCandidate& candidate,
    std::size_t depthIndex, std::size_t rangeIndex,
    ArrivalAccumulationStatistics& localStatistics) {
  const double candidateFrequency = frequency(frequencyIndex);
  validateArrivalCandidate(candidate);
  addArrivalCandidate(laneAt(rangeIndex, depthIndex, frequencyIndex),
                      capacity_.arrivalsPerCell,
                      2.0 * std::numbers::pi * candidateFrequency, candidate,
                      localStatistics);
}

std::vector<Arrival>& BroadbandArrivalWorkspace::laneAt(
    std::size_t rangeIndex, std::size_t depthIndex,
    std::size_t frequencyIndex) {
  return lanes_[laneIndex(rangeIndex, depthIndex, frequencyIndex)];
}

std::span<const Arrival> BroadbandArrivalWorkspace::laneAt(
    std::size_t rangeIndex, std::size_t depthIndex,
    std::size_t frequencyIndex) const {
  return lanes_[laneIndex(rangeIndex, depthIndex, frequencyIndex)];
}

BroadbandArrivalWorkspace::FrequencyView
BroadbandArrivalWorkspace::frequencyView(std::size_t frequencyIndex) const {
  if (frequencyIndex >= frequencies_.size()) {
    throw std::out_of_range(
        "broadband arrival-workspace frequency index is out of range");
  }
  return FrequencyView(*this, frequencyIndex);
}

BroadbandArrivalStorageStatistics
BroadbandArrivalWorkspace::storageStatistics() const {
  BroadbandArrivalStorageStatistics result{
      .laneCount = lanes_.size(),
      .logicalArrivalSlots = logicalArrivalSlots_,
      .laneHeaderBytes = checkedMultiply(lanes_.capacity(),
                                         sizeof(std::vector<Arrival>),
                                         "broadband ARR lane-header bytes")};
  for (const std::vector<Arrival>& lane : lanes_) {
    if (!lane.empty()) ++result.nonEmptyLaneCount;
    result.storedArrivalCount = checkedAdd(
        result.storedArrivalCount, lane.size(), "broadband ARR stored arrivals");
    result.allocatedArrivalSlots = checkedAdd(
        result.allocatedArrivalSlots, lane.capacity(),
        "broadband ARR allocated arrival slots");
  }
  result.allocatedArrivalBytes =
      checkedMultiply(result.allocatedArrivalSlots, sizeof(Arrival),
                      "broadband ARR allocated arrival bytes");
  result.memoryFootprintBytes = checkedAdd(
      sizeof(BroadbandArrivalWorkspace),
      checkedMultiply(frequencies_.capacity(), sizeof(double),
                      "broadband ARR frequency bytes"),
      "broadband ARR memory footprint");
  result.memoryFootprintBytes =
      checkedAdd(result.memoryFootprintBytes, result.laneHeaderBytes,
                 "broadband ARR memory footprint");
  result.memoryFootprintBytes =
      checkedAdd(result.memoryFootprintBytes, result.allocatedArrivalBytes,
                 "broadband ARR memory footprint");
  return result;
}

std::size_t BroadbandArrivalWorkspace::laneIndex(
    std::size_t rangeIndex, std::size_t depthIndex,
    std::size_t frequencyIndex) const {
  if (rangeIndex >= rangeCount_ || depthIndex >= depthCount_ ||
      frequencyIndex >= frequencies_.size()) {
    throw std::out_of_range(
        "broadband arrival-workspace index is out of range");
  }
  return ((rangeIndex * depthCount_) + depthIndex) * frequencies_.size() +
         frequencyIndex;
}

BroadbandArrivalWorkspace::FrequencyView::FrequencyView(
    const BroadbandArrivalWorkspace& workspace,
    std::size_t frequencyIndex) noexcept
    : workspace_(&workspace), frequencyIndex_(frequencyIndex) {}

double BroadbandArrivalWorkspace::FrequencyView::frequency() const noexcept {
  return workspace_->frequencies_[frequencyIndex_];
}

std::size_t BroadbandArrivalWorkspace::FrequencyView::depthCount() const
    noexcept {
  return workspace_->depthCount_;
}

std::size_t BroadbandArrivalWorkspace::FrequencyView::rangeCount() const
    noexcept {
  return workspace_->rangeCount_;
}

std::size_t BroadbandArrivalWorkspace::FrequencyView::receiverCellCount() const
    noexcept {
  return workspace_->capacity_.receiverCellCount;
}

const ArrivalCapacityPlan&
BroadbandArrivalWorkspace::FrequencyView::capacity() const noexcept {
  return workspace_->capacity_;
}

std::size_t BroadbandArrivalWorkspace::FrequencyView::flatIndex(
    std::size_t depthIndex, std::size_t rangeIndex) const {
  if (depthIndex >= depthCount() || rangeIndex >= rangeCount()) {
    throw std::out_of_range(
        "broadband arrival frequency-view index is out of range");
  }
  return depthIndex * rangeCount() + rangeIndex;
}

std::span<const Arrival>
BroadbandArrivalWorkspace::FrequencyView::cellAt(
    std::size_t cellIndex) const {
  if (cellIndex >= receiverCellCount()) {
    throw std::out_of_range(
        "broadband arrival frequency-view cell index is out of range");
  }
  const std::size_t depthIndex = cellIndex / rangeCount();
  const std::size_t rangeIndex = cellIndex % rangeCount();
  return workspace_->laneAt(rangeIndex, depthIndex, frequencyIndex_);
}

std::span<const Arrival>
BroadbandArrivalWorkspace::FrequencyView::arrivalsAt(
    std::size_t depthIndex, std::size_t rangeIndex) const {
  return cellAt(flatIndex(depthIndex, rangeIndex));
}

std::size_t BroadbandArrivalWorkspace::FrequencyView::arrivalCountAt(
    std::size_t depthIndex, std::size_t rangeIndex) const {
  return arrivalsAt(depthIndex, rangeIndex).size();
}

}  // namespace rayreuse
