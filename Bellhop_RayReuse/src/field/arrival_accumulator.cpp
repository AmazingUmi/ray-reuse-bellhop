#include "rayreuse/field/arrival_accumulator.hpp"

#include <cmath>
#include <limits>

#include "rayreuse/error.hpp"

namespace rayreuse {
namespace {

constexpr double kArrivalPhaseTolerance = static_cast<double>(0.05F);

[[nodiscard]] Arrival storedFromCandidate(
    const ArrivalCandidate& candidate) {
  return Arrival{static_cast<float>(candidate.amplitude),
                 static_cast<float>(candidate.phaseRadians),
                 static_cast<std::complex<float>>(candidate.delaySeconds),
                 static_cast<float>(candidate.sourceDeclinationDegrees),
                 static_cast<float>(candidate.receiverDeclinationDegrees),
                 candidate.topBounceCount,
                 candidate.bottomBounceCount};
}

}  // namespace

void validateArrivalCandidate(const ArrivalCandidate& candidate) {
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

void addArrivalCandidate(std::vector<Arrival>& lane,
                         std::size_t arrivalsPerCell, double omega,
                         const ArrivalCandidate& candidate,
                         ArrivalAccumulationStatistics& statistics) {
  if (arrivalsPerCell == 0U) {
    throw ValidationError("arrival lane capacity must be positive");
  }
  if (!std::isfinite(omega) || omega <= 0.0) {
    throw ValidationError("arrival angular frequency must be positive and finite");
  }
  const std::size_t count = lane.size();
  bool groups = false;
  if (count != 0U) {
    const Arrival& last = lane.back();
    const auto delayDifference =
        candidate.delaySeconds -
        static_cast<std::complex<double>>(last.delaySeconds);
    groups = omega * std::abs(delayDifference) < kArrivalPhaseTolerance &&
             std::abs(static_cast<double>(last.phaseRadians) -
                      candidate.phaseRadians) < kArrivalPhaseTolerance;
  }

  ++statistics.candidateCount;
  if (groups) {
    Arrival& record = lane.back();
    const float candidateAmplitude = static_cast<float>(candidate.amplitude);
    const float ampTot = record.amplitude + candidateAmplitude;
    if (std::abs(ampTot) <= std::numeric_limits<float>::epsilon()) {
      ++statistics.cuspGuardCount;
      return;
    }
    ++statistics.mergeCount;
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

  if (count >= arrivalsPerCell) {
    std::size_t minimumIndex = 0U;
    for (std::size_t i = 1U; i < count; ++i) {
      if (lane[i].amplitude < lane[minimumIndex].amplitude) minimumIndex = i;
    }
    if (candidate.amplitude > lane[minimumIndex].amplitude) {
      lane[minimumIndex] = storedFromCandidate(candidate);
      ++statistics.weakestReplacementCount;
    } else {
      ++statistics.capacityDiscardCount;
    }
    return;
  }

  lane.push_back(storedFromCandidate(candidate));
  ++statistics.appendCount;
  if (lane.size() == arrivalsPerCell) ++statistics.saturatedCellCount;
}

void mergeArrivalAccumulationStatistics(
    ArrivalAccumulationStatistics& total,
    const ArrivalAccumulationStatistics& value) noexcept {
  total.candidateCount += value.candidateCount;
  total.appendCount += value.appendCount;
  total.mergeCount += value.mergeCount;
  total.cuspGuardCount += value.cuspGuardCount;
  total.weakestReplacementCount += value.weakestReplacementCount;
  total.capacityDiscardCount += value.capacityDiscardCount;
  total.saturatedCellCount += value.saturatedCellCount;
}

}  // namespace rayreuse
