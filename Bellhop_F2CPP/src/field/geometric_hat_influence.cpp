#include "bellhop/field/geometric_hat_influence.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <limits>
#include <numbers>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "bellhop/error.hpp"

namespace bellhop {
namespace {

void requireFinite(double value, std::string_view name) {
  if (!std::isfinite(value)) {
    throw ValidationError(std::string(name) + " must be finite");
  }
}

void requireFiniteComplex(std::complex<double> value,
                          std::string_view name) {
  if (!std::isfinite(value.real()) || !std::isfinite(value.imag())) {
    throw ValidationError(std::string(name) + " must be finite");
  }
}

[[nodiscard]] double floatingSpacing(double value) {
  return std::nextafter(value, std::numeric_limits<double>::infinity()) -
         value;
}

[[nodiscard]] bool crossesGeometricCaustic(double previousQ,
                                            double currentQ) noexcept {
  return (currentQ <= 0.0 && previousQ > 0.0) ||
         (currentQ >= 0.0 && previousQ < 0.0);
}

[[nodiscard]] std::complex<double> negativeImaginaryExponential(
    std::complex<double> phase) {
  const double magnitude = std::exp(phase.imag());
  return {magnitude * std::cos(phase.real()),
          -magnitude * std::sin(phase.real())};
}

[[nodiscard]] std::size_t activePointCount(
    const RayFrequencyState& frequencyState) {
  for (std::size_t index = 0U; index < frequencyState.points.size(); ++index) {
    if (!frequencyState.points[index].active) {
      return index + 1U;
    }
  }
  return frequencyState.points.size();
}

void validateUniformRanges(const ReceiverGrid& receivers, double delta) {
  if (receivers.rangeCount() < 2U) {
    throw ValidationError(
        "ray-centered geometric hat requires at least two receiver ranges");
  }
  for (std::size_t index = 2U; index < receivers.rangeCount(); ++index) {
    const double expected =
        receivers.ranges().front() + static_cast<double>(index) * delta;
    const double tolerance =
        32.0 * std::numeric_limits<double>::epsilon() *
        std::max({1.0, std::abs(expected),
                  std::abs(receivers.ranges()[index])});
    if (std::abs(receivers.ranges()[index] - expected) > tolerance) {
      throw ValidationError(
          "ray-centered geometric hat receiver ranges must be equally "
          "spaced");
    }
  }
}

[[nodiscard]] std::size_t clampedReceiverIndex1Based(
    double projectedRange, const ReceiverGrid& receivers, double delta) {
  requireFinite(projectedRange, "projected geometric-hat range");
  const double raw =
      std::trunc((projectedRange - receivers.ranges().front()) / delta) +
      1.0;
  if (raw <= 0.0) {
    return 0U;
  }
  if (raw >= static_cast<double>(receivers.rangeCount())) {
    return receivers.rangeCount();
  }
  return static_cast<std::size_t>(raw);
}

void validateInput(const FrequencyWorkspace* pressureWorkspace,
                   const IntensityWorkspace* intensityWorkspace,
                   const RayPath& path,
                   const RayFrequencyState& frequencyState,
                   const ReceiverGrid& receivers,
                   double launchAngleSpacingRadians,
                   const std::optional<GeometricHatDiagnosticRequest>&
                       diagnosticRequest) {
  if ((pressureWorkspace == nullptr) == (intensityWorkspace == nullptr)) {
    throw ValidationError(
        "geometric hat influence requires exactly one workspace");
  }
  if (!std::isfinite(launchAngleSpacingRadians) ||
      launchAngleSpacingRadians <= 0.0) {
    throw ValidationError(
        "geometric hat launch-angle spacing must be positive and finite");
  }
  if (path.points.size() < 2U ||
      path.points.size() != frequencyState.points.size()) {
    throw ValidationError(
        "geometric hat geometry and frequency point counts must match");
  }
  if (!frequencyState.points.front().active) {
    throw ValidationError("geometric hat source point must be active");
  }
  const double workspaceFrequency =
      pressureWorkspace != nullptr ? pressureWorkspace->frequency()
                                   : intensityWorkspace->frequency();
  const std::size_t workspaceDepthCount =
      pressureWorkspace != nullptr ? pressureWorkspace->depthCount()
                                   : intensityWorkspace->depthCount();
  const std::size_t workspaceRangeCount =
      pressureWorkspace != nullptr ? pressureWorkspace->rangeCount()
                                   : intensityWorkspace->rangeCount();
  if (workspaceFrequency != frequencyState.frequency ||
      workspaceDepthCount != receivers.receiversPerRange() ||
      workspaceRangeCount != receivers.rangeCount()) {
    throw ValidationError(
        "geometric hat workspace metadata must match the run");
  }
  bool inactiveSeen = false;
  for (const RayFrequencyPoint& point : frequencyState.points) {
    if (inactiveSeen && point.active) {
      throw ValidationError(
          "geometric hat active prefix must be contiguous");
    }
    inactiveSeen = inactiveSeen || !point.active;
    requireFiniteComplex(point.complexTravelTime,
                         "geometric hat complex travel time");
    requireFinite(point.amplitude, "geometric hat amplitude");
    requireFinite(point.reflectionPhase,
                  "geometric hat reflection phase");
    if (point.amplitude < 0.0) {
      throw ValidationError(
          "geometric hat amplitude must be non-negative");
    }
  }
  for (const RayState& point : path.points) {
    if (!isFinite(point.position) || !isFinite(point.slowness) ||
        !std::isfinite(point.dynamicQ[0U]) ||
        !std::isfinite(point.soundSpeed) || point.soundSpeed <= 0.0 ||
        !std::isfinite(point.realTravelTime)) {
      throw ValidationError(
          "geometric hat ray path contains an invalid state");
    }
  }
  if (diagnosticRequest.has_value() &&
      (diagnosticRequest->receiverRangeIndex >= receivers.rangeCount() ||
       diagnosticRequest->receiverDepthIndex >=
           receivers.receiversPerRange())) {
    throw ValidationError(
        "geometric hat diagnostic receiver index is out of range");
  }
}

}  // namespace

GeometricHatInfluence::GeometricHatInfluence(
    ReceiverGrid receivers, CervenyCoordinateSystem coordinates,
    SourceGeometry sourceGeometry, SimulationRunMode runMode)
    : receivers_(std::move(receivers)),
      coordinates_(coordinates),
      sourceGeometry_(sourceGeometry),
      runMode_(runMode),
      receiverRangeDelta_(
          receivers_.rangeCount() >= 2U
              ? receivers_.ranges()[1U] - receivers_.ranges()[0U]
              : 0.0) {
  switch (coordinates_) {
    case CervenyCoordinateSystem::Cartesian:
      break;
    case CervenyCoordinateSystem::RayCentered:
      if (receivers_.isIrregular()) {
        throw ValidationError(
            "ray-centered geometric hat does not support irregular "
            "receivers");
      }
      validateUniformRanges(receivers_, receiverRangeDelta_);
      break;
    default:
      throw ValidationError("geometric hat coordinate system is invalid");
  }
  switch (sourceGeometry_) {
    case SourceGeometry::Point:
    case SourceGeometry::Line:
      break;
    default:
      throw ValidationError("geometric hat source geometry is invalid");
  }
  if (!isTransmissionLossMode(runMode_)) {
    throw ValidationError(
        "geometric hat influence requires a transmission-loss mode");
  }
}

std::optional<GeometricHatDiagnostic> GeometricHatInfluence::accumulate(
    FrequencyWorkspace& workspace, const RayPath& path,
    const RayFrequencyState& frequencyState,
    double launchAngleSpacingRadians,
    std::optional<GeometricHatDiagnosticRequest> diagnosticRequest) const {
  if (runMode_ != SimulationRunMode::CoherentTransmissionLoss) {
    throw ValidationError(
        "geometric hat complex pressure requires coherent TL mode");
  }
  return accumulateImpl(&workspace, nullptr, path, frequencyState,
                        launchAngleSpacingRadians, diagnosticRequest);
}

std::optional<GeometricHatDiagnostic>
GeometricHatInfluence::accumulateIntensity(
    IntensityWorkspace& workspace, const RayPath& path,
    const RayFrequencyState& frequencyState,
    double launchAngleSpacingRadians,
    std::optional<GeometricHatDiagnosticRequest> diagnosticRequest) const {
  if (runMode_ != SimulationRunMode::IncoherentTransmissionLoss &&
      runMode_ != SimulationRunMode::SemiCoherentTransmissionLoss) {
    throw ValidationError(
        "geometric hat intensity requires incoherent or semi-coherent TL "
        "mode");
  }
  return accumulateImpl(nullptr, &workspace, path, frequencyState,
                        launchAngleSpacingRadians, diagnosticRequest);
}

std::optional<GeometricHatDiagnostic> GeometricHatInfluence::accumulateImpl(
    FrequencyWorkspace* pressureWorkspace,
    IntensityWorkspace* intensityWorkspace, const RayPath& path,
    const RayFrequencyState& frequencyState,
    double launchAngleSpacingRadians,
    std::optional<GeometricHatDiagnosticRequest> diagnosticRequest) const {
  validateInput(pressureWorkspace, intensityWorkspace, path, frequencyState,
                receivers_, launchAngleSpacingRadians, diagnosticRequest);
  std::optional<GeometricHatDiagnostic> diagnostic;
  if (diagnosticRequest.has_value()) {
    diagnostic.emplace();
    diagnostic->receiverRangeIndex =
        diagnosticRequest->receiverRangeIndex;
    diagnostic->receiverDepthIndex =
        diagnosticRequest->receiverDepthIndex;
  }

  const std::size_t pointCount = activePointCount(frequencyState);
  const double angularFrequency =
      2.0 * std::numbers::pi * frequencyState.frequency;
  const double q0 =
      path.points.front().soundSpeed / launchAngleSpacingRadians;
  const double ratio =
      sourceGeometry_ == SourceGeometry::Line
          ? 1.0
          : std::sqrt(std::abs(std::cos(path.launchAngle)));
  requireFinite(q0, "geometric hat q0");
  requireFinite(ratio, "geometric hat source ratio");

  const auto applyContribution =
      [&](std::size_t depthIndex, std::size_t rangeIndex,
          std::size_t leftIndex, std::size_t rightIndex,
          double interpolationWeight, double normalOffset,
          double qInterpolated, double hatWeight,
          double amplitudeConstant, double causticPhase,
          std::complex<double> delay) {
        requireFinite(qInterpolated, "geometric hat interpolated q");
        requireFinite(hatWeight, "geometric hat weight");
        requireFinite(amplitudeConstant,
                      "geometric hat amplitude constant");
        requireFinite(causticPhase, "geometric hat caustic phase");
        requireFiniteComplex(delay, "geometric hat delay");
        std::complex<double> pressureIncrement{};
        double intensityIncrement = 0.0;
        if (pressureWorkspace != nullptr) {
          const double amplitude = amplitudeConstant * hatWeight;
          const std::complex<double> phaseArgument =
              angularFrequency * delay - causticPhase;
          pressureIncrement =
              amplitude * negativeImaginaryExponential(phaseArgument);
          requireFiniteComplex(pressureIncrement,
                               "geometric hat pressure increment");
          const std::complex<double> updated =
              pressureWorkspace->at(depthIndex, rangeIndex) +
              pressureIncrement;
          requireFiniteComplex(updated,
                               "geometric hat accumulated pressure");
          pressureWorkspace->at(depthIndex, rangeIndex) = updated;
        } else {
          // ApplyContribution forms the attenuated real constant, squares it,
          // and only then applies the hat weight once.
          const double attenuatedConstant =
              amplitudeConstant *
              std::exp((angularFrequency * delay).imag());
          const double power = attenuatedConstant * attenuatedConstant;
          intensityIncrement = power * hatWeight;
          if (!std::isfinite(intensityIncrement) ||
              intensityIncrement < 0.0) {
            throw ValidationError(
                "geometric hat intensity increment must be finite and "
                "non-negative");
          }
          intensityWorkspace->add(depthIndex, rangeIndex,
                                  intensityIncrement);
        }

        if (diagnosticRequest.has_value() &&
            diagnosticRequest->receiverRangeIndex == rangeIndex &&
            diagnosticRequest->receiverDepthIndex == depthIndex) {
          ++diagnostic->evaluationCount;
          if (!diagnostic->evaluated) {
            diagnostic->evaluated = true;
            diagnostic->leftPointIndex = leftIndex;
            diagnostic->rightPointIndex = rightIndex;
            diagnostic->interpolationWeight = interpolationWeight;
            diagnostic->normalOffset = normalOffset;
            diagnostic->qInterpolated = qInterpolated;
            diagnostic->hatWeight = hatWeight;
            diagnostic->amplitudeConstant = amplitudeConstant;
            diagnostic->causticPhase = causticPhase;
            diagnostic->delay = delay;
            diagnostic->pressureIncrement = pressureIncrement;
            diagnostic->intensityIncrement = intensityIncrement;
          }
        }
      };

  if (coordinates_ == CervenyCoordinateSystem::Cartesian) {
    const std::vector<double>& ranges = receivers_.ranges();
    const auto firstReceiver =
        std::find_if(ranges.begin(), ranges.end(),
                     [&](double range) {
                       return range > path.points.front().position.range;
                     });
    std::size_t receiverIndex{};
    if (firstReceiver == ranges.end()) {
      if (path.points.front().slowness.range >= 0.0) {
        return diagnostic;
      }
      // Origin's masked MINLOC returns zero in this case and would index
      // Rr(0).  Resolve the intended left-going endpoint without retaining
      // that out-of-bounds legacy defect.
      receiverIndex = ranges.size() - 1U;
    } else {
      receiverIndex =
          static_cast<std::size_t>(firstReceiver - ranges.begin());
      if (path.points.front().slowness.range < 0.0 &&
          receiverIndex > 0U) {
        --receiverIndex;
      }
    }
    double previousRange = path.points.front().position.range;
    double phase = 0.0;
    double previousQ = path.points.front().dynamicQ[0U];

    for (std::size_t rightIndex = 1U; rightIndex < pointCount;
         ++rightIndex) {
      const std::size_t leftIndex = rightIndex - 1U;
      const Vec2 segment =
          path.points[rightIndex].position - path.points[leftIndex].position;
      const double segmentLength = norm(segment);
      if (segmentLength <
          1000.0 * floatingSpacing(
                       path.points[rightIndex].position.range)) {
        continue;
      }
      const Vec2 tangent = segment / segmentLength;
      const Vec2 normal{.range = -tangent.depth,
                        .depth = tangent.range};
      const double rightRange = path.points[rightIndex].position.range;
      const double leftQ = path.points[leftIndex].dynamicQ[0U];
      if (crossesGeometricCaustic(previousQ, leftQ)) {
        phase += std::numbers::pi / 2.0;
      }
      previousQ = leftQ;

      for (;;) {
        const double receiverRange = ranges[receiverIndex];
        if (receiverRange >= std::min(previousRange, rightRange) &&
            receiverRange < std::max(previousRange, rightRange)) {
          for (std::size_t depthIndex = 0U;
               depthIndex < receivers_.receiversPerRange(); ++depthIndex) {
            const Vec2 receiver{
                .range = receiverRange,
                .depth = receivers_.depthAt(depthIndex, receiverIndex)};
            const Vec2 offset =
                receiver - path.points[leftIndex].position;
            const double interpolationWeight =
                fortranDotProduct2D(offset, tangent) / segmentLength;
            const double normalOffset =
                std::abs(fortranDotProduct2D(offset, normal));
            const double q =
                leftQ + interpolationWeight *
                            (path.points[rightIndex].dynamicQ[0U] - leftQ);
            const double beamRadius = std::abs(q / q0);
            if (normalOffset < beamRadius) {
              const std::complex<double> delay =
                  frequencyState.points[leftIndex].complexTravelTime +
                  interpolationWeight *
                      (frequencyState.points[rightIndex].complexTravelTime -
                       frequencyState.points[leftIndex].complexTravelTime);
              const double amplitudeConstant =
                  ratio *
                  std::sqrt(path.points[rightIndex].soundSpeed /
                            std::abs(q)) *
                  frequencyState.points[rightIndex].amplitude;
              const double hatWeight =
                  (beamRadius - normalOffset) / beamRadius;
              double phaseAtReceiver =
                  frequencyState.points[leftIndex].reflectionPhase + phase;
              if (crossesGeometricCaustic(previousQ, q)) {
                phaseAtReceiver += std::numbers::pi / 2.0;
              }
              applyContribution(
                  depthIndex, receiverIndex, leftIndex, rightIndex,
                  interpolationWeight, normalOffset, q, hatWeight,
                  amplitudeConstant, phaseAtReceiver, delay);
            }
          }
        }

        if (ranges[receiverIndex] < rightRange) {
          if (receiverIndex + 1U >= ranges.size()) {
            break;
          }
          const std::size_t next = receiverIndex + 1U;
          if (ranges[next] >= rightRange) {
            break;
          }
          receiverIndex = next;
        } else {
          if (receiverIndex == 0U) {
            break;
          }
          const std::size_t next = receiverIndex - 1U;
          if (ranges[next] <= rightRange) {
            break;
          }
          receiverIndex = next;
        }
      }
      previousRange = rightRange;
    }
    return diagnostic;
  }

  std::vector<Vec2> normals;
  std::vector<double> scaledAmplitudes;
  normals.reserve(pointCount);
  scaledAmplitudes.reserve(pointCount);
  for (std::size_t index = 0U; index < pointCount; ++index) {
    const Vec2 tangent =
        path.points[index].soundSpeed * path.points[index].slowness;
    normals.push_back(
        Vec2{.range = tangent.depth, .depth = -tangent.range});
    scaledAmplitudes.push_back(
        ratio * std::sqrt(path.points[index].soundSpeed) *
        frequencyState.points[index].amplitude);
  }

  for (std::size_t depthIndex = 0U;
       depthIndex < receivers_.receiversPerRange(); ++depthIndex) {
    const double receiverDepth = receivers_.depths()[depthIndex];
    double phase = 0.0;
    double previousQ = path.points.front().dynamicQ[0U];
    double previousNormalOffset = 0.0;
    double previousProjectedRange = 0.0;
    std::size_t previousReceiverIndex1Based = 1U;
    if (std::abs(normals.front().depth) < 1.0e-6) {
      previousNormalOffset = 1.0e10;
      previousProjectedRange = 1.0e10;
    } else {
      previousNormalOffset =
          (receiverDepth - path.points.front().position.depth) /
          normals.front().depth;
      previousProjectedRange =
          path.points.front().position.range +
          previousNormalOffset * normals.front().range;
      previousReceiverIndex1Based = clampedReceiverIndex1Based(
          previousProjectedRange, receivers_, receiverRangeDelta_);
    }

    for (std::size_t rightIndex = 1U; rightIndex < pointCount;
         ++rightIndex) {
      if (std::abs(normals[rightIndex].depth) < 1.0e-10) {
        continue;
      }
      const double normalOffset =
          (receiverDepth - path.points[rightIndex].position.depth) /
          normals[rightIndex].depth;
      const double projectedRange =
          path.points[rightIndex].position.range +
          normalOffset * normals[rightIndex].range;
      const std::size_t receiverIndex1Based =
          clampedReceiverIndex1Based(projectedRange, receivers_,
                                     receiverRangeDelta_);
      const bool duplicatePoint =
          std::abs(path.points[rightIndex].position.range -
                   path.points[rightIndex - 1U].position.range) <
          1000.0 * floatingSpacing(
                       path.points[rightIndex].position.range);
      if (duplicatePoint ||
          previousReceiverIndex1Based == receiverIndex1Based) {
        previousProjectedRange = projectedRange;
        previousNormalOffset = normalOffset;
        previousReceiverIndex1Based = receiverIndex1Based;
        continue;
      }

      const double leftQ = path.points[rightIndex - 1U].dynamicQ[0U];
      if (crossesGeometricCaustic(previousQ, leftQ)) {
        phase += std::numbers::pi / 2.0;
      }
      previousQ = leftQ;

      const auto evaluateReceiver = [&](std::size_t oneBasedRange) {
        const std::size_t rangeIndex = oneBasedRange - 1U;
        const double interpolationWeight =
            (receivers_.ranges()[rangeIndex] - previousProjectedRange) /
            (projectedRange - previousProjectedRange);
        const double interpolatedNormal =
            std::abs(previousNormalOffset +
                     interpolationWeight *
                         (normalOffset - previousNormalOffset));
        const double q =
            leftQ + interpolationWeight *
                        (path.points[rightIndex].dynamicQ[0U] - leftQ);
        const double beamRadius = std::abs(q) / q0;
        if (interpolatedNormal >= beamRadius) {
          return;
        }
        const std::complex<double> delay =
            frequencyState.points[rightIndex - 1U].complexTravelTime +
            interpolationWeight *
                (frequencyState.points[rightIndex].complexTravelTime -
                 frequencyState.points[rightIndex - 1U]
                     .complexTravelTime);
        const double amplitudeConstant =
            scaledAmplitudes[rightIndex] / std::sqrt(std::abs(q));
        const double hatWeight =
            (beamRadius - interpolatedNormal) / beamRadius;
        double phaseAtReceiver =
            frequencyState.points[rightIndex - 1U].reflectionPhase + phase;
        if (crossesGeometricCaustic(previousQ, q)) {
          phaseAtReceiver += std::numbers::pi / 2.0;
        }
        applyContribution(
            depthIndex, rangeIndex, rightIndex - 1U, rightIndex,
            interpolationWeight, interpolatedNormal, q, hatWeight,
            amplitudeConstant, phaseAtReceiver, delay);
      };

      if (receiverIndex1Based > previousReceiverIndex1Based) {
        for (std::size_t oneBasedRange =
                 previousReceiverIndex1Based + 1U;
             oneBasedRange <= receiverIndex1Based; ++oneBasedRange) {
          evaluateReceiver(oneBasedRange);
        }
      } else {
        for (std::size_t oneBasedRange = previousReceiverIndex1Based;
             oneBasedRange >= receiverIndex1Based + 1U;
             --oneBasedRange) {
          evaluateReceiver(oneBasedRange);
          if (oneBasedRange == receiverIndex1Based + 1U) {
            break;
          }
        }
      }
      previousProjectedRange = projectedRange;
      previousNormalOffset = normalOffset;
      previousReceiverIndex1Based = receiverIndex1Based;
    }
  }
  return diagnostic;
}

}  // namespace bellhop
