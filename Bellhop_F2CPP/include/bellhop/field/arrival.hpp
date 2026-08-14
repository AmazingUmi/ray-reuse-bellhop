#pragma once

#include <complex>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace bellhop {

// Origin's BellhopCore plans max(20,000,000 / receiverCellCount, 10) arrival
// records per receiver cell (bellhop.f90::ArrivalsStorage / MinNArr).
inline constexpr std::size_t kOriginArrivalStorageSlots = 20'000'000U;
inline constexpr std::size_t kOriginMinimumArrivalsPerCell = 10U;

// Binary64 calculation candidate produced by a geometric receiver
// contribution. Stored fields default to binary64 so contribution math never
// rounds before the AddArr float boundary.
struct ArrivalCandidate {
  double amplitude{};
  double phaseRadians{};
  std::complex<double> delaySeconds{};
  double sourceDeclinationDegrees{};
  double receiverDeclinationDegrees{};
  std::int32_t topBounceCount{};
  std::int32_t bottomBounceCount{};
};

// Origin-compatible stored record (ArrMod.f90::Arrival). Fields default to
// float / complex-float precision and the bounce counts are signed 32-bit
// integers, matching Origin's REAL / COMPLEX / INTEGER fields. The float
// conversion happens at the same boundary as SNGL/CMPLX in AddArr.
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

// Checked Origin capacity plan:
//   arrivalsPerCell = max(20,000,000 / receiverCellCount, 10)
// with checked arithmetic. The optional override replaces the per-cell
// capacity for component testing only; it must never become ENV/CLI syntax.
[[nodiscard]] ArrivalCapacityPlan planArrivalCapacity(
    std::size_t receiverCellCount,
    std::optional<std::size_t> arrivalsPerCellOverride = std::nullopt);

}  // namespace bellhop
