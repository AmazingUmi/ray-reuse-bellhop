#pragma once

#include "rayreuse/model/environment.hpp"
#include "rayreuse/ray/geometry_tracer.hpp"

namespace rayreuse::test {

inline constexpr double kMunkOracleLaunchAngle = -3.54656494649486695e-4;
inline constexpr double kMunkExtremeLaunchAngle = 3.54301838154848892e-1;

inline Environment makeMunkEnvironment() {
  return Environment(
      SoundSpeedProfile(
          {{.depth = 0.0, .soundSpeed = 1548.52, .density = 1000.0},
           {.depth = 200.0, .soundSpeed = 1530.29, .density = 1000.0},
           {.depth = 250.0, .soundSpeed = 1526.69, .density = 1000.0},
           {.depth = 400.0, .soundSpeed = 1517.78, .density = 1000.0},
           {.depth = 600.0, .soundSpeed = 1509.49, .density = 1000.0},
           {.depth = 800.0, .soundSpeed = 1504.30, .density = 1000.0},
           {.depth = 1000.0, .soundSpeed = 1501.38, .density = 1000.0},
           {.depth = 1200.0, .soundSpeed = 1500.14, .density = 1000.0},
           {.depth = 1400.0, .soundSpeed = 1500.12, .density = 1000.0},
           {.depth = 1600.0, .soundSpeed = 1501.02, .density = 1000.0},
           {.depth = 1800.0, .soundSpeed = 1502.57, .density = 1000.0},
           {.depth = 2000.0, .soundSpeed = 1504.62, .density = 1000.0},
           {.depth = 2200.0, .soundSpeed = 1507.02, .density = 1000.0},
           {.depth = 2400.0, .soundSpeed = 1509.69, .density = 1000.0},
           {.depth = 2600.0, .soundSpeed = 1512.55, .density = 1000.0},
           {.depth = 2800.0, .soundSpeed = 1515.56, .density = 1000.0},
           {.depth = 3000.0, .soundSpeed = 1518.67, .density = 1000.0},
           {.depth = 3200.0, .soundSpeed = 1521.85, .density = 1000.0},
           {.depth = 3400.0, .soundSpeed = 1525.10, .density = 1000.0},
           {.depth = 3600.0, .soundSpeed = 1528.38, .density = 1000.0},
           {.depth = 3800.0, .soundSpeed = 1531.70, .density = 1000.0},
           {.depth = 4000.0, .soundSpeed = 1535.04, .density = 1000.0},
           {.depth = 4200.0, .soundSpeed = 1538.39, .density = 1000.0},
           {.depth = 4400.0, .soundSpeed = 1541.76, .density = 1000.0},
           {.depth = 4600.0, .soundSpeed = 1545.14, .density = 1000.0},
           {.depth = 4800.0, .soundSpeed = 1548.52, .density = 1000.0},
           {.depth = 5000.0, .soundSpeed = 1551.91, .density = 1000.0}}),
      BoundaryModel::vacuum(0.0),
      BoundaryModel::acousticHalfSpace(
          5000.0, AcousticMaterial{
                      .compressionalSoundSpeed = 1600.0,
                      .shearSoundSpeed = 0.0,
                      .density = 1800.0,
                      .compressionalAttenuation = {
                          .value = 0.8,
                          .unit = AttenuationUnit::DecibelsPerWavelength}}));
}

inline constexpr IntegratorSettings makeMunkIntegratorSettings() {
  return IntegratorSettings{.stepLength = 500.0,
                            .rangeLimit = 101000.0,
                            .depthLimit = 5500.0,
                            .maximumRayPoints = 10000U};
}

}  // namespace rayreuse::test
