#include "rayreuse/field/simple_gaussian_influence.hpp"

#include <cmath>
#include <complex>
#include <cstddef>
#include <limits>
#include <numbers>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "rayreuse/error.hpp"

namespace rayreuse {
namespace {

void requireFinite(double value, std::string_view name) {
  if (!std::isfinite(value)) {
    throw ValidationError(std::string(name) + " must be finite");
  }
}

void requireFiniteComplex(std::complex<double> value, std::string_view name) {
  if (!std::isfinite(value.real()) || !std::isfinite(value.imag())) {
    throw ValidationError(std::string(name) + " must be finite");
  }
}

double floatingSpacing(double value) {
  return std::nextafter(value, std::numeric_limits<double>::infinity()) - value;
}

bool crossesSimpleGaussianCaustic(double previousQ, double currentQ) noexcept {
  return (currentQ < 0.0 && previousQ >= 0.0) ||
         (currentQ > 0.0 && previousQ <= 0.0);
}

std::size_t activePointCount(const RayFrequencyState& frequencyState) {
  for (std::size_t index = 0U; index < frequencyState.points.size(); ++index) {
    if (!frequencyState.points[index].active) {
      return index + 1U;
    }
  }
  return frequencyState.points.size();
}

void validateInput(
    const FrequencyWorkspace& workspace, const RayPath& path,
    const RayFrequencyState& frequencyState, const ReceiverGrid& receivers,
    double launchAngleSpacingRadians,
    const std::optional<SimpleGaussianDiagnosticRequest>& request) {
  if (!std::isfinite(launchAngleSpacingRadians) ||
      launchAngleSpacingRadians <= 0.0) {
    throw ValidationError(
        "simple Gaussian launch-angle spacing must be positive and finite");
  }
  if (path.points.size() < 2U ||
      path.points.size() != frequencyState.points.size()) {
    throw ValidationError(
        "simple Gaussian geometry and frequency point counts must match");
  }
  if (!frequencyState.points.front().active) {
    throw ValidationError("simple Gaussian source point must be active");
  }
  if (workspace.frequency() != frequencyState.frequency ||
      workspace.depthCount() != receivers.depthCount() ||
      workspace.rangeCount() != receivers.rangeCount()) {
    throw ValidationError(
        "simple Gaussian workspace metadata must match the run");
  }
  bool inactiveSeen = false;
  for (const RayFrequencyPoint& point : frequencyState.points) {
    if (inactiveSeen && point.active) {
      throw ValidationError("simple Gaussian active prefix must be contiguous");
    }
    inactiveSeen = inactiveSeen || !point.active;
    requireFiniteComplex(point.complexTravelTime,
                         "simple Gaussian complex travel time");
    requireFinite(point.amplitude, "simple Gaussian amplitude");
    requireFinite(point.reflectionPhase, "simple Gaussian reflection phase");
    if (point.amplitude < 0.0) {
      throw ValidationError("simple Gaussian amplitude must be non-negative");
    }
  }
  for (const RayState& point : path.points) {
    if (!isFinite(point.position) || !isFinite(point.slowness) ||
        !std::isfinite(point.dynamicQ[0U]) ||
        !std::isfinite(point.soundSpeed) || point.soundSpeed <= 0.0 ||
        !std::isfinite(point.realTravelTime)) {
      throw ValidationError(
          "simple Gaussian ray path contains an invalid state");
    }
  }
  for (const std::complex<double> pressure : workspace.pressure()) {
    requireFiniteComplex(pressure, "simple Gaussian existing pressure");
  }
  if (request.has_value() &&
      (request->receiverRangeIndex >= receivers.rangeCount() ||
       request->receiverDepthIndex >= receivers.depthCount())) {
    throw ValidationError(
        "simple Gaussian diagnostic receiver index is out of range");
  }
}

}  // namespace

SimpleGaussianInfluence::SimpleGaussianInfluence(
    ReceiverGrid receivers, double configuredStepLengthMeters,
    SourceGeometry sourceGeometry)
    : receivers_(std::move(receivers)),
      configuredStepLengthMeters_(configuredStepLengthMeters) {
  if (!std::isfinite(configuredStepLengthMeters_) ||
      configuredStepLengthMeters_ <= 0.0) {
    throw ValidationError(
        "simple Gaussian configured step length must be positive and finite");
  }
  if (sourceGeometry != SourceGeometry::Point) {
    throw ValidationError("simple Gaussian influence requires a point source");
  }
}

std::optional<SimpleGaussianDiagnostic> SimpleGaussianInfluence::accumulate(
    FrequencyWorkspace& workspace, const RayPath& path,
    const RayFrequencyState& frequencyState, double launchAngleSpacingRadians,
    std::optional<SimpleGaussianDiagnosticRequest> diagnosticRequest) const {
  validateInput(workspace, path, frequencyState, receivers_,
                launchAngleSpacingRadians, diagnosticRequest);
  std::optional<SimpleGaussianDiagnostic> diagnostic;
  if (diagnosticRequest.has_value()) {
    diagnostic.emplace();
    diagnostic->receiverRangeIndex = diagnosticRequest->receiverRangeIndex;
    diagnostic->receiverDepthIndex = diagnosticRequest->receiverDepthIndex;
  }

  const double cosine = std::cos(path.launchAngle);
  if (!std::isfinite(cosine) || cosine < 0.0) {
    throw ValidationError(
        "simple Gaussian launch angle must have non-negative cosine");
  }
  const double sourceRatio = std::sqrt(cosine);
  // beta is REAL(KIND=8) in Origin but is assigned the default-REAL literal
  // 0.98, so its binary32 rounding is observable after promotion.
  constexpr float kLegacyBetaLiteral = 0.98F;
  const double beta = static_cast<double>(kLegacyBetaLiteral);
  const double gaussianA =
      -4.0 * std::log(beta) /
      (launchAngleSpacingRadians * launchAngleSpacingRadians);
  const double normalization =
      launchAngleSpacingRadians * std::sqrt(gaussianA / std::numbers::pi);
  const double angularFrequency =
      2.0 * std::numbers::pi * frequencyState.frequency;
  requireFinite(sourceRatio, "simple Gaussian source ratio");
  requireFinite(gaussianA, "simple Gaussian A");
  requireFinite(normalization, "simple Gaussian normalization");
  requireFinite(angularFrequency, "simple Gaussian angular frequency");

  const std::size_t pointCount = activePointCount(frequencyState);
  const std::vector<double>& receiverRanges = receivers_.ranges();
  std::size_t rangeIndex = 0U;
  double leftRange = path.points.front().position.range;
  double previousQ = 1.0;
  double causticPhase = 0.0;
  for (std::size_t rightIndex = 1U; rightIndex < pointCount; ++rightIndex) {
    const std::size_t leftIndex = rightIndex - 1U;
    const double rightRange = path.points[rightIndex].position.range;
    const double leftQ = path.points[leftIndex].dynamicQ[0U];
    if (crossesSimpleGaussianCaustic(previousQ, leftQ)) {
      causticPhase += 0.5 * std::numbers::pi;
    }
    previousQ = leftQ;

    while (rangeIndex < receiverRanges.size() &&
           std::abs(rightRange - leftRange) >
               1000.0 * floatingSpacing(leftRange) &&
           rightRange > receiverRanges[rangeIndex]) {
      const double weight =
          (receiverRanges[rangeIndex] - leftRange) / (rightRange - leftRange);
      const Vec2 interpolatedPosition =
          path.points[leftIndex].position +
          weight * (path.points[rightIndex].position -
                    path.points[leftIndex].position);
      const Vec2 interpolatedSlowness =
          path.points[leftIndex].slowness +
          weight * (path.points[rightIndex].slowness -
                    path.points[leftIndex].slowness);
      const double q =
          leftQ + weight * (path.points[rightIndex].dynamicQ[0U] - leftQ);
      const std::complex<double> tau =
          frequencyState.points[leftIndex].complexTravelTime +
          weight * (frequencyState.points[rightIndex].complexTravelTime -
                    frequencyState.points[leftIndex].complexTravelTime);
      const double legacyArcLength =
          (static_cast<double>(rightIndex) + weight) *
          configuredStepLengthMeters_;
      if (crossesSimpleGaussianCaustic(previousQ, q)) {
        causticPhase += 0.5 * std::numbers::pi;
      }

      const double segmentRange = rightRange - leftRange;
      const double segmentDepth = path.points[rightIndex].position.depth -
                                  path.points[leftIndex].position.depth;
      const double segmentLength =
          std::sqrt(segmentRange * segmentRange + segmentDepth * segmentDepth);
      if (!std::isfinite(segmentLength) || segmentLength <= 0.0) {
        throw ValidationError(
            "simple Gaussian ray segment length must be positive and finite");
      }
      const double rightAmplitude = frequencyState.points[rightIndex].amplitude;
      const double rightReflectionPhase =
          frequencyState.points[rightIndex].reflectionPhase;
      requireFinite(weight, "simple Gaussian interpolation weight");
      requireFinite(q, "simple Gaussian interpolated q");
      requireFinite(legacyArcLength, "simple Gaussian legacy arc length");
      requireFinite(causticPhase, "simple Gaussian caustic phase");
      for (std::size_t depthIndex = 0U; depthIndex < receivers_.depthCount();
           ++depthIndex) {
        const double deltaDepth =
            receivers_.depths()[depthIndex] - interpolatedPosition.depth;
        const double closestPointDistance =
            std::abs(deltaDepth * segmentRange) / segmentLength;
        const double offRayRadicand =
            deltaDepth * deltaDepth -
            closestPointDistance * closestPointDistance;
        if (!std::isfinite(offRayRadicand) || offRayRadicand < 0.0) {
          throw ValidationError("simple Gaussian off-ray distance is invalid");
        }
        const double offRayDistance = std::sqrt(offRayRadicand);
        const double effectiveDistance = legacyArcLength + offRayDistance;
        if (!std::isfinite(effectiveDistance) || effectiveDistance <= 0.0) {
          throw ValidationError(
              "simple Gaussian effective distance must be positive and "
              "finite");
        }
        const double angularOffset =
            std::atan(closestPointDistance / effectiveDistance);
        const std::complex<double> delay =
            tau + interpolatedSlowness.depth * deltaDepth;
        const std::complex<double> phase =
            angularFrequency * delay - rightReflectionPhase - causticPhase;
        const std::complex<double> contribution =
            (sourceRatio * normalization * rightAmplitude /
             std::sqrt(effectiveDistance)) *
            std::exp(-gaussianA * angularOffset * angularOffset -
                     std::complex<double>{0.0, 1.0} * phase);
        requireFinite(closestPointDistance,
                      "simple Gaussian closest-point distance");
        requireFinite(offRayDistance, "simple Gaussian off-ray distance");
        requireFinite(angularOffset, "simple Gaussian angular offset");
        requireFiniteComplex(delay, "simple Gaussian delay");
        requireFiniteComplex(contribution,
                             "simple Gaussian pressure increment");
        const std::complex<double> updated =
            workspace.at(depthIndex, rangeIndex) + contribution;
        requireFiniteComplex(updated, "simple Gaussian accumulated pressure");
        workspace.at(depthIndex, rangeIndex) = updated;

        if (diagnosticRequest.has_value() &&
            diagnosticRequest->receiverRangeIndex == rangeIndex &&
            diagnosticRequest->receiverDepthIndex == depthIndex) {
          ++diagnostic->evaluationCount;
          if (!diagnostic->evaluated) {
            diagnostic->evaluated = true;
            diagnostic->leftPointIndex = leftIndex;
            diagnostic->rightPointIndex = rightIndex;
            diagnostic->interpolationWeight = weight;
            diagnostic->qInterpolated = q;
            diagnostic->beta = beta;
            diagnostic->gaussianA = gaussianA;
            diagnostic->normalization = normalization;
            diagnostic->legacyArcLength = legacyArcLength;
            diagnostic->closestPointDistance = closestPointDistance;
            diagnostic->offRayDistance = offRayDistance;
            diagnostic->effectiveDistance = effectiveDistance;
            diagnostic->angularOffset = angularOffset;
            diagnostic->causticPhase = causticPhase;
            diagnostic->rightAmplitude = rightAmplitude;
            diagnostic->rightReflectionPhase = rightReflectionPhase;
            diagnostic->delay = delay;
            diagnostic->pressureIncrement = contribution;
          }
        }
      }

      previousQ = q;
      ++rangeIndex;
    }
    leftRange = rightRange;
  }
  return diagnostic;
}

}  // namespace rayreuse
