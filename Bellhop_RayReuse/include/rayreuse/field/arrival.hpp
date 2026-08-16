#pragma once

#include <complex>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace rayreuse {

inline constexpr std::size_t kOriginArrivalStorageSlots = 20'000'000U;
inline constexpr std::size_t kOriginMinimumArrivalsPerCell = 10U;

struct ArrivalCandidate {
  double amplitude{};
  double phaseRadians{};
  std::complex<double> delaySeconds{};
  double sourceDeclinationDegrees{};
  double receiverDeclinationDegrees{};
  std::int32_t topBounceCount{};
  std::int32_t bottomBounceCount{};
};

struct Arrival {
  float amplitude{};
  float phaseRadians{};
  std::complex<float> delaySeconds{};
  float sourceDeclinationDegrees{};
  float receiverDeclinationDegrees{};
  std::int32_t topBounceCount{};
  std::int32_t bottomBounceCount{};
};

struct ArrivalCapacityPlan {
  std::size_t receiverCellCount{};
  std::size_t arrivalsPerCell{};
  std::size_t logicalSlotCount{};
};

[[nodiscard]] ArrivalCapacityPlan planArrivalCapacity(
    std::size_t receiverCellCount,
    std::optional<std::size_t> arrivalsPerCellOverride = std::nullopt);

}  // namespace rayreuse
