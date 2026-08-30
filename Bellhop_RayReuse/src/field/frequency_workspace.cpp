#include "rayreuse/field/frequency_workspace.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

#include "rayreuse/error.hpp"

namespace rayreuse {
namespace {

std::size_t checkedProduct(std::size_t lhs, std::size_t rhs) {
  if (lhs != 0U && rhs > std::numeric_limits<std::size_t>::max() / lhs) {
    throw ValidationError("receiver-grid dimensions overflow size_t");
  }
  return lhs * rhs;
}

}  // namespace

FrequencyWorkspace::FrequencyWorkspace(double frequency,
                                       const ReceiverGrid& receivers)
    : frequency_(frequency),
      depthCount_(receivers.receiversPerRange()),
      rangeCount_(receivers.rangeCount()),
      pressure_(checkedProduct(depthCount_, rangeCount_)) {
  if (!std::isfinite(frequency_) || frequency_ <= 0.0) {
    throw ValidationError("workspace frequency must be positive and finite");
  }
}

double FrequencyWorkspace::frequency() const noexcept { return frequency_; }

std::size_t FrequencyWorkspace::depthCount() const noexcept {
  return depthCount_;
}

std::size_t FrequencyWorkspace::rangeCount() const noexcept {
  return rangeCount_;
}

std::span<std::complex<double>> FrequencyWorkspace::pressure() noexcept {
  return pressure_;
}

std::span<const std::complex<double>> FrequencyWorkspace::pressure()
    const noexcept {
  return pressure_;
}

std::complex<double>& FrequencyWorkspace::at(std::size_t depthIndex,
                                             std::size_t rangeIndex) {
  return pressure_.at(flatIndex(depthIndex, rangeIndex));
}

const std::complex<double>& FrequencyWorkspace::at(
    std::size_t depthIndex, std::size_t rangeIndex) const {
  return pressure_.at(flatIndex(depthIndex, rangeIndex));
}

void FrequencyWorkspace::clear() noexcept {
  std::fill(pressure_.begin(), pressure_.end(), std::complex<double>{});
}

std::size_t FrequencyWorkspace::flatIndex(std::size_t depthIndex,
                                          std::size_t rangeIndex) const {
  if (depthIndex >= depthCount_ || rangeIndex >= rangeCount_) {
    throw std::out_of_range("frequency workspace index is out of range");
  }
  return depthIndex * rangeCount_ + rangeIndex;
}

IntensityWorkspace::IntensityWorkspace(double frequency,
                                       const ReceiverGrid& receivers)
    : frequency_(frequency),
      depthCount_(receivers.receiversPerRange()),
      rangeCount_(receivers.rangeCount()),
      intensity_(checkedProduct(depthCount_, rangeCount_)) {
  if (!std::isfinite(frequency_) || frequency_ <= 0.0) {
    throw ValidationError(
        "intensity-workspace frequency must be positive and finite");
  }
}

double IntensityWorkspace::frequency() const noexcept { return frequency_; }

std::size_t IntensityWorkspace::depthCount() const noexcept {
  return depthCount_;
}

std::size_t IntensityWorkspace::rangeCount() const noexcept {
  return rangeCount_;
}

std::span<const double> IntensityWorkspace::intensity() const noexcept {
  return intensity_;
}

double IntensityWorkspace::at(std::size_t depthIndex,
                              std::size_t rangeIndex) const {
  return intensity_.at(flatIndex(depthIndex, rangeIndex));
}

void IntensityWorkspace::add(std::size_t depthIndex, std::size_t rangeIndex,
                             double contribution) {
  const std::size_t index = flatIndex(depthIndex, rangeIndex);
  if (!std::isfinite(contribution) || contribution < 0.0) {
    throw ValidationError(
        "intensity contribution must be finite and non-negative");
  }
  const double updated = intensity_[index] + contribution;
  if (!std::isfinite(updated)) {
    throw ValidationError("accumulated intensity must remain finite");
  }
  intensity_[index] = updated;
}

void IntensityWorkspace::clear() noexcept {
  std::fill(intensity_.begin(), intensity_.end(), 0.0);
}

std::size_t IntensityWorkspace::flatIndex(std::size_t depthIndex,
                                          std::size_t rangeIndex) const {
  if (depthIndex >= depthCount_ || rangeIndex >= rangeCount_) {
    throw std::out_of_range("intensity-workspace index is out of range");
  }
  return depthIndex * rangeCount_ + rangeIndex;
}

}  // namespace rayreuse
