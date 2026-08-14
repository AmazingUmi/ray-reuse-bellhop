#include "bellhop/model/simulation_case.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <string>
#include <utility>

#include "bellhop/error.hpp"
#include "bellhop/model/sound_speed_evaluator.hpp"

namespace bellhop {
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

std::size_t checkedProduct(std::size_t left, std::size_t right,
                           const std::string& name) {
  if (left != 0U &&
      right > std::numeric_limits<std::size_t>::max() / left) {
    throw ValidationError(name + " dimensions overflow size_t");
  }
  return left * right;
}

}  // namespace

bool isTransmissionLossMode(SimulationRunMode mode) {
  switch (mode) {
    case SimulationRunMode::CoherentTransmissionLoss:
    case SimulationRunMode::IncoherentTransmissionLoss:
    case SimulationRunMode::SemiCoherentTransmissionLoss:
      return true;
    case SimulationRunMode::RayTrace:
      return false;
  }
  throw ValidationError("simulation run mode is invalid");
}

FieldAccumulationKind fieldAccumulationKind(SimulationRunMode mode) {
  switch (mode) {
    case SimulationRunMode::CoherentTransmissionLoss:
      return FieldAccumulationKind::ComplexPressure;
    case SimulationRunMode::IncoherentTransmissionLoss:
    case SimulationRunMode::SemiCoherentTransmissionLoss:
      return FieldAccumulationKind::Intensity;
    case SimulationRunMode::RayTrace:
      return FieldAccumulationKind::None;
  }
  throw ValidationError("simulation run mode is invalid");
}

bool usesLloydMirror(SimulationRunMode mode) {
  switch (mode) {
    case SimulationRunMode::CoherentTransmissionLoss:
    case SimulationRunMode::IncoherentTransmissionLoss:
    case SimulationRunMode::RayTrace:
      return false;
    case SimulationRunMode::SemiCoherentTransmissionLoss:
      return true;
  }
  throw ValidationError("simulation run mode is invalid");
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
    throw ValidationError(
        "source beam pattern requires at least two samples");
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
  return SourceBeamPattern(
      std::move(angles), std::move(amplitudes), true);
}

