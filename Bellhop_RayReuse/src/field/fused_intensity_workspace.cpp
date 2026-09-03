#include "rayreuse/field/fused_intensity_workspace.hpp"

#include <limits>
#include <stdexcept>

#include "rayreuse/error.hpp"

namespace rayreuse {
namespace {

[[nodiscard]] std::size_t checkedProduct(std::size_t lhs, std::size_t rhs) {
  if (lhs != 0U && rhs > std::numeric_limits<std::size_t>::max() / lhs) {
    throw ValidationError(
        "fused intensity-workspace dimensions overflow size_t");
  }
  return lhs * rhs;
}

}  // namespace

FusedIntensityWorkspace::FusedIntensityWorkspace(
    const ReceiverGrid& receivers, std::size_t frequencyCount)
    : rangeCount_(receivers.rangeCount()),
      depthCount_(receivers.receiversPerRange()),
      frequencyCount_(frequencyCount),
      intensity_(checkedProduct(checkedProduct(rangeCount_, depthCount_),
                                frequencyCount_)) {
  if (rangeCount_ == 0U || depthCount_ == 0U || frequencyCount_ == 0U) {
    throw ValidationError(
        "fused intensity workspace requires non-empty dimensions");
  }
}

std::size_t FusedIntensityWorkspace::rangeCount() const noexcept {
  return rangeCount_;
}

std::size_t FusedIntensityWorkspace::depthCount() const noexcept {
  return depthCount_;
}

std::size_t FusedIntensityWorkspace::frequencyCount() const noexcept {
  return frequencyCount_;
}

std::span<double> FusedIntensityWorkspace::cell(std::size_t rangeIndex,
                                                std::size_t depthIndex) {
  return std::span<double>(
      intensity_.data() + cellOffset(rangeIndex, depthIndex), frequencyCount_);
}

std::span<const double> FusedIntensityWorkspace::cell(
    std::size_t rangeIndex, std::size_t depthIndex) const {
  return std::span<const double>(
      intensity_.data() + cellOffset(rangeIndex, depthIndex), frequencyCount_);
}

IntensityWorkspace FusedIntensityWorkspace::materializeIntensityFrequency(
    std::size_t frequencyIndex, double frequency,
    const ReceiverGrid& receivers) const {
  if (frequencyIndex >= frequencyCount_) {
    throw std::out_of_range(
        "fused intensity-workspace frequency index is out of range");
  }
  if (receivers.rangeCount() != rangeCount_ ||
      receivers.receiversPerRange() != depthCount_) {
    throw ValidationError(
        "fused intensity workspace and receiver-grid sizes must match");
  }

  IntensityWorkspace workspace(frequency, receivers);
  // Legacy IntensityWorkspace layout is depth-major
  // (flatIndex = depthIndex * rangeCount + rangeIndex), the same mapping the
  // pressure twin materializes into FrequencyWorkspace::pressure().
  std::span<double> destination = workspace.intensity_;
  for (std::size_t depthIndex = 0U; depthIndex < depthCount_; ++depthIndex) {
    for (std::size_t rangeIndex = 0U; rangeIndex < rangeCount_; ++rangeIndex) {
      destination[depthIndex * rangeCount_ + rangeIndex] =
          cell(rangeIndex, depthIndex)[frequencyIndex];
    }
  }
  return workspace;
}

std::size_t FusedIntensityWorkspace::cellOffset(
    std::size_t rangeIndex, std::size_t depthIndex) const {
  if (rangeIndex >= rangeCount_ || depthIndex >= depthCount_) {
    throw std::out_of_range("fused intensity-workspace index is out of range");
  }
  return ((rangeIndex * depthCount_) + depthIndex) * frequencyCount_;
}

}  // namespace rayreuse
