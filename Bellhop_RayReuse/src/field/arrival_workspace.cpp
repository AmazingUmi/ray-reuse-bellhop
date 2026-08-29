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
constexpr double kArrivalPhaseTolerance = static_cast<double>(0.05F);

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

Arrival storedFromCandidate(const ArrivalCandidate& candidate) {
  return Arrival{static_cast<float>(candidate.amplitude),
                 static_cast<float>(candidate.phaseRadians),
                 static_cast<std::complex<float>>(candidate.delaySeconds),
                 static_cast<float>(candidate.sourceDeclinationDegrees),
                 static_cast<float>(candidate.receiverDeclinationDegrees),
                 candidate.topBounceCount,
                 candidate.bottomBounceCount};
}

void validateCandidate(const ArrivalCandidate& candidate) {
  if (!std::isfinite(candidate.amplitude) ||
      !std::isfinite(candidate.phaseRadians) ||
      !std::isfinite(candidate.delaySeconds.real()) ||
      !std::isfinite(candidate.delaySeconds.imag()) ||
      !std::isfinite(candidate.sourceDeclinationDegrees) ||
      !std::isfinite(candidate.receiverDeclinationDegrees)) {
    throw ValidationError("arrival candidate fields must all be finite");
  }
  if (candidate.topBounceCount < 0 || candidate.bottomBounceCount < 0) {
    throw ValidationError(
        "arrival candidate bounce counts must be non-negative");
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
  validateCandidate(candidate);
  auto& cell = cells_.at(flatIndex(depthIndex, rangeIndex));
  const std::size_t count = cell.size();
  bool groups = false;
  if (count != 0U) {
    const Arrival& last = cell.back();
    const auto delayDifference =
        candidate.delaySeconds -
        static_cast<std::complex<double>>(last.delaySeconds);
    groups = omega() * std::abs(delayDifference) < kArrivalPhaseTolerance &&
             std::abs(static_cast<double>(last.phaseRadians) -
                      candidate.phaseRadians) < kArrivalPhaseTolerance;
  }
  ++candidateCount_;
  if (groups) {
    Arrival& record = cell.back();
    const float candidateAmplitude = static_cast<float>(candidate.amplitude);
    const float ampTot = record.amplitude + candidateAmplitude;
    if (std::abs(ampTot) <= std::numeric_limits<float>::epsilon()) {
      ++cuspGuardCount_;
      return;
    }
    ++mergeCount_;
    const float w1 = record.amplitude / ampTot;
    const float w2 = candidateAmplitude / ampTot;
    record.delaySeconds =
        w1 * record.delaySeconds +
        w2 * static_cast<std::complex<float>>(candidate.delaySeconds);
    record.amplitude = ampTot;
    record.sourceDeclinationDegrees =
        w1 * record.sourceDeclinationDegrees +
        w2 * static_cast<float>(candidate.sourceDeclinationDegrees);
    record.receiverDeclinationDegrees =
        w1 * record.receiverDeclinationDegrees +
        w2 * static_cast<float>(candidate.receiverDeclinationDegrees);
    return;
  }
  if (count >= capacity_.arrivalsPerCell) {
    std::size_t minimumIndex = 0U;
    for (std::size_t i = 1U; i < count; ++i)
      if (cell[i].amplitude < cell[minimumIndex].amplitude) minimumIndex = i;
    if (candidate.amplitude > cell[minimumIndex].amplitude) {
      cell[minimumIndex] = storedFromCandidate(candidate);
      ++weakestReplacementCount_;
    } else
      ++capacityDiscardCount_;
    return;
  }
  cell.push_back(storedFromCandidate(candidate));
  ++appendCount_;
  if (cell.size() == capacity_.arrivalsPerCell) ++saturatedCellCount_;
}

std::size_t ArrivalWorkspace::candidateCount() const noexcept {
  return candidateCount_;
}
std::size_t ArrivalWorkspace::appendCount() const noexcept {
  return appendCount_;
}
std::size_t ArrivalWorkspace::mergeCount() const noexcept {
  return mergeCount_;
}
std::size_t ArrivalWorkspace::cuspGuardCount() const noexcept {
  return cuspGuardCount_;
}
std::size_t ArrivalWorkspace::weakestReplacementCount() const noexcept {
  return weakestReplacementCount_;
}
std::size_t ArrivalWorkspace::capacityDiscardCount() const noexcept {
  return capacityDiscardCount_;
}
std::size_t ArrivalWorkspace::saturatedCellCount() const noexcept {
  return saturatedCellCount_;
}
}  // namespace rayreuse
