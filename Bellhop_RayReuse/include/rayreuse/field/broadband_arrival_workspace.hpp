#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <vector>

#include "rayreuse/field/arrival_accumulator.hpp"
#include "rayreuse/field/arrival.hpp"
#include "rayreuse/model/simulation_case.hpp"

namespace rayreuse {

struct BroadbandArrivalStorageStatistics {
  std::size_t laneCount{};
  std::size_t nonEmptyLaneCount{};
  std::size_t storedArrivalCount{};
  std::size_t allocatedArrivalSlots{};
  std::size_t logicalArrivalSlots{};
  std::size_t laneHeaderBytes{};
  std::size_t allocatedArrivalBytes{};
  std::size_t memoryFootprintBytes{};
};

// Source-local fused Arrival destination. Each vector owns the ordered Arrival
// sequence for one receiver/frequency lane. The lane headers use the logical
// [range][depth][frequency] layout so all frequency lanes at a fixed receiver
// cell are adjacent:
//   ((range * depthCount) + depth) * frequencyCount + frequency.
class BroadbandArrivalWorkspace {
 public:
  class FrequencyView {
   public:
    [[nodiscard]] double frequency() const noexcept;
    [[nodiscard]] std::size_t depthCount() const noexcept;
    [[nodiscard]] std::size_t rangeCount() const noexcept;
    [[nodiscard]] std::size_t receiverCellCount() const noexcept;
    [[nodiscard]] const ArrivalCapacityPlan& capacity() const noexcept;
    [[nodiscard]] std::size_t flatIndex(std::size_t depthIndex,
                                        std::size_t rangeIndex) const;
    [[nodiscard]] std::span<const Arrival> cellAt(
        std::size_t cellIndex) const;
    [[nodiscard]] std::span<const Arrival> arrivalsAt(
        std::size_t depthIndex, std::size_t rangeIndex) const;
    [[nodiscard]] std::size_t arrivalCountAt(
        std::size_t depthIndex, std::size_t rangeIndex) const;

   private:
    friend class BroadbandArrivalWorkspace;
    FrequencyView(const BroadbandArrivalWorkspace& workspace,
                  std::size_t frequencyIndex) noexcept;

    const BroadbandArrivalWorkspace* workspace_{};
    std::size_t frequencyIndex_{};
  };

  BroadbandArrivalWorkspace(
      std::span<const double> frequencies, const ReceiverGrid& receivers,
      std::optional<std::size_t> capacityOverride = std::nullopt);

  [[nodiscard]] std::size_t rangeCount() const noexcept;
  [[nodiscard]] std::size_t depthCount() const noexcept;
  [[nodiscard]] std::size_t frequencyCount() const noexcept;
  [[nodiscard]] std::size_t receiverCellCount() const noexcept;
  [[nodiscard]] std::size_t laneCount() const noexcept;
  [[nodiscard]] double frequency(std::size_t frequencyIndex) const;
  [[nodiscard]] const ArrivalCapacityPlan& capacity() const noexcept;

  void addCandidate(std::size_t frequencyIndex,
                    const ArrivalCandidate& candidate,
                    std::size_t depthIndex, std::size_t rangeIndex,
                    ArrivalAccumulationStatistics& localStatistics);

  // Narrow mutable seam for the shared AddArr primitive introduced by B02.
  // Static range workers may mutate only lanes in their exclusively owned
  // range block.
  [[nodiscard]] std::vector<Arrival>& laneAt(std::size_t rangeIndex,
                                             std::size_t depthIndex,
                                             std::size_t frequencyIndex);
  [[nodiscard]] std::span<const Arrival> laneAt(
      std::size_t rangeIndex, std::size_t depthIndex,
      std::size_t frequencyIndex) const;

  // Read-only, zero-copy projection using the legacy ArrivalWorkspace cell
  // traversal contract (depth-major flat cell indexing).
  [[nodiscard]] FrequencyView frequencyView(
      std::size_t frequencyIndex) const;

  [[nodiscard]] BroadbandArrivalStorageStatistics storageStatistics() const;

 private:
  [[nodiscard]] std::size_t laneIndex(std::size_t rangeIndex,
                                      std::size_t depthIndex,
                                      std::size_t frequencyIndex) const;

  std::size_t rangeCount_{};
  std::size_t depthCount_{};
  std::vector<double> frequencies_;
  ArrivalCapacityPlan capacity_;
  std::size_t logicalArrivalSlots_{};
  std::vector<std::vector<Arrival>> lanes_;
};

}  // namespace rayreuse
