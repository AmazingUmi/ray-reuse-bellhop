#include "bellhop/field/geometric_gaussian_influence.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <limits>
#include <numbers>
#include <string>
#include <string_view>
#include <utility>

#include "bellhop/error.hpp"

namespace bellhop {
namespace {

inline constexpr double kBeamWindow = 4.0;
// Origin spells this literal as default REAL, so it is rounded to REAL4
// before the mixed-kind multiplication with the binary64 frequency and
// REAL(complex(kind=8)) travel time.
inline constexpr double kNearFieldFactor = static_cast<double>(0.2F);

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

void validateInput(
    const FrequencyWorkspace* pressureWorkspace,
    const IntensityWorkspace* intensityWorkspace, const RayPath& path,
    const RayFrequencyState& frequencyState, const ReceiverGrid& receivers,
    double launchAngleSpacingRadians,
    const std::optional<GeometricGaussianDiagnosticRequest>&
        diagnosticRequest) {
  if ((pressureWorkspace == nullptr) == (intensityWorkspace == nullptr)) {
    throw ValidationError(
        "geometric Gaussian influence requires exactly one workspace");
  }
  if (!std::isfinite(launchAngleSpacingRadians) ||
      launchAngleSpacingRadians <= 0.0) {
    throw ValidationError(
        "geometric Gaussian launch-angle spacing must be positive and "
        "finite");
  }
  if (path.points.size() < 2U ||
      path.points.size() != frequencyState.points.size()) {
    throw ValidationError(
        "geometric Gaussian geometry and frequency point counts must "
        "match");
  }
  if (!frequencyState.points.front().active) {
    throw ValidationError(
        "geometric Gaussian source point must be active");
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
        "geometric Gaussian workspace metadata must match the run");
  }

  bool inactiveSeen = false;
  for (const RayFrequencyPoint& point : frequencyState.points) {
    if (inactiveSeen && point.active) {
      throw ValidationError(
          "geometric Gaussian active prefix must be contiguous");
    }
    inactiveSeen = inactiveSeen || !point.active;
    requireFiniteComplex(point.complexTravelTime,
                         "geometric Gaussian complex travel time");
    requireFinite(point.amplitude, "geometric Gaussian amplitude");
    requireFinite(point.reflectionPhase,
                  "geometric Gaussian reflection phase");
    if (point.amplitude < 0.0) {
      throw ValidationError(
          "geometric Gaussian amplitude must be non-negative");
    }
  }
  for (const RayState& point : path.points) {
    if (!isFinite(point.position) || !isFinite(point.slowness) ||
        !std::isfinite(point.dynamicQ[0U]) ||
        !std::isfinite(point.soundSpeed) || point.soundSpeed <= 0.0 ||
        !std::isfinite(point.realTravelTime)) {
      throw ValidationError(
          "geometric Gaussian ray path contains an invalid state");
    }
  }
  if (diagnosticRequest.has_value() &&
      (diagnosticRequest->receiverRangeIndex >= receivers.rangeCount() ||
       diagnosticRequest->receiverDepthIndex >=
           receivers.receiversPerRange())) {
    throw ValidationError(
        "geometric Gaussian diagnostic receiver index is out of range");
  }
}

[[nodiscard]] GeometricGaussianWidthBranch classifyWidthBranch(
    double geometricSigma, double nearFieldSigma,
    double wavelengthSigma) noexcept {
  const double broadeningSigma =
      std::min(nearFieldSigma, wavelengthSigma);
  if (geometricSigma >= broadeningSigma) {
    return GeometricGaussianWidthBranch::Geometric;
  }
  return nearFieldSigma <= wavelengthSigma
             ? GeometricGaussianWidthBranch::NearField
             : GeometricGaussianWidthBranch::WavelengthCap;
}

}  // namespace

