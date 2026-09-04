#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <vector>

#include "rayreuse/field/arrival_accumulator.hpp"
#include "rayreuse/field/arrival.hpp"
#include "rayreuse/model/simulation_case.hpp"

namespace rayreuse {

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

  void addCandidate(double frequency, const ArrivalCandidate& candidate,
                    std::size_t depthIndex, std::size_t rangeIndex);

  [[nodiscard]] std::size_t candidateCount() const noexcept;
  [[nodiscard]] std::size_t appendCount() const noexcept;
  [[nodiscard]] std::size_t mergeCount() const noexcept;
  [[nodiscard]] std::size_t cuspGuardCount() const noexcept;
  [[nodiscard]] std::size_t weakestReplacementCount() const noexcept;
  [[nodiscard]] std::size_t capacityDiscardCount() const noexcept;
  [[nodiscard]] std::size_t saturatedCellCount() const noexcept;

 private:
  [[nodiscard]] double omega() const noexcept;

  double frequency_;
  std::size_t depthCount_;
  std::size_t rangeCount_;
  ArrivalCapacityPlan capacity_;
  std::vector<std::vector<Arrival>> cells_;
  ArrivalAccumulationStatistics statistics_;
};

}  // namespace rayreuse
