#pragma once

#include "bellhop/field/frequency_workspace.hpp"
#include "bellhop/model/simulation_case.hpp"

namespace bellhop {

void scaleCoherentCartesianPointPressure(
    FrequencyWorkspace& workspace, const ReceiverGrid& receivers,
    double launchAngleSpacingRadians, double sourceSoundSpeed);

}  // namespace bellhop