double SourceBeamPattern::amplitudeForLaunchAngle(
    double launchAngleRadians) const {
  requireFinite(launchAngleRadians, "source beam pattern launch angle");
  const double angleDegrees =
      launchAngleRadians * (180.0 / std::numbers::pi);
  auto upper = std::lower_bound(
      anglesDegrees_.begin(), anglesDegrees_.end(), angleDegrees);
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

bool SourceBeamPattern::isDirectional() const noexcept {
  return directional_;
}

std::size_t SourceBeamPattern::size() const noexcept {
  return anglesDegrees_.size();
}

double SourceBeamPattern::minimumAngleDegrees() const noexcept {
  return anglesDegrees_.front();
}

double SourceBeamPattern::maximumAngleDegrees() const noexcept {
  return anglesDegrees_.back();
}

ReceiverGrid::ReceiverGrid(std::vector<double> depths,
                           std::vector<double> ranges,
                           ReceiverGridLayout layout)
    : depths_(std::move(depths)),
      ranges_(std::move(ranges)),
      layout_(layout) {
  validateStrictlyIncreasing(depths_, "receiver depths");
  validateStrictlyIncreasing(ranges_, "receiver ranges");
  if (ranges_.front() < 0.0) {
    throw ValidationError("receiver ranges must be non-negative");
  }
  if (layout_ == ReceiverGridLayout::Irregular &&
      depths_.size() != ranges_.size()) {
    throw ValidationError(
        "irregular receiver depth and range counts must match");
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

std::size_t ReceiverGrid::receiversPerRange() const noexcept {
  return isIrregular() ? 1U : depthCount();
}

ReceiverGridLayout ReceiverGrid::layout() const noexcept { return layout_; }

bool ReceiverGrid::isIrregular() const noexcept {
  return layout_ == ReceiverGridLayout::Irregular;
}

double ReceiverGrid::depthAt(std::size_t pressureDepthIndex,
                             std::size_t rangeIndex) const {
  if (rangeIndex >= ranges_.size() ||
      pressureDepthIndex >= receiversPerRange()) {
    throw std::out_of_range("receiver-grid index is out of range");
  }
  return isIrregular() ? depths_[rangeIndex] : depths_[pressureDepthIndex];
}

FrequencyGrid::FrequencyGrid(std::vector<double> values)
    : values_(std::move(values)) {
  if (values_.empty()) {
    throw ValidationError("frequency grid must not be empty");
  }
  for (double value : values_) {
    requireFinite(value, "frequency");
    if (value <= 0.0) {
      throw ValidationError("frequencies must be positive");
    }
  }
}

const std::vector<double>& FrequencyGrid::values() const noexcept {
  return values_;
}

std::size_t FrequencyGrid::size() const noexcept { return values_.size(); }

double FrequencyGrid::designFrequency() const noexcept {
  return *std::max_element(values_.begin(), values_.end());
}

SimulationCase::SimulationCase(Environment environment, Source source,
                               ReceiverGrid receivers,
                               FrequencyGrid frequencies, LaunchFan launchFan,
                               IntegratorSettings integrator,
                               SourceBeamPattern sourceBeamPattern,
                               SimulationRunMode runMode,
                               FieldComponent fieldComponent,
                               SourceGeometry sourceGeometry,
                               CervenyCoordinateSystem cervenyCoordinateSystem,
                               BeamFamily beamFamily)
    : SimulationCase(
          std::move(environment), std::vector<Source>{source},
          std::move(receivers), std::move(frequencies), launchFan,
          integrator, std::move(sourceBeamPattern), runMode,
          fieldComponent, sourceGeometry, cervenyCoordinateSystem,
          beamFamily) {}

SimulationCase::SimulationCase(Environment environment,
                               std::vector<Source> sources,
                               ReceiverGrid receivers,
                               FrequencyGrid frequencies,
                               LaunchFan launchFan,
                               IntegratorSettings integrator,
                               SourceBeamPattern sourceBeamPattern,
                               SimulationRunMode runMode,
                               FieldComponent fieldComponent,
                               SourceGeometry sourceGeometry,
                               CervenyCoordinateSystem cervenyCoordinateSystem,
                               BeamFamily beamFamily)
    : environment_(std::move(environment)),
      sources_(std::move(sources)),
      receivers_(std::move(receivers)),
      frequencies_(std::move(frequencies)),
      integrator_(integrator),
      sourceBeamPattern_(std::move(sourceBeamPattern)),
      runMode_(runMode),
      fieldComponent_(fieldComponent),
      sourceGeometry_(sourceGeometry),
      cervenyCoordinateSystem_(cervenyCoordinateSystem),
      beamFamily_(beamFamily) {
  switch (runMode_) {
    case SimulationRunMode::CoherentTransmissionLoss:
    case SimulationRunMode::IncoherentTransmissionLoss:
    case SimulationRunMode::SemiCoherentTransmissionLoss:
    case SimulationRunMode::RayTrace:
      break;
    default:
      throw ValidationError("simulation run mode is invalid");
  }
  switch (fieldComponent_) {
    case FieldComponent::Pressure:
    case FieldComponent::Vertical:
    case FieldComponent::Horizontal:
      break;
    default:
      throw ValidationError("field component is invalid");
  }
  switch (sourceGeometry_) {
    case SourceGeometry::Point:
    case SourceGeometry::Line:
      break;
    default:
      throw ValidationError("source geometry is invalid");
  }
  switch (cervenyCoordinateSystem_) {
    case CervenyCoordinateSystem::Cartesian:
    case CervenyCoordinateSystem::RayCentered:
      break;
    default:
      throw ValidationError("Cerveny coordinate system is invalid");
  }
  switch (beamFamily_) {
    case BeamFamily::CervenyGaussian:
    case BeamFamily::GeometricHat:
    case BeamFamily::GeometricGaussian:
    case BeamFamily::SimpleGaussian:
      break;
    default:
      throw ValidationError("beam family is invalid");
  }
  if ((beamFamily_ == BeamFamily::GeometricGaussian ||
       beamFamily_ == BeamFamily::SimpleGaussian) &&
      cervenyCoordinateSystem_ != CervenyCoordinateSystem::Cartesian) {
    throw ValidationError(
        "selected beam family supports only Cartesian coordinates");
  }
  if ((beamFamily_ == BeamFamily::CervenyGaussian ||
       beamFamily_ == BeamFamily::GeometricHat) &&
      cervenyCoordinateSystem_ == CervenyCoordinateSystem::RayCentered &&
      receivers_.isIrregular()) {
    throw ValidationError(
        "ray-centered beam families do not support irregular receiver grids");
  }
  if (beamFamily_ != BeamFamily::CervenyGaussian &&
      fieldComponent_ != FieldComponent::Pressure) {
    throw ValidationError(
        "non-Cerveny beam families support only the pressure component");
  }
  if (beamFamily_ == BeamFamily::SimpleGaussian &&
      (runMode_ != SimulationRunMode::CoherentTransmissionLoss ||
       sourceGeometry_ != SourceGeometry::Point ||
       receivers_.isIrregular())) {
    throw ValidationError(
        "simple Gaussian beams require coherent point-source TL on a "
        "rectilinear receiver grid");
  }
  if (sources_.empty()) {
    throw ValidationError("at least one source is required");
  }
  for (const Source& source : sources_) {
    requireFinite(source.depth, "source.depth");
    requireFinite(source.amplitude, "source.amplitude");
    if (source.amplitude < 0.0) {
      throw ValidationError("source amplitude must be non-negative");
    }
  }
  std::stable_sort(
      sources_.begin(), sources_.end(),
      [](const Source& left, const Source& right) {
        return left.depth < right.depth;
      });
  if (isTransmissionLossMode(runMode_)) {
    const std::size_t receiverValueCount = checkedProduct(
        receivers_.receiversPerRange(), receivers_.rangeCount(),
        "receiver grid");
    if (checkedProduct(sources_.size(), receiverValueCount,
                       "source/receiver workspace") >
        kMaximumReceiverGridValues) {
      throw ValidationError(
          "source/receiver workspaces exceed the supported pressure-value "
          "limit");
    }
  }
  for (const Source& source : sources_) {
    const Vec2 sourcePosition{.range = 0.0, .depth = source.depth};
    if (environment_.seaSurface().geometry().interiorSignedDistance(
            sourcePosition, 0U) <= 0.0 ||
        environment_.seabed().geometry().interiorSignedDistance(
            sourcePosition, 0U) <= 0.0) {
      throw ValidationError("source depth must be strictly inside the water");
    }
  }

  for (std::size_t rangeIndex = 0U;
       rangeIndex < receivers_.rangeCount(); ++rangeIndex) {
    for (std::size_t depthIndex = 0U;
         depthIndex < receivers_.receiversPerRange(); ++depthIndex) {
      const double range = receivers_.ranges()[rangeIndex];
      const double depth = receivers_.depthAt(depthIndex, rangeIndex);
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

  if (frequencies_.size() != 1U) {
    throw ValidationError(
        "Bellhop F2CPP requires exactly one frequency per run");
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
    throw ValidationError(
        "integrator.maximumRayPoints must be at least two");
  }

  const Vec2 sourcePosition{.range = 0.0, .depth = sources_.front().depth};
  // Origin builds one angle fan outside the source loop. For multiple
  // sources its automatic-count criterion uses the 1500 m/s reference speed;
  // each source's actual local speed is evaluated later for epsilon, window
  // radius and pressure scaling. Preserve existing single-source planning.
  double sourceSoundSpeed = 1500.0;
  if (sources_.size() == 1U) {
    if (environment_.soundSpeedProfile().interpolationKind() ==
        SspInterpolationKind::Quadrilateral) {
      sourceSoundSpeed = environment_.soundSpeedProfile()
                             .quadrilateralRealSoundSpeedAt(sourcePosition);
    } else {
      const GeometrySspEvaluator soundSpeedProfile(
          environment_.soundSpeedProfile());
      sourceSoundSpeed = soundSpeedProfile.evaluate(sourcePosition, 0U)
                             .soundSpeed;
    }
  }
  launchFanPlan_ = LaunchFanPlanner::plan(
      LaunchFanPlanningInput{
          .frequencies = frequencies_.values(),
          .sourceSoundSpeed = sourceSoundSpeed,
          .waterDepth = environment_.waterDepth(),
          .maximumRange = receivers_.ranges().back(),
          .minimumLaunchAngle = launchFan.minimumAngle,
          .maximumLaunchAngle = launchFan.maximumAngle,
          .explicitLaunchAngleCount =
              launchFan.explicitLaunchAngleCount,
          .inputDegreeBounds =
              launchFan.inputDegreeBounds,
          .rayTraceMode = runMode_ == SimulationRunMode::RayTrace,
      });
  for (const double launchAngle : launchFanPlan_.launchAngles) {
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

const Source& SimulationCase::source() const noexcept {
  return sources_.front();
}

const std::vector<Source>& SimulationCase::sources() const noexcept {
  return sources_;
}

std::size_t SimulationCase::sourceCount() const noexcept {
  return sources_.size();
}

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

SimulationRunMode SimulationCase::runMode() const noexcept {
  return runMode_;
}

FieldComponent SimulationCase::fieldComponent() const noexcept {
  return fieldComponent_;
}

SourceGeometry SimulationCase::sourceGeometry() const noexcept {
  return sourceGeometry_;
}

CervenyCoordinateSystem SimulationCase::cervenyCoordinateSystem() const
    noexcept {
  return cervenyCoordinateSystem_;
}

BeamFamily SimulationCase::beamFamily() const noexcept {
  return beamFamily_;
}

}  // namespace bellhop
