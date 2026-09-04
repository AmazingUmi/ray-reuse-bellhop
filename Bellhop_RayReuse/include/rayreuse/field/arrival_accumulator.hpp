#pragma once

#include <cstddef>
#include <vector>

#include "rayreuse/field/arrival.hpp"

namespace rayreuse {

struct ArrivalAccumulationStatistics {
  std::size_t candidateCount{};
  std::size_t appendCount{};
  std::size_t mergeCount{};
  std::size_t cuspGuardCount{};
  std::size_t weakestReplacementCount{};
  std::size_t capacityDiscardCount{};
  std::size_t saturatedCellCount{};
};

void validateArrivalCandidate(const ArrivalCandidate& candidate);

// Origin AddArr semantics for one ordered receiver/frequency lane. Statistics
// are supplied by the caller so a fused range worker can accumulate locally.
// The owning workspace validates the candidate before resolving the lane, which
// preserves legacy exception precedence.
void addArrivalCandidate(std::vector<Arrival>& lane,
                         std::size_t arrivalsPerCell, double omega,
                         const ArrivalCandidate& candidate,
                         ArrivalAccumulationStatistics& statistics);

void mergeArrivalAccumulationStatistics(
    ArrivalAccumulationStatistics& total,
    const ArrivalAccumulationStatistics& value) noexcept;

}  // namespace rayreuse
