#include <bit>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <numbers>
#include <stdexcept>

#include "bellhop/field/arrival_workspace.hpp"
#include "bellhop/model/simulation_case.hpp"

namespace {

using bellhop::Arrival;
using bellhop::ArrivalCandidate;
using bellhop::ArrivalWorkspace;
using bellhop::ReceiverGrid;

constexpr double kFrequencyHz = 50.0;

void add(ArrivalWorkspace& workspace, double amplitude, double phase,
         double delayReal, double sourceAngle, double receiverAngle,
         std::int32_t top, std::int32_t bottom) {
  workspace.addCandidate(
      kFrequencyHz,
      ArrivalCandidate{.amplitude = amplitude,
                       .phaseRadians = phase,
                       .delaySeconds = {delayReal, 0.0},
                       .sourceDeclinationDegrees = sourceAngle,
                       .receiverDeclinationDegrees = receiverAngle,
                       .topBounceCount = top,
                       .bottomBounceCount = bottom},
      0U, 0U);
}

void emit(std::size_t id, const ArrivalWorkspace& workspace) {
  const auto arrivals = workspace.arrivalsAt(0U, 0U);
  std::cout << "SCENARIO " << id << ' ' << arrivals.size() << '\n';
  for (std::size_t index = 0U; index < arrivals.size(); ++index) {
    const Arrival& value = arrivals[index];
    std::cout << "ARRIVAL " << id << ' ' << (index + 1U) << ' '
              << std::bit_cast<std::int32_t>(value.amplitude) << ' '
              << std::bit_cast<std::int32_t>(value.phaseRadians) << ' '
              << std::bit_cast<std::int32_t>(value.delaySeconds.real()) << ' '
              << std::bit_cast<std::int32_t>(value.delaySeconds.imag()) << ' '
              << std::bit_cast<std::int32_t>(value.sourceDeclinationDegrees)
              << ' '
              << std::bit_cast<std::int32_t>(value.receiverDeclinationDegrees)
              << ' ' << value.topBounceCount << ' ' << value.bottomBounceCount
              << '\n';
  }
}

void runScenario(std::size_t id) {
  const std::size_t capacity = (id >= 11U && id <= 14U) ? 2U : 4U;
  ArrivalWorkspace workspace(kFrequencyHz, ReceiverGrid({10.0}, {100.0}),
                             capacity);
  const double omega = 2.0 * std::numbers::pi * kFrequencyHz;
  const double phaseTolerance = static_cast<double>(0.05F);
  switch (id) {
    case 1U:
      add(workspace, 1.0, 0.0, 0.0, 10.0, 20.0, 1, 2);
      break;
    case 2U:
      add(workspace, 1.0, 0.01, 0.0, 10.0, 20.0, 1, 2);
      add(workspace, 2.0, 0.02, 0.0001, 40.0, 50.0, 3, 4);
      break;
    case 3U:
      add(workspace, 1.0, 0.0, 0.0, 10.0, 20.0, 1, 2);
      add(workspace, 2.0, 0.20, 0.01, 11.0, 21.0, 3, 4);
      add(workspace, 3.0, 0.001, 0.000001, 12.0, 22.0, 5, 6);
      break;
    case 4U:
    case 5U:
    case 6U: {
      const double factor = id == 4U ? 0.049 : (id == 5U ? phaseTolerance : 0.051);
      add(workspace, 1.0, 0.0, 0.0, 10.0, 20.0, 1, 2);
      add(workspace, 2.0, 0.0, factor / omega, 30.0, 40.0, 3, 4);
      break;
    }
    case 7U:
    case 8U:
    case 9U: {
      const double phase = id == 7U ? 0.049 : (id == 8U ? phaseTolerance : 0.051);
      add(workspace, 1.0, 0.0, 0.0, 10.0, 20.0, 1, 2);
      add(workspace, 2.0, phase, 0.0, 30.0, 40.0, 3, 4);
      break;
    }
    case 10U:
      add(workspace, 1.0, 0.0, 0.0, 10.0, 20.0, 1, 2);
      add(workspace, -1.0, 0.0, 0.0, 30.0, 40.0, 3, 4);
      break;
    case 11U:
      add(workspace, 1.0, 0.0, 0.0, 10.0, 20.0, 1, 2);
      add(workspace, 1.0, 0.20, 0.01, 11.0, 21.0, 3, 4);
      add(workspace, 2.0, 0.40, 0.02, 12.0, 22.0, 5, 6);
      break;
    case 12U:
      add(workspace, 1.0, 0.0, 0.0, 10.0, 20.0, 1, 2);
      add(workspace, 2.0, 0.20, 0.01, 11.0, 21.0, 3, 4);
      add(workspace, 3.0, 0.40, 0.02, 12.0, 22.0, 5, 6);
      break;
    case 13U:
      add(workspace, 1.0, 0.0, 0.0, 10.0, 20.0, 1, 2);
      add(workspace, 2.0, 0.20, 0.01, 11.0, 21.0, 3, 4);
      add(workspace, 1.0, 0.40, 0.02, 12.0, 22.0, 5, 6);
      break;
    case 14U:
      add(workspace, 1.0, 0.0, 0.0, 10.0, 20.0, 1, 2);
      add(workspace, 2.0, 0.20, 0.01, 11.0, 21.0, 3, 4);
      add(workspace, 0.5, 0.40, 0.02, 12.0, 22.0, 5, 6);
      break;
    case 15U:
      break;
    default:
      throw std::logic_error("unknown fixed scenario");
  }
  emit(id, workspace);
}

}  // namespace

int main() {
  std::cout << "I8_ARRIVAL_ACCUMULATOR_PROBE_V1\n";
  for (std::size_t scenario = 1U; scenario <= 15U; ++scenario) {
    try {
      runScenario(scenario);
    } catch (const std::exception& error) {
      // Keep the fixed scenario stream complete so the validator can report
      // the exact production API contract mismatch without hiding later
      // capacity scenarios.
      std::cout << "ERROR " << scenario << ' ' << error.what() << '\n';
    }
  }
  return 0;
}