GeometricGaussianInfluence::GeometricGaussianInfluence(
    ReceiverGrid receivers, SourceGeometry sourceGeometry,
    SimulationRunMode runMode)
    : receivers_(std::move(receivers)),
      sourceGeometry_(sourceGeometry),
      runMode_(runMode) {
  switch (sourceGeometry_) {
    case SourceGeometry::Point:
    case SourceGeometry::Line:
      break;
    default:
      throw ValidationError(
          "geometric Gaussian source geometry is invalid");
  }
  if (!isTransmissionLossMode(runMode_)) {
    throw ValidationError(
        "geometric Gaussian influence requires a transmission-loss mode");
  }
}

std::optional<GeometricGaussianDiagnostic>
GeometricGaussianInfluence::accumulate(
    FrequencyWorkspace& workspace, const RayPath& path,
    const RayFrequencyState& frequencyState,
    double launchAngleSpacingRadians,
    std::optional<GeometricGaussianDiagnosticRequest>
        diagnosticRequest) const {
  if (runMode_ != SimulationRunMode::CoherentTransmissionLoss) {
    throw ValidationError(
        "geometric Gaussian complex pressure requires coherent TL mode");
  }
  return accumulateImpl(&workspace, nullptr, path, frequencyState,
                        launchAngleSpacingRadians, diagnosticRequest);
}

std::optional<GeometricGaussianDiagnostic>
GeometricGaussianInfluence::accumulateIntensity(
    IntensityWorkspace& workspace, const RayPath& path,
    const RayFrequencyState& frequencyState,
    double launchAngleSpacingRadians,
    std::optional<GeometricGaussianDiagnosticRequest>
        diagnosticRequest) const {
  if (runMode_ != SimulationRunMode::IncoherentTransmissionLoss &&
      runMode_ != SimulationRunMode::SemiCoherentTransmissionLoss) {
    throw ValidationError(
        "geometric Gaussian intensity requires incoherent or "
        "semi-coherent TL mode");
  }
  return accumulateImpl(nullptr, &workspace, path, frequencyState,
                        launchAngleSpacingRadians, diagnosticRequest);
}

