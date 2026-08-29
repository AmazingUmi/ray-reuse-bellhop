#pragma once

#include "rayreuse/field/frequency_workspace.hpp"
#include "rayreuse/model/simulation_case.hpp"

namespace rayreuse {

void scaleCoherentCartesianPressure(
    FrequencyWorkspace& workspace, const ReceiverGrid& receivers,
    double launchAngleSpacingRadians, double sourceSoundSpeed,
    SourceGeometry sourceGeometry = SourceGeometry::Point);

void scaleCoherentGeometricPressure(
    FrequencyWorkspace& workspace, const ReceiverGrid& receivers,
    double launchAngleSpacingRadians, double sourceSoundSpeed,
    SourceGeometry sourceGeometry = SourceGeometry::Point);

[[nodiscard]] FrequencyWorkspace scaleCartesianIntensityToPressure(
    const IntensityWorkspace& workspace, const ReceiverGrid& receivers,
    double launchAngleSpacingRadians, double sourceSoundSpeed,
    SourceGeometry sourceGeometry = SourceGeometry::Point);

[[nodiscard]] FrequencyWorkspace scaleGeometricIntensityToPressure(
    const IntensityWorkspace& workspace, const ReceiverGrid& receivers,
    double launchAngleSpacingRadians, double sourceSoundSpeed,
    SourceGeometry sourceGeometry = SourceGeometry::Point);

void scaleCoherentCartesianPointPressure(FrequencyWorkspace& workspace,
                                         const ReceiverGrid& receivers,
                                         double launchAngleSpacingRadians,
                                         double sourceSoundSpeed);

void scaleCoherentGeometricPointPressure(FrequencyWorkspace& workspace,
                                         const ReceiverGrid& receivers,
                                         double launchAngleSpacingRadians,
                                         double sourceSoundSpeed);

[[nodiscard]] FrequencyWorkspace scaleCartesianPointIntensityToPressure(
    const IntensityWorkspace& workspace, const ReceiverGrid& receivers,
    double launchAngleSpacingRadians, double sourceSoundSpeed);

[[nodiscard]] FrequencyWorkspace scaleGeometricPointIntensityToPressure(
    const IntensityWorkspace& workspace, const ReceiverGrid& receivers,
    double launchAngleSpacingRadians, double sourceSoundSpeed);

}  // namespace rayreuse
