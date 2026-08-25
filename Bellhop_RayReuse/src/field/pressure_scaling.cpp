#include "rayreuse/field/pressure_scaling.hpp"

#include <cmath>
#include <complex>
#include <string>
#include <vector>

#include "rayreuse/error.hpp"

namespace rayreuse {
namespace {

void requireFinite(double value, const std::string& name) {
  if (!std::isfinite(value)) {
    throw ValidationError(name + " must be finite");
  }
}

void requireFiniteComplex(std::complex<double> value, const std::string& name) {
  if (!std::isfinite(value.real()) || !std::isfinite(value.imag())) {
    throw ValidationError(name + " must be finite");
  }
}

}  // namespace

void scaleCoherentCartesianPointPressure(FrequencyWorkspace& workspace,
                                         const ReceiverGrid& receivers,
                                         double launchAngleSpacingRadians,
                                         double sourceSoundSpeed) {
  requireFinite(launchAngleSpacingRadians, "launch-angle spacing");
  if (launchAngleSpacingRadians <= 0.0) {
    throw ValidationError("launch-angle spacing must be positive");
  }
  requireFinite(sourceSoundSpeed, "source sound speed");
  if (sourceSoundSpeed <= 0.0) {
    throw ValidationError("source sound speed must be positive");
  }
  if (workspace.depthCount() != receivers.depthCount() ||
      workspace.rangeCount() != receivers.rangeCount()) {
    throw ValidationError(
        "pressure-scaling workspace and receiver-grid sizes must match");
  }
  for (const std::complex<double> pressure : workspace.pressure()) {
    requireFiniteComplex(pressure, "unscaled workspace pressure");
  }

  const double beamScale =
      (-launchAngleSpacingRadians * std::sqrt(workspace.frequency())) /
      sourceSoundSpeed;
  requireFinite(beamScale, "Cartesian point-source beam scale");
  if (beamScale == 0.0) {
    throw ValidationError(
        "Cartesian point-source beam scale must not underflow to zero");
  }

  std::vector<double> rangeFactors;
  rangeFactors.reserve(receivers.rangeCount());
  for (const double range : receivers.ranges()) {
    const double factor =
        range == 0.0 ? 0.0 : beamScale / std::sqrt(std::abs(range));
    requireFinite(factor, "Cartesian point-source range factor");
    if (range != 0.0 && factor == 0.0) {
      throw ValidationError(
          "Cartesian point-source range factor must not underflow to zero");
    }
    rangeFactors.push_back(factor);
  }

  // Validate the complete scaled field before modifying the workspace so a
  // numerical overflow cannot leave a partially scaled result.
  for (std::size_t rangeIndex = 0U; rangeIndex < workspace.rangeCount();
       ++rangeIndex) {
    for (std::size_t depthIndex = 0U; depthIndex < workspace.depthCount();
         ++depthIndex) {
      requireFiniteComplex(
          workspace.at(depthIndex, rangeIndex) * rangeFactors[rangeIndex],
          "scaled workspace pressure");
    }
  }

  for (std::size_t rangeIndex = 0U; rangeIndex < workspace.rangeCount();
       ++rangeIndex) {
    for (std::size_t depthIndex = 0U; depthIndex < workspace.depthCount();
         ++depthIndex) {
      workspace.at(depthIndex, rangeIndex) *= rangeFactors[rangeIndex];
    }
  }
}

FrequencyWorkspace scaleCartesianPointIntensityToPressure(
    const IntensityWorkspace& workspace, const ReceiverGrid& receivers,
    double launchAngleSpacingRadians, double sourceSoundSpeed) {
  if (workspace.depthCount() != receivers.depthCount() ||
      workspace.rangeCount() != receivers.rangeCount()) {
    throw ValidationError(
        "intensity-scaling workspace and receiver-grid sizes must match");
  }
  for (const double intensity : workspace.intensity()) {
    if (!std::isfinite(intensity) || intensity < 0.0) {
      throw ValidationError(
          "unscaled workspace intensity must be finite and non-negative");
    }
  }

  // Keep the strong intensity input separate. Origin takes SQRT(intensity)
  // first and then applies the ordinary Cartesian point-source pressure
  // scale; it does not reinterpret a coherent pressure field as power.
  FrequencyWorkspace pressure(workspace.frequency(), receivers);
  for (std::size_t rangeIndex = 0U; rangeIndex < workspace.rangeCount();
       ++rangeIndex) {
    for (std::size_t depthIndex = 0U; depthIndex < workspace.depthCount();
         ++depthIndex) {
      const double root = std::sqrt(workspace.at(depthIndex, rangeIndex));
      requireFinite(root, "square root of workspace intensity");
      pressure.at(depthIndex, rangeIndex) = {root, 0.0};
    }
  }
  scaleCoherentCartesianPointPressure(pressure, receivers,
                                      launchAngleSpacingRadians,
                                      sourceSoundSpeed);
  return pressure;
}

}  // namespace rayreuse
