#pragma once

#include <cstddef>
#include <span>
#include <vector>

#include "rayreuse/field/frequency_workspace.hpp"
#include "rayreuse/model/simulation_case.hpp"

namespace rayreuse {

// Fused-only incoherent/semi-coherent intensity destination (IGR-3A design
// §6.2): real-payload twin of FusedPressureWorkspace with the same flat
// [range][depth][frequency] layout. At a fixed receiver (range, depth),
// every frequency lane is contiguous:
//   ((range * depthCount) + depth) * frequencyCount + frequency.
class FusedIntensityWorkspace {
 public:
  FusedIntensityWorkspace(const ReceiverGrid& receivers,
                          std::size_t frequencyCount);

  [[nodiscard]] std::size_t rangeCount() const noexcept;
  [[nodiscard]] std::size_t depthCount() const noexcept;
  [[nodiscard]] std::size_t frequencyCount() const noexcept;
  [[nodiscard]] std::span<double> cell(std::size_t rangeIndex,
                                       std::size_t depthIndex);
  [[nodiscard]] std::span<const double> cell(std::size_t rangeIndex,
                                             std::size_t depthIndex) const;

  // Bitwise value copy only: no scaling, reduction, or reassociation.
  [[nodiscard]] IntensityWorkspace materializeIntensityFrequency(
      std::size_t frequencyIndex, double frequency,
      const ReceiverGrid& receivers) const;

 private:
  [[nodiscard]] std::size_t cellOffset(std::size_t rangeIndex,
                                       std::size_t depthIndex) const;

  std::size_t rangeCount_{};
  std::size_t depthCount_{};
  std::size_t frequencyCount_{};
  std::vector<double> intensity_;
};

}  // namespace rayreuse
