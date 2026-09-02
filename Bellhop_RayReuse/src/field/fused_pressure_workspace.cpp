#include "rayreuse/field/fused_pressure_workspace.hpp"

#include <limits>
#include <stdexcept>

#include "rayreuse/error.hpp"

namespace rayreuse {
namespace {

[[nodiscard]] std::size_t checkedProduct(std::size_t lhs, std::size_t rhs) {
  if (lhs != 0U && rhs > std::numeric_limits<std::size_t>::max() / lhs) {
    throw ValidationError("fused pressure-workspace dimensions overflow size_t");
  }
  return lhs * rhs;
}

}  // namespace

FusedPressureWorkspace::FusedPressureWorkspace(
    const ReceiverGrid& receivers, std::size_t frequencyCount)
    : rangeCount_(receivers.rangeCount()),
      depthCount_(receivers.receiversPerRange()),
      frequencyCount_(frequencyCount),
      pressure_(checkedProduct(checkedProduct(rangeCount_, depthCount_),
                               frequencyCount_)) {
  if (rangeCount_ == 0U || depthCount_ == 0U || frequencyCount_ == 0U) {
    throw ValidationError(
        "fused pressure workspace requires non-empty dimensions");
  }
}

std::size_t FusedPressureWorkspace::rangeCount() const noexcept {
  return rangeCount_;
}

std::size_t FusedPressureWorkspace::depthCount() const noexcept {
  return depthCount_;
}

std::size_t FusedPressureWorkspace::frequencyCount() const noexcept {
  return frequencyCount_;
}

std::span<std::complex<double>> FusedPressureWorkspace::cell(
    std::size_t rangeIndex, std::size_t depthIndex) {
  return std::span<std::complex<double>>(
      pressure_.data() + cellOffset(rangeIndex, depthIndex), frequencyCount_);
}

std::span<const std::complex<double>> FusedPressureWorkspace::cell(
    std::size_t rangeIndex, std::size_t depthIndex) const {
  return std::span<const std::complex<double>>(
      pressure_.data() + cellOffset(rangeIndex, depthIndex), frequencyCount_);
}

FrequencyWorkspace FusedPressureWorkspace::materializeFrequency(
    std::size_t frequencyIndex, double frequency,
    const ReceiverGrid& receivers) const {
  if (frequencyIndex >= frequencyCount_) {
    throw std::out_of_range(
        "fused pressure-workspace frequency index is out of range");
  }
  if (receivers.rangeCount() != rangeCount_ ||
      receivers.receiversPerRange() != depthCount_) {
    throw ValidationError(
        "fused pressure workspace and receiver-grid sizes must match");
  }

  FrequencyWorkspace workspace(frequency, receivers);
  std::span<std::complex<double>> destination = workspace.pressure();
  for (std::size_t depthIndex = 0U; depthIndex < depthCount_; ++depthIndex) {
    for (std::size_t rangeIndex = 0U; rangeIndex < rangeCount_; ++rangeIndex) {
      destination[depthIndex * rangeCount_ + rangeIndex] =
          cell(rangeIndex, depthIndex)[frequencyIndex];
    }
  }
  return workspace;
}

std::size_t FusedPressureWorkspace::cellOffset(
    std::size_t rangeIndex, std::size_t depthIndex) const {
  if (rangeIndex >= rangeCount_ || depthIndex >= depthCount_) {
    throw std::out_of_range("fused pressure-workspace index is out of range");
  }
  return ((rangeIndex * depthCount_) + depthIndex) * frequencyCount_;
}

}  // namespace rayreuse
