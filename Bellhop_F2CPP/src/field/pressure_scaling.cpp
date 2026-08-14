#include "bellhop/field/pressure_scaling.hpp"

#include <cmath>
#include <complex>
#include <string>
#include <vector>

#include "bellhop/error.hpp"

namespace bellhop {
namespace {

void requireFinite(double value, const std::string& name) {
  if (!std::isfinite(value)) {
    throw ValidationError(name + " must be finite");
  }
}

void requireFiniteComplex(std::complex<double> value,
                          const std::string& name) {
  if (!std::isfinite(value.real()) ||
      !std::isfinite(value.imag())) {
    throw ValidationError(name + " must be finite");
  }
}

}  // namespace

void scaleCoherentPressure(
    FrequencyWorkspace& workspace, const ReceiverGrid& receivers,
    double launchAngleSpacingRadians, double sourceSoundSpeed,
    SourceGeometry sourceGeometry, PressureNormalization normalization) {
  requireFinite(
      launchAngleSpacingRadians, "launch-angle spacing");
  if (launchAngleSpacingRadians <= 0.0) {
    throw ValidationError(
        "launch-angle spacing must be positive");
  }
  requireFinite(sourceSoundSpeed, "source sound speed");
  if (sourceSoundSpeed <= 0.0) {
    throw ValidationError("source sound speed must be positive");
  }
  if (workspace.depthCount() != receivers.receiversPerRange() ||
      workspace.rangeCount() != receivers.rangeCount()) {
    throw ValidationError(
        "pressure-scaling workspace and receiver-grid sizes must match");
  }
  switch (sourceGeometry) {
    case SourceGeometry::Point:
    case SourceGeometry::Line:
      break;
    default:
      throw ValidationError("pressure-scaling source geometry is invalid");
  }
  switch (normalization) {
    case PressureNormalization::Cerveny:
    case PressureNormalization::Geometric:
      break;
    default:
      throw ValidationError("pressure normalization is invalid");
  }
  for (const std::complex<double> pressure : workspace.pressure()) {
    requireFiniteComplex(
        pressure, "unscaled workspace pressure");
  }

  const double beamScale =
      normalization == PressureNormalization::Cerveny
          ? (-launchAngleSpacingRadians *
             std::sqrt(workspace.frequency())) /
                sourceSoundSpeed
          : -1.0;
  requireFinite(beamScale, "beam scale");
  if (beamScale == 0.0) {
    throw ValidationError("beam scale must not underflow to zero");
  }

  // Origin declares pi as default REAL here, so the line-source prefix is
  // formed in binary32 before it is promoted and multiplied by the
  // binary64 Cartesian beam scale.
  constexpr float kLegacyPi = 3.14159265F;
  const float linePrefix = -4.0F * std::sqrt(kLegacyPi);

  std::vector<double> rangeFactors;
  rangeFactors.reserve(receivers.rangeCount());
  for (const double range : receivers.ranges()) {
    const double factor = sourceGeometry == SourceGeometry::Line
                              ? static_cast<double>(linePrefix) * beamScale
                              : (range == 0.0
                                     ? 0.0
                                     : beamScale /
                                           std::sqrt(std::abs(range)));
    requireFinite(factor, "Cartesian source range factor");
    if ((sourceGeometry == SourceGeometry::Line || range != 0.0) &&
        factor == 0.0) {
      throw ValidationError(
          "Cartesian source range factor must not underflow to zero");
    }
    rangeFactors.push_back(factor);
  }

  // Validate the complete scaled field before modifying the workspace so a
  // numerical overflow cannot leave a partially scaled result.
  for (std::size_t rangeIndex = 0U;
       rangeIndex < workspace.rangeCount(); ++rangeIndex) {
    for (std::size_t depthIndex = 0U;
         depthIndex < workspace.depthCount(); ++depthIndex) {
      requireFiniteComplex(
          workspace.at(depthIndex, rangeIndex) *
              rangeFactors[rangeIndex],
          "scaled workspace pressure");
    }
  }

  for (std::size_t rangeIndex = 0U;
       rangeIndex < workspace.rangeCount(); ++rangeIndex) {
    for (std::size_t depthIndex = 0U;
         depthIndex < workspace.depthCount(); ++depthIndex) {
      workspace.at(depthIndex, rangeIndex) *=
          rangeFactors[rangeIndex];
    }
  }
}

void scaleCoherentCartesianPressure(
    FrequencyWorkspace& workspace, const ReceiverGrid& receivers,
    double launchAngleSpacingRadians, double sourceSoundSpeed,
    SourceGeometry sourceGeometry) {
  scaleCoherentPressure(
      workspace, receivers, launchAngleSpacingRadians, sourceSoundSpeed,
      sourceGeometry, PressureNormalization::Cerveny);
}

void scaleCoherentCartesianPointPressure(
    FrequencyWorkspace& workspace, const ReceiverGrid& receivers,
    double launchAngleSpacingRadians, double sourceSoundSpeed) {
  scaleCoherentCartesianPressure(
      workspace, receivers, launchAngleSpacingRadians, sourceSoundSpeed,
      SourceGeometry::Point);
}

FrequencyWorkspace scaleIntensityToPressure(
    const IntensityWorkspace& workspace, const ReceiverGrid& receivers,
    double launchAngleSpacingRadians, double sourceSoundSpeed,
    SourceGeometry sourceGeometry, PressureNormalization normalization) {
  if (workspace.depthCount() != receivers.receiversPerRange() ||
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

  // Construct a separate pressure workspace so validation or scaling failure
  // cannot partially consume or reinterpret the strong intensity input.
  FrequencyWorkspace pressure(workspace.frequency(), receivers);
  for (std::size_t rangeIndex = 0U;
       rangeIndex < workspace.rangeCount(); ++rangeIndex) {
    for (std::size_t depthIndex = 0U;
         depthIndex < workspace.depthCount(); ++depthIndex) {
      const double root =
          std::sqrt(workspace.at(depthIndex, rangeIndex));
      requireFinite(root, "square root of workspace intensity");
      pressure.at(depthIndex, rangeIndex) = {root, 0.0};
    }
  }
  scaleCoherentPressure(
      pressure, receivers, launchAngleSpacingRadians, sourceSoundSpeed,
      sourceGeometry, normalization);
  return pressure;
}

FrequencyWorkspace scaleCartesianIntensityToPressure(
    const IntensityWorkspace& workspace, const ReceiverGrid& receivers,
    double launchAngleSpacingRadians, double sourceSoundSpeed,
    SourceGeometry sourceGeometry) {
  return scaleIntensityToPressure(
      workspace, receivers, launchAngleSpacingRadians, sourceSoundSpeed,
      sourceGeometry, PressureNormalization::Cerveny);
}

}  // namespace bellhop
