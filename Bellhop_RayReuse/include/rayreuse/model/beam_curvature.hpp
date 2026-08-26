#pragma once

namespace rayreuse {

// ReflectMod.f90 allows the complete dynamic-ray reflection jump to be
// doubled, retained, or suppressed. This is a Cerveny beam configuration,
// not a physical boundary property and not a frequency-local acoustic state.
enum class BoundaryCurvatureMode {
  Standard,
  Double,
  Zero,
};

}  // namespace rayreuse
