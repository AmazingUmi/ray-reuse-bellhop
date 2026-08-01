#pragma once

#include "rayreuse/field/frequency_workspace.hpp"
#include "rayreuse/model/simulation_case.hpp"

namespace rayreuse {

void scaleCoherentCartesianPointPressure(FrequencyWorkspace& workspace,
                                         const ReceiverGrid& receivers,
                                         double launchAngleSpacingRadians,
                                         double sourceSoundSpeed);

}  // namespace rayreuse
