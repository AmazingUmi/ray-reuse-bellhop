#include "rayreuse/model/simulation_case.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
#include <numbers>
#include <string>
#include <utility>

#include "rayreuse/error.hpp"
#include "rayreuse/model/c_linear_ssp.hpp"

namespace rayreuse {
namespace {

void requireFinite(double value, const std::string& name) {
  if (!std::isfinite(value)) {
    throw ValidationError(name + " must be finite");
  }
}

void validateStrictlyIncreasing(const std::vector<double>& values,
                                const std::string& name) {
  if (values.empty()) {
    throw ValidationError(name + " must not be empty");
  }
  for (std::size_t index = 0; index < values.size(); ++index) {
    requireFinite(values[index], name);
    if (index > 0U && values[index - 1U] >= values[index]) {
      throw ValidationError(name + " must be strictly increasing");
    }
  }
}

void validateRunMode(SimulationRunMode mode) {
  switch (mode) {
    case SimulationRunMode::Coherent:
    case SimulationRunMode::Incoherent:
    case SimulationRunMode::SemiCoherent:
    case SimulationRunMode::RayTrace:
    case SimulationRunMode::AsciiArrivals:
    case SimulationRunMode::BinaryArrivals:
    case SimulationRunMode::Eigenray:
      return;
  }
  throw ValidationError("simulation run mode is invalid");
}

void validateBeamFamily(BeamFamily family) {
  switch (family) {
    case BeamFamily::CervenyGaussian:
    case BeamFamily::GeometricHat:
    case BeamFamily::GeometricGaussian:
    case BeamFamily::SimpleGaussian:
      return;
  }
  throw ValidationError("beam family is invalid");
}

void validateFieldComponent(FieldComponent component) {
  switch (component) {
    case FieldComponent::Pressure:
    case FieldComponent::Vertical:
    case FieldComponent::Horizontal:
      return;
  }
  throw ValidationError("field component is invalid");
}

void validateCurvatureMode(BoundaryCurvatureMode mode) {
  switch (mode) {
    case BoundaryCurvatureMode::Standard:
    case BoundaryCurvatureMode::Double:
    case BoundaryCurvatureMode::Zero:
      return;
  }
  throw ValidationError("boundary curvature mode is invalid");
}

void validateBeamWidthMode(BeamWidthMode mode) {
  switch (mode) {
    case BeamWidthMode::SpaceFilling:
    case BeamWidthMode::MinimumWidth:
    case BeamWidthMode::Wkb:
      return;
  }
  throw ValidationError("beam width mode is invalid");
}

void validateCervenyCoordinateSystem(CervenyCoordinateSystem coordinates) {
  switch (coordinates) {
    case CervenyCoordinateSystem::Cartesian:
    case CervenyCoordinateSystem::RayCentered:
      return;
  }
  throw ValidationError("Cerveny coordinate system is invalid");
}

void validateRayCenteredReceiverRanges(const ReceiverGrid& receivers) {
  if (receivers.rangeCount() < 2U) {
    throw ValidationError(
        "ray-centered Cerveny requires at least two receiver ranges");
  }
  const std::vector<double>& ranges = receivers.ranges();
  const double delta = ranges[1U] - ranges[0U];
  for (std::size_t index = 2U; index < ranges.size(); ++index) {
    const double expected = ranges.front() + static_cast<double>(index) * delta;
    const double tolerance =
        32.0 * std::numeric_limits<double>::epsilon() *
        std::max({1.0, std::abs(expected), std::abs(ranges[index])});
    if (std::abs(ranges[index] - expected) > tolerance) {
      throw ValidationError(
          "ray-centered Cerveny receiver ranges must be equally spaced");
    }
  }
}

}  // namespace

bool isTransmissionLossMode(SimulationRunMode mode) {
  switch (mode) {
    case SimulationRunMode::Coherent:
    case SimulationRunMode::Incoherent:
    case SimulationRunMode::SemiCoherent:
      return true;
    case SimulationRunMode::RayTrace:
    case SimulationRunMode::AsciiArrivals:
    case SimulationRunMode::BinaryArrivals:
    case SimulationRunMode::Eigenray:
      return false;
  }
  throw ValidationError("simulation run mode is invalid");
}

FieldAccumulationKind fieldAccumulationKind(SimulationRunMode mode) {
  switch (mode) {
    case SimulationRunMode::Coherent:
      return FieldAccumulationKind::ComplexPressure;
    case SimulationRunMode::Incoherent:
    case SimulationRunMode::SemiCoherent:
      return FieldAccumulationKind::Intensity;
    case SimulationRunMode::RayTrace:
    case SimulationRunMode::AsciiArrivals:
    case SimulationRunMode::BinaryArrivals:
    case SimulationRunMode::Eigenray:
      return FieldAccumulationKind::None;
  }
  throw ValidationError("simulation run mode is invalid");
}

bool usesLloydMirror(SimulationRunMode mode) {
  switch (mode) {
    case SimulationRunMode::Coherent:
    case SimulationRunMode::Incoherent:
    case SimulationRunMode::RayTrace:
    case SimulationRunMode::AsciiArrivals:
    case SimulationRunMode::BinaryArrivals:
    case SimulationRunMode::Eigenray:
      return false;
    case SimulationRunMode::SemiCoherent:
      return true;
  }
  throw ValidationError("simulation run mode is invalid");
}

ReceiverGrid::ReceiverGrid(std::vector<double> depths,
                           std::vector<double> ranges)
    : depths_(std::move(depths)), ranges_(std::move(ranges)) {
  validateStrictlyIncreasing(depths_, "receiver depths");
  validateStrictlyIncreasing(ranges_, "receiver ranges");
  if (ranges_.front() < 0.0) {
    throw ValidationError("receiver ranges must be non-negative");
  }
}

const std::vector<double>& ReceiverGrid::depths() const noexcept {
  return depths_;
}

const std::vector<double>& ReceiverGrid::ranges() const noexcept {
  return ranges_;
}

std::size_t ReceiverGrid::depthCount() const noexcept { return depths_.size(); }

std::size_t ReceiverGrid::rangeCount() const noexcept { return ranges_.size(); }

FrequencyGrid::FrequencyGrid(std::vector<double> values)
    : values_(std::move(values)) {
  if (values_.empty()) {
    throw ValidationError("frequency grid must not be empty");
  }
  for (std::size_t index = 0U; index < values_.size(); ++index) {
    const double value = values_[index];
    requireFinite(value, "frequency");
    if (value <= 0.0) {
      throw ValidationError("frequencies must be positive");
    }
    if (index > 0U && values_[index - 1U] >= value) {
      throw ValidationError("frequency grid must be strictly increasing");
    }
  }
}

const std::vector<double>& FrequencyGrid::values() const noexcept {
  return values_;
}

std::size_t FrequencyGrid::size() const noexcept { return values_.size(); }

double FrequencyGrid::designFrequency() const noexcept {
  return values_.back();
}

SourceBeamPattern::SourceBeamPattern(std::vector<double> anglesDegrees,
                                     std::vector<double> amplitudes,
                                     bool directional)
    : anglesDegrees_(std::move(anglesDegrees)),
      amplitudes_(std::move(amplitudes)),
      directional_(directional) {}

SourceBeamPattern SourceBeamPattern::omnidirectional() {
  return SourceBeamPattern({-180.0, 180.0}, {1.0, 1.0}, false);
}

SourceBeamPattern SourceBeamPattern::directional(
    std::vector<SourceBeamPatternSample> samples) {
  if (samples.size() < 2U) {
    throw ValidationError("source beam pattern requires at least two samples");
  }
  std::vector<double> angles;
  std::vector<double> amplitudes;
  angles.reserve(samples.size());
  amplitudes.reserve(samples.size());
  for (const SourceBeamPatternSample& sample : samples) {
    requireFinite(sample.angleDegrees, "source beam pattern angle");
    requireFinite(sample.powerDecibels, "source beam pattern power");
    if (!angles.empty() && angles.back() >= sample.angleDegrees) {
      throw ValidationError(
          "source beam pattern angles must be strictly increasing");
    }
    const double amplitude = std::pow(10.0, sample.powerDecibels / 20.0);
    if (!std::isfinite(amplitude) || amplitude < 0.0) {
      throw ValidationError(
          "source beam pattern conversion produced an invalid amplitude");
    }
    angles.push_back(sample.angleDegrees);
    amplitudes.push_back(amplitude);
  }
  return SourceBeamPattern(std::move(angles), std::move(amplitudes), true);
}

double SourceBeamPattern::amplitudeForLaunchAngle(
    double launchAngleRadians) const {
  requireFinite(launchAngleRadians, "source beam pattern launch angle");
  const double angleDegrees = launchAngleRadians * (180.0 / std::numbers::pi);
  const auto upper = std::lower_bound(anglesDegrees_.begin(),
                                      anglesDegrees_.end(), angleDegrees);
  std::size_t leftIndex = 0U;
  if (upper == anglesDegrees_.end()) {
    leftIndex = anglesDegrees_.size() - 2U;
  } else if (upper != anglesDegrees_.begin()) {
    leftIndex = static_cast<std::size_t>(
        std::distance(anglesDegrees_.begin(), upper) - 1);
  }
  const double fraction =
      (angleDegrees - anglesDegrees_[leftIndex]) /
      (anglesDegrees_[leftIndex + 1U] - anglesDegrees_[leftIndex]);
  return (1.0 - fraction) * amplitudes_[leftIndex] +
         fraction * amplitudes_[leftIndex + 1U];
}

bool SourceBeamPattern::isDirectional() const noexcept { return directional_; }

std::size_t SourceBeamPattern::size() const noexcept {
  return anglesDegrees_.size();
}

double SourceBeamPattern::minimumAngleDegrees() const noexcept {
  return anglesDegrees_.front();
}

double SourceBeamPattern::maximumAngleDegrees() const noexcept {
  return anglesDegrees_.back();
}

SimulationCase::SimulationCase(Environment environment, Source source,
                               ReceiverGrid receivers,
                               FrequencyGrid frequencies, LaunchFan launchFan,
                               IntegratorSettings integrator,
                               SourceBeamPattern sourceBeamPattern,
                               SimulationRunMode runMode, BeamFamily beamFamily,
                               FieldComponent fieldComponent,
                               BoundaryCurvatureMode curvatureMode,
                               BeamWidthMode beamWidthMode,
                               CervenyCoordinateSystem cervenyCoordinateSystem)
    : environment_(std::move(environment)),
      source_(source),
      receivers_(std::move(receivers)),
      frequencies_(std::move(frequencies)),
      integrator_(integrator),
      sourceBeamPattern_(std::move(sourceBeamPattern)),
      runMode_(runMode),
      beamFamily_(beamFamily),
      fieldComponent_(fieldComponent),
      curvatureMode_(curvatureMode),
      beamWidthMode_(beamWidthMode),
      cervenyCoordinateSystem_(cervenyCoordinateSystem) {
  validateRunMode(runMode_);
  validateBeamFamily(beamFamily_);
  validateFieldComponent(fieldComponent_);
  validateCurvatureMode(curvatureMode_);
  validateBeamWidthMode(beamWidthMode_);
  validateCervenyCoordinateSystem(cervenyCoordinateSystem_);
  if (cervenyCoordinateSystem_ == CervenyCoordinateSystem::RayCentered &&
      (!isTransmissionLossMode(runMode_) ||
       (beamFamily_ != BeamFamily::CervenyGaussian &&
        beamFamily_ != BeamFamily::GeometricHat))) {
    throw ValidationError(
        "ray-centered coordinates are supported only for Cerveny or "
        "geometric-hat TL");
  }
  if (cervenyCoordinateSystem_ == CervenyCoordinateSystem::RayCentered) {
    validateRayCenteredReceiverRanges(receivers_);
  }
  if (fieldComponent_ != FieldComponent::Pressure &&
      (!isTransmissionLossMode(runMode_) ||
       beamFamily_ != BeamFamily::CervenyGaussian)) {
    throw ValidationError("only Cerveny TL supports non-pressure components");
  }
  if (beamFamily_ == BeamFamily::SimpleGaussian &&
      runMode_ != SimulationRunMode::Coherent) {
    throw ValidationError("simple Gaussian TL requires coherent pressure");
  }
  if (curvatureMode_ != BoundaryCurvatureMode::Standard &&
      (!isTransmissionLossMode(runMode_) ||
       beamFamily_ != BeamFamily::CervenyGaussian)) {
    throw ValidationError(
        "only Cerveny TL supports non-standard curvature modes");
  }
  if (beamWidthMode_ != BeamWidthMode::MinimumWidth &&
      (!isTransmissionLossMode(runMode_) ||
       beamFamily_ != BeamFamily::CervenyGaussian)) {
    throw ValidationError("only Cerveny TL supports non-minimum beam widths");
  }
  requireFinite(source_.depth, "source.depth");
  requireFinite(source_.amplitude, "source.amplitude");
  const Vec2 sourcePosition{.range = 0.0, .depth = source_.depth};
  if (environment_.seaSurface().geometry().interiorSignedDistance(
          sourcePosition, 0U) <= 0.0 ||
      environment_.seabed().geometry().interiorSignedDistance(sourcePosition,
                                                              0U) <= 0.0) {
    throw ValidationError("source depth must be strictly inside the water");
  }
  if (source_.amplitude < 0.0) {
    throw ValidationError("source amplitude must be non-negative");
  }

  for (double range : receivers_.ranges()) {
    for (double depth : receivers_.depths()) {
      const Vec2 receiverPosition{.range = range, .depth = depth};
      if (environment_.seaSurface().geometry().interiorSignedDistance(
              receiverPosition, 0U) < 0.0 ||
          environment_.seabed().geometry().interiorSignedDistance(
              receiverPosition, 0U) < 0.0) {
        throw ValidationError(
            "receiver grid must lie inside or on the water boundaries");
      }
    }
  }

  requireFinite(integrator_.stepLength, "integrator.stepLength");
  requireFinite(integrator_.rangeLimit, "integrator.rangeLimit");
  requireFinite(integrator_.depthLimit, "integrator.depthLimit");
  if (integrator_.stepLength <= 0.0) {
    throw ValidationError("integrator.stepLength must be positive");
  }
  if (integrator_.rangeLimit <= 0.0) {
    throw ValidationError("integrator.rangeLimit must be positive");
  }
  if (integrator_.depthLimit <= 0.0) {
    throw ValidationError("integrator.depthLimit must be positive");
  }
  if (integrator_.maximumRayPoints < 2U) {
    throw ValidationError("integrator.maximumRayPoints must be at least two");
  }

  const CLinearSsp soundSpeedProfile(environment_.soundSpeedProfile());
  const SoundSpeedSample sourceSample = soundSpeedProfile.evaluate(
      Vec2{.range = 0.0, .depth = source_.depth}, 0U);
  launchFanPlan_ = LaunchFanPlanner::plan(LaunchFanPlanningInput{
      .frequencies = frequencies_.values(),
      .sourceSoundSpeed = sourceSample.soundSpeed,
      .waterDepth = environment_.waterDepth(),
      .maximumRange = receivers_.ranges().back(),
      .minimumLaunchAngle = launchFan.minimumAngle,
      .maximumLaunchAngle = launchFan.maximumAngle,
      .explicitLaunchAngleCount = launchFan.explicitLaunchAngleCount,
      .inputDegreeBounds = launchFan.inputDegreeBounds,
      .rayTraceMode = runMode_ == SimulationRunMode::RayTrace,
  });
  for (double launchAngle : launchFanPlan_.launchAngles) {
    const double patternAmplitude =
        sourceBeamPattern_.amplitudeForLaunchAngle(launchAngle);
    if (!std::isfinite(patternAmplitude) || patternAmplitude < 0.0) {
      throw ValidationError(
          "source beam pattern must remain finite and non-negative over "
          "the configured launch fan");
    }
  }
}

const Environment& SimulationCase::environment() const noexcept {
  return environment_;
}

const Source& SimulationCase::source() const noexcept { return source_; }

const ReceiverGrid& SimulationCase::receivers() const noexcept {
  return receivers_;
}

const FrequencyGrid& SimulationCase::frequencies() const noexcept {
  return frequencies_;
}

const LaunchFanPlan& SimulationCase::launchFanPlan() const noexcept {
  return launchFanPlan_;
}

const IntegratorSettings& SimulationCase::integrator() const noexcept {
  return integrator_;
}

const SourceBeamPattern& SimulationCase::sourceBeamPattern() const noexcept {
  return sourceBeamPattern_;
}

SimulationRunMode SimulationCase::runMode() const noexcept { return runMode_; }

BeamFamily SimulationCase::beamFamily() const noexcept { return beamFamily_; }

FieldComponent SimulationCase::fieldComponent() const noexcept {
  return fieldComponent_;
}

BoundaryCurvatureMode SimulationCase::curvatureMode() const noexcept {
  return curvatureMode_;
}

BeamWidthMode SimulationCase::beamWidthMode() const noexcept {
  return beamWidthMode_;
}

CervenyCoordinateSystem SimulationCase::cervenyCoordinateSystem()
    const noexcept {
  return cervenyCoordinateSystem_;
}

}  // namespace rayreuse
