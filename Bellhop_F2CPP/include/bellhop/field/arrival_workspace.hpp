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

  // Adds one candidate in encounter order (Origin ArrMod.f90::AddArr). The
  // candidate must carry the workspace frequency and every field is validated
  // before any cell mutation. Grouping with the last record, weighted float
  // merge, the axial-cusp guard, and bounded strongest-arrival retention
  // follow AddArr exactly.
  void addCandidate(double frequency, const ArrivalCandidate& candidate,
                    std::size_t depthIndex, std::size_t rangeIndex);

  [[nodiscard]] std::size_t candidateCount() const noexcept;
  [[nodiscard]] std::size_t appendCount() const noexcept;
  [[nodiscard]] std::size_t mergeCount() const noexcept;
  [[nodiscard]] std::size_t cuspGuardCount() const noexcept;
  [[nodiscard]] std::size_t weakestReplacementCount() const noexcept;
  [[nodiscard]] std::size_t capacityDiscardCount() const noexcept;
  // Number of distinct cells that have reached their per-cell capacity.
  [[nodiscard]] std::size_t saturatedCellCount() const noexcept;

 private:
  [[nodiscard]] double omega() const noexcept;

  double frequency_;
  std::size_t depthCount_;
  std::size_t rangeCount_;
  ArrivalCapacityPlan capacity_;
  std::vector<std::vector<Arrival>> cells_;
  std::size_t candidateCount_{};
  std::size_t appendCount_{};
  std::size_t mergeCount_{};
  std::size_t cuspGuardCount_{};
  std::size_t weakestReplacementCount_{};
  std::size_t capacityDiscardCount_{};
  std::size_t saturatedCellCount_{};
};

}  // namespace bellhop