std::optional<GeometricGaussianDiagnostic>
GeometricGaussianInfluence::accumulateImpl(
    FrequencyWorkspace* pressureWorkspace,
    IntensityWorkspace* intensityWorkspace, const RayPath& path,
    const RayFrequencyState& frequencyState,
    double launchAngleSpacingRadians,
    std::optional<GeometricGaussianDiagnosticRequest>
        diagnosticRequest) const {
  validateInput(pressureWorkspace, intensityWorkspace, path, frequencyState,
                receivers_, launchAngleSpacingRadians, diagnosticRequest);
  std::optional<GeometricGaussianDiagnostic> diagnostic;
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
  const double sourceRatio =
      sourceGeometry_ == SourceGeometry::Point
          ? std::sqrt(std::abs(std::cos(path.launchAngle))) /
                std::sqrt(2.0 * std::numbers::pi)
          : 1.0 / std::sqrt(2.0 * std::numbers::pi);
  requireFinite(q0, "geometric Gaussian q0");
  requireFinite(sourceRatio, "geometric Gaussian source ratio");

  const auto applyContribution =
      [&](std::size_t depthIndex, std::size_t rangeIndex,
          std::size_t leftIndex, std::size_t rightIndex,
          double interpolationWeight, double normalOffset,
          double qInterpolated, double geometricSigma,
          double nearFieldSigma, double wavelengthSigma, double sigma1,
          GeometricGaussianWidthBranch widthBranch,
          double gaussianWeight, double amplitudeConstant,
          double causticPhase, std::complex<double> delay) {
        requireFinite(qInterpolated,
                      "geometric Gaussian interpolated q");
        requireFinite(geometricSigma,
                      "geometric Gaussian geometric sigma");
        requireFinite(nearFieldSigma,
                      "geometric Gaussian near-field sigma");
        requireFinite(wavelengthSigma,
                      "geometric Gaussian wavelength sigma");
        requireFinite(sigma1, "geometric Gaussian sigma1");
        requireFinite(gaussianWeight,
                      "geometric Gaussian weight");
        requireFinite(amplitudeConstant,
                      "geometric Gaussian amplitude constant");
        requireFinite(causticPhase,
                      "geometric Gaussian caustic phase");
        requireFiniteComplex(delay, "geometric Gaussian delay");

        std::complex<double> pressureIncrement{};
        double intensityIncrement = 0.0;
        if (pressureWorkspace != nullptr) {
          const double amplitude = amplitudeConstant * gaussianWeight;
          const std::complex<double> phaseArgument =
              angularFrequency * delay - causticPhase;
          pressureIncrement =
              amplitude * negativeImaginaryExponential(phaseArgument);
          requireFiniteComplex(pressureIncrement,
                               "geometric Gaussian pressure increment");
          const std::complex<double> updated =
              pressureWorkspace->at(depthIndex, rangeIndex) +
              pressureIncrement;
          requireFiniteComplex(updated,
                               "geometric Gaussian accumulated pressure");
          pressureWorkspace->at(depthIndex, rangeIndex) = updated;
        } else {
          const double attenuatedConstant =
              amplitudeConstant *
              std::exp((angularFrequency * delay).imag());
          const double power = attenuatedConstant * attenuatedConstant;
          intensityIncrement = std::sqrt(2.0 * std::numbers::pi) *
                               power * gaussianWeight;
          if (!std::isfinite(intensityIncrement) ||
              intensityIncrement < 0.0) {
            throw ValidationError(
                "geometric Gaussian intensity increment must be finite "
                "and non-negative");
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
            diagnostic->geometricSigma = geometricSigma;
            diagnostic->nearFieldSigma = nearFieldSigma;
            diagnostic->wavelengthSigma = wavelengthSigma;
            diagnostic->sigma1 = sigma1;
            diagnostic->widthBranch = widthBranch;
            diagnostic->gaussianWeight = gaussianWeight;
            diagnostic->amplitudeConstant = amplitudeConstant;
            diagnostic->causticPhase = causticPhase;
            diagnostic->delay = delay;
            diagnostic->pressureIncrement = pressureIncrement;
            diagnostic->intensityIncrement = intensityIncrement;
          }
        }
      };

  const std::vector<double>& ranges = receivers_.ranges();
  const auto firstReceiver =
      std::find_if(ranges.begin(), ranges.end(), [&](double range) {
        return range > path.points.front().position.range;
      });
  std::size_t receiverIndex{};
  if (firstReceiver == ranges.end()) {
    if (path.points.front().slowness.range >= 0.0) {
      return diagnostic;
    }
    receiverIndex = ranges.size() - 1U;
  } else {
    receiverIndex =
        static_cast<std::size_t>(firstReceiver - ranges.begin());
    if (path.points.front().slowness.range < 0.0 && receiverIndex > 0U) {
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
        1000.0 *
            floatingSpacing(path.points[rightIndex].position.range)) {
      continue;
    }
    const Vec2 tangent = segment / segmentLength;
    const Vec2 normal{.range = -tangent.depth, .depth = tangent.range};
    const double rightRange = path.points[rightIndex].position.range;
    const double leftQ = path.points[leftIndex].dynamicQ[0U];
    if (crossesGeometricCaustic(previousQ, leftQ)) {
      phase += std::numbers::pi / 2.0;
    }
    previousQ = leftQ;

    // This is not merely an optimization.  Origin first projects the
    // endpoint geometric width onto a vertical receiver line, then uses the
    // resulting segment envelope as a depth gate.  A receiver can project
    // outside the ray chord while still passing the later local-normal test,
    // so omitting this gate adds observable edge contributions.
    const double wavelength =
        path.points[leftIndex].soundSpeed / frequencyState.frequency;
    const double wavelengthSigma = std::numbers::pi * wavelength;
    double segmentSigma =
        std::max(std::abs(path.points[leftIndex].dynamicQ[0U]),
                 std::abs(path.points[rightIndex].dynamicQ[0U])) /
        q0 / std::abs(tangent.range);
    segmentSigma =
        std::max(segmentSigma,
                 std::min(kNearFieldFactor * frequencyState.frequency *
                              frequencyState.points[rightIndex]
                                  .complexTravelTime.real(),
                          wavelengthSigma));
    const double segmentRadius = kBeamWindow * segmentSigma;
    double minimumDepth = -std::numeric_limits<double>::infinity();
    double maximumDepth = std::numeric_limits<double>::infinity();
    if (std::abs(tangent.range) > 0.5) {
      minimumDepth =
          std::min(path.points[leftIndex].position.depth,
                   path.points[rightIndex].position.depth) -
          segmentRadius;
      maximumDepth =
          std::max(path.points[leftIndex].position.depth,
                   path.points[rightIndex].position.depth) +
          segmentRadius;
    }

    for (;;) {
      const double receiverRange = ranges[receiverIndex];
      if (receiverRange >= std::min(previousRange, rightRange) &&
          receiverRange < std::max(previousRange, rightRange)) {
        for (std::size_t depthIndex = 0U;
             depthIndex < receivers_.receiversPerRange(); ++depthIndex) {
          const Vec2 receiver{
              .range = receiverRange,
              .depth = receivers_.depthAt(depthIndex, receiverIndex)};
          if (receiver.depth < minimumDepth ||
              receiver.depth > maximumDepth) {
            continue;
          }
          const Vec2 offset =
              receiver - path.points[leftIndex].position;
          const double interpolationWeight =
              fortranDotProduct2D(offset, tangent) / segmentLength;
          const double normalOffset =
              std::abs(fortranDotProduct2D(offset, normal));
          const double qInterpolated =
              leftQ + interpolationWeight *
                          (path.points[rightIndex].dynamicQ[0U] - leftQ);
          const double geometricSigma = std::abs(qInterpolated / q0);
          const double nearFieldSigma =
              kNearFieldFactor * frequencyState.frequency *
              frequencyState.points[rightIndex].complexTravelTime.real();
          const double sigma1 =
              std::max(geometricSigma,
                       std::min(nearFieldSigma, wavelengthSigma));
          if (normalOffset < kBeamWindow * sigma1) {
            const std::complex<double> delay =
                frequencyState.points[leftIndex].complexTravelTime +
                interpolationWeight *
                    (frequencyState.points[rightIndex].complexTravelTime -
                     frequencyState.points[leftIndex].complexTravelTime);
            const double amplitudeConstant =
                sourceRatio *
                std::sqrt(path.points[rightIndex].soundSpeed /
                          (q0 * sigma1)) *
                frequencyState.points[rightIndex].amplitude;
            const double normalizedOffset = normalOffset / sigma1;
            const double gaussianWeight =
                std::sqrt(geometricSigma / sigma1) *
                std::exp(-0.5 * normalizedOffset * normalizedOffset);
            double phaseAtReceiver =
                frequencyState.points[leftIndex].reflectionPhase + phase;
            if (crossesGeometricCaustic(previousQ, qInterpolated)) {
              phaseAtReceiver += std::numbers::pi / 2.0;
            }
            applyContribution(
                depthIndex, receiverIndex, leftIndex, rightIndex,
                interpolationWeight, normalOffset, qInterpolated,
                geometricSigma, nearFieldSigma, wavelengthSigma, sigma1,
                classifyWidthBranch(geometricSigma, nearFieldSigma,
                                    wavelengthSigma),
                gaussianWeight, amplitudeConstant, phaseAtReceiver, delay);
          }
        }
      }

      if (rightRange > ranges[receiverIndex]) {
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

}  // namespace bellhop
