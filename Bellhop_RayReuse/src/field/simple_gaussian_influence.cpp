#include "rayreuse/field/simple_gaussian_influence.hpp"

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

#include "rayreuse/error.hpp"
#include "rayreuse/field/fused_pressure_workspace.hpp"

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

// Fused-kernel entry checks (IGR-3A A06, design §5; A04 Hat / A05 Gaussian
// precedent): the per-ray conditions of the legacy validateInput with
// fused-prefixed diagnostics for the layer-identifiable dimensions, legacy
// messages verbatim for the per-ray conditions; the fused workspace replaces
// the legacy per-frequency workspace metadata checks. The legacy per-call
// whole-workspace payload rescan is deliberately not restored (CC A02 / Hat
// A04 / Gaussian A05 precedent: the store-time finite checks preserve the
// accumulation contract). No diagnostic request exists on the fused entry.
void validateFusedSimpleGaussianInput(
    const FusedPressureWorkspace& workspace,
    std::span<const double> frequencies, const RayPath& path,
    std::span<const RayFrequencyState> frequencyStates,
    const ReceiverGrid& receivers, double launchAngleSpacingRadians) {
  const std::size_t frequencyCount = frequencyStates.size();
  if (frequencyCount == 0U) {
    throw ValidationError(
        "fused simple Gaussian influence requires at least one frequency");
  }
  if (workspace.frequencyCount() != frequencyCount ||
      frequencies.size() != frequencyCount) {
    throw ValidationError(
        "fused simple Gaussian influence requires workspace, frequency, and "
        "frequency-state dimensions of equal size");
  }
  if (workspace.depthCount() != receivers.depthCount() ||
      workspace.rangeCount() != receivers.rangeCount()) {
    throw ValidationError(
        "fused simple Gaussian workspace and receiver-grid sizes must match");
  }
  if (!std::isfinite(launchAngleSpacingRadians) ||
      launchAngleSpacingRadians <= 0.0) {
    throw ValidationError(
        "simple Gaussian launch-angle spacing must be positive and finite");
  }
  if (path.points.size() < 2U) {
    throw ValidationError(
        "simple Gaussian geometry and frequency point counts must match");
  }
  for (std::size_t frequencyIndex = 0U; frequencyIndex < frequencyCount;
       ++frequencyIndex) {
    const RayFrequencyState& frequencyState =
        frequencyStates[frequencyIndex];
    if (frequencies[frequencyIndex] != frequencyState.frequency) {
      throw ValidationError(
          "fused simple Gaussian workspace and ray frequencies must match");
    }
    if (path.points.size() != frequencyState.points.size()) {
      throw ValidationError(
          "simple Gaussian geometry and frequency point counts must match");
    }
    if (!frequencyState.points.front().active) {
      throw ValidationError("simple Gaussian source point must be active");
    }
    bool inactiveSeen = false;
    for (const RayFrequencyPoint& point : frequencyState.points) {
      if (inactiveSeen && point.active) {
        throw ValidationError(
            "simple Gaussian active prefix must be contiguous");
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

void SimpleGaussianInfluence::setFusedLaunchAngleStep(
    double launchAngleStep) {
  fusedLaunchAngleStep_ = launchAngleStep;
}

// IGR-3A A06 fused kernel (design §5/§8, coherent only): entry validation,
// then the legacy Cartesian traversal reproduced exactly — segment loop ->
// monotone range cursor (advancing on every matched range, anchors
// unclamped) -> shared depth rows. The beam width is frequency-independent
// (gaussianA from the launch-angle spacing, the binary32-rounded legacy
// beta literal, normalization), so unlike the A05 Gaussian kernel there is
// no per-lane eligibility state: previousQ and causticPhase are pure
// functions of the ray path, and every frequency lane's legacy traversal
// computes the identical evolution sequence — one shared state evolved over
// the union-prefix traversal therefore reproduces each lane's legacy values
// exactly at every point the lane contributes (a lane's own legacy loop is
// a causal prefix of the union traversal). Frequency-local per lane: the
// active-prefix loop bound, tau/delay, right amplitude, reflection phase,
// angular frequency, and the pressure increment.
bool SimpleGaussianInfluence::accumulateFusedPrevalidated(
    FusedPressureWorkspace& workspace,
    std::span<const double> frequencies, const RayPath& path,
    std::span<const RayFrequencyState> frequencyStates,
    std::size_t rangeBegin, std::size_t rangeEnd,
    CartesianCervenyStatistics* statistics) const {
  // The legacy simple-Gaussian kernel produces no influence counters; the
  // pointer is accepted for adapter-shape uniformity (design §3.4 documents
  // the zero-counter envelope for non-CC families).
  static_cast<void>(statistics);
  validateFusedSimpleGaussianInput(workspace, frequencies, path,
                                   frequencyStates, receivers_,
                                   fusedLaunchAngleStep_);
  if (rangeBegin >= rangeEnd || rangeEnd > workspace.rangeCount()) {
    throw ValidationError("fused simple Gaussian range partition is invalid");
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
      (fusedLaunchAngleStep_ * fusedLaunchAngleStep_);
  const double normalization =
      fusedLaunchAngleStep_ * std::sqrt(gaussianA / std::numbers::pi);
  requireFinite(sourceRatio, "simple Gaussian source ratio");
  requireFinite(gaussianA, "simple Gaussian A");
  requireFinite(normalization, "simple Gaussian normalization");

  const std::size_t frequencyCount = frequencyStates.size();
  std::vector<std::size_t> activePrefixPointCount(frequencyCount);
  std::vector<double> angularFrequency(frequencyCount);
  std::size_t unionPrefix = 0U;
  for (std::size_t frequencyIndex = 0U; frequencyIndex < frequencyCount;
       ++frequencyIndex) {
    // Exact per-frequency active-prefix scan of the legacy kernel (the
    // first inactive point is retained).
    activePrefixPointCount[frequencyIndex] =
        activePointCount(frequencyStates[frequencyIndex]);
    unionPrefix =
        std::max(unionPrefix, activePrefixPointCount[frequencyIndex]);
    angularFrequency[frequencyIndex] =
        2.0 * std::numbers::pi * frequencyStates[frequencyIndex].frequency;
    requireFinite(angularFrequency[frequencyIndex],
                  "simple Gaussian angular frequency");
  }

  const std::vector<double>& receiverRanges = receivers_.ranges();
  std::size_t rangeIndex = 0U;
  double leftRange = path.points.front().position.range;
  double previousQ = 1.0;
  double causticPhase = 0.0;
  for (std::size_t rightIndex = 1U; rightIndex < unionPrefix; ++rightIndex) {
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
      const double legacyArcLength =
          (static_cast<double>(rightIndex) + weight) *
          configuredStepLengthMeters_;
      // The per-match caustic update and the previousQ hand-off below run on
      // EVERY matched range (anchors unclamped): later in-partition
      // contributions depend on the evolved state, exactly as in the legacy
      // single-lane loop. Validation and stores are owned by the worker
      // whose partition contains the range (A04/A05 precedent).
      if (crossesSimpleGaussianCaustic(previousQ, q)) {
        causticPhase += 0.5 * std::numbers::pi;
      }

      if (rangeIndex >= rangeBegin && rangeIndex < rangeEnd) {
        const double segmentRange = rightRange - leftRange;
        const double segmentDepth =
            path.points[rightIndex].position.depth -
            path.points[leftIndex].position.depth;
        const double segmentLength =
            std::sqrt(segmentRange * segmentRange +
                      segmentDepth * segmentDepth);
        if (!std::isfinite(segmentLength) || segmentLength <= 0.0) {
          throw ValidationError(
              "simple Gaussian ray segment length must be positive and "
              "finite");
        }
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
            throw ValidationError(
                "simple Gaussian off-ray distance is invalid");
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
          requireFinite(closestPointDistance,
                        "simple Gaussian closest-point distance");
          requireFinite(offRayDistance, "simple Gaussian off-ray distance");
          requireFinite(angularOffset, "simple Gaussian angular offset");
          for (std::size_t frequencyIndex = 0U; frequencyIndex < frequencyCount;
               ++frequencyIndex) {
            // That frequency's own legacy loop bound.
            if (rightIndex >= activePrefixPointCount[frequencyIndex]) {
              continue;
            }
            const RayFrequencyState& frequencyState =
                frequencyStates[frequencyIndex];
            const std::complex<double> tau =
                frequencyState.points[leftIndex].complexTravelTime +
                weight *
                    (frequencyState.points[rightIndex].complexTravelTime -
                     frequencyState.points[leftIndex].complexTravelTime);
            const double rightAmplitude =
                frequencyState.points[rightIndex].amplitude;
            const double rightReflectionPhase =
                frequencyState.points[rightIndex].reflectionPhase;
            const std::complex<double> delay =
                tau + interpolatedSlowness.depth * deltaDepth;
            const std::complex<double> phase =
                angularFrequency[frequencyIndex] * delay -
                rightReflectionPhase - causticPhase;
            const std::complex<double> contribution =
                (sourceRatio * normalization * rightAmplitude /
                 std::sqrt(effectiveDistance)) *
                std::exp(-gaussianA * angularOffset * angularOffset -
                         std::complex<double>{0.0, 1.0} * phase);
            requireFiniteComplex(delay, "simple Gaussian delay");
            requireFiniteComplex(contribution,
                                 "simple Gaussian pressure increment");
            std::complex<double>& pressureValue =
                workspace.cell(rangeIndex, depthIndex)[frequencyIndex];
            const std::complex<double> updated =
                pressureValue + contribution;
            requireFiniteComplex(updated,
                                 "simple Gaussian accumulated pressure");
            pressureValue = updated;
          }
        }
      }

      previousQ = q;
      ++rangeIndex;
    }
    leftRange = rightRange;
  }
  return true;
}

}  // namespace rayreuse
