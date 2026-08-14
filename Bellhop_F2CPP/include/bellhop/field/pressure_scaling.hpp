#pragma once

#include "bellhop/field/frequency_workspace.hpp"
#include "bellhop/model/simulation_case.hpp"

namespace bellhop {

enum class PressureNormalization {
  Cerveny,
  Geometric,
};

void scaleCoherentCartesianPointPressure(
    FrequencyWorkspace& workspace, const ReceiverGrid& receivers,
    double launchAngleSpacingRadians, double sourceSoundSpeed);

void scaleCoherentCartesianPressure(
    FrequencyWorkspace& workspace, const ReceiverGrid& receivers,
    double launchAngleSpacingRadians, double sourceSoundSpeed,
    SourceGeometry sourceGeometry);

void scaleCoherentPressure(
    FrequencyWorkspace& workspace, const ReceiverGrid& receivers,
    double launchAngleSpacingRadians, double sourceSoundSpeed,
    SourceGeometry sourceGeometry, PressureNormalization normalization);

[[nodiscard]] FrequencyWorkspace scaleCartesianIntensityToPressure(
    const IntensityWorkspace& workspace, const ReceiverGrid& receivers,
    double launchAngleSpacingRadians, double sourceSoundSpeed,
    SourceGeometry sourceGeometry);

[[nodiscard]] FrequencyWorkspace scaleIntensityToPressure(
    const IntensityWorkspace& workspace, const ReceiverGrid& receivers,
    double launchAngleSpacingRadians, double sourceSoundSpeed,
    SourceGeometry sourceGeometry, PressureNormalization normalization);

}  // namespace bellhop
