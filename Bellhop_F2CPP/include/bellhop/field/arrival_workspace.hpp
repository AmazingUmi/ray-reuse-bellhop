#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <vector>

#include "bellhop/field/arrival.hpp"
#include "bellhop/model/simulation_case.hpp"

namespace bellhop {

// One source and one frequency. Receiver cells follow the ReceiverGrid flat
// order: receiver depth is the outer dimension and range is the inner
// dimension for rectilinear grids; an irregular grid has one accumulation cell
// per range. Cell storage is lazy: constructing the workspace allocates zero
// Arrival records.
class ArrivalWorkspace {
 public:
  ArrivalWorkspace(double frequency, const ReceiverGrid& receivers,
                   std::optional<std::size_t> capacityOverride = std::nullopt);

  [[nodiscard]] double frequency() const noexcept;
  [[nodiscard]] std::size_t depthCount() const noexcept;
  [[nodiscard]] std::size_t rangeCount() const noexcept;
  [[nodiscard]] std::size_t receiverCellCount() const noexcept;
  [[nodiscard]] const ArrivalCapacityPlan& capacity() const noexcept;

  [[nodiscard]] std::size_t flatIndex(std::size_t depthIndex,
                                      std::size_t rangeIndex) const;
  [[nodiscard]] std::span<const Arrival> cellAt(std::size_t cellIndex) const;
  [[nodiscard]] std::span<const Arrival> arrivalsAt(
      std::size_t depthIndex, std::size_t rangeIndex) const;
  [[nodiscard]] std::size_t arrivalCountAt(std::size_t depthIndex,
                                           std::size_t rangeIndex) const;

 private:
  double frequency_;
  std::size_t depthCount_;
  std::size_t rangeCount_;
  ArrivalCapacityPlan capacity_;
  std::vector<std::vector<Arrival>> cells_;
};

}  // namespace bellhop
