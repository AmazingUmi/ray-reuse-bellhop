#include "rayreuse/field/cartesian_cerveny_influence.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstddef>
#include <limits>
#include <numbers>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "rayreuse/error.hpp"

namespace rayreuse {
namespace {

using Clock = std::chrono::steady_clock;

[[nodiscard]] double elapsedSeconds(Clock::time_point begin,
                                    Clock::time_point end) {
  return std::chrono::duration<double>(end - begin).count();
}

[[nodiscard]] bool finiteComplex(std::complex<double> value) noexcept {
  return std::isfinite(value.real()) && std::isfinite(value.imag());
}

void requireFinite(double value, std::string_view name) {
  if (!std::isfinite(value)) {
    throw ValidationError(std::string(name) + " must be finite");
  }
}

void requireFiniteComplex(std::complex<double> value, std::string_view name) {
  if (!finiteComplex(value)) {
    throw ValidationError(std::string(name) + " must be finite");
  }
}

void validateWorkspacePressure(const FrequencyWorkspace& workspace,
                               std::string_view name) {
  for (const std::complex<double> pressure : workspace.pressure()) {
    requireFiniteComplex(pressure, name);
  }
}

void validateWorkspaceIntensity(const IntensityWorkspace& workspace,
                                std::string_view name) {
  for (const double intensity : workspace.intensity()) {
    if (!std::isfinite(intensity) || intensity < 0.0) {
      throw ValidationError(std::string(name) +
                            " must be finite and non-negative");
    }
  }
}

[[nodiscard]] double floatingSpacing(double value) {
  const double next =
      std::nextafter(value, std::numeric_limits<double>::infinity());
  return next - value;
}

[[nodiscard]] std::complex<double> negativeImaginaryExponential(
    std::complex<double> phase) {
  const double magnitude = std::exp(phase.imag());
  return {magnitude * std::cos(phase.real()),
          -magnitude * std::sin(phase.real())};
}

[[nodiscard]] double cervenyHermiteTaperUnchecked(
    double offset, double fullValueRadius, double zeroValueRadius) noexcept {
  const double absoluteOffset = std::abs(offset);
  if (absoluteOffset <= fullValueRadius) {
    return 1.0;
  }
  if (absoluteOffset >= zeroValueRadius) {
    return 0.0;
  }
  const double coordinate =
      (absoluteOffset - fullValueRadius) / (zeroValueRadius - fullValueRadius);
  const double complement = 1.0 - coordinate;
  const double complementSquared = complement * complement;
  return (1.0 + 2.0 * coordinate) * complementSquared;
}

[[nodiscard]] std::size_t fortranUpperRangeIndex(
    double range, const std::vector<double>& receivers, double rangeDelta) {
  const double rawIndex =
      std::trunc((range - receivers.front()) / rangeDelta) + 1.0;
  if (rawIndex <= 1.0) {
    return 1U;
  }
  const double count = static_cast<double>(receivers.size());
  if (rawIndex >= count) {
    return receivers.size();
  }
  return static_cast<std::size_t>(rawIndex);
}

void validateUniformReceiverRanges(const ReceiverGrid& receivers,
                                   double rangeDelta) {
  if (receivers.rangeCount() < 2U) {
    throw ValidationError(
        "Cartesian Cerveny influence requires at least two receiver ranges");
  }
  const std::vector<double>& ranges = receivers.ranges();
  for (std::size_t index = 2U; index < ranges.size(); ++index) {
    const double expected =
        ranges.front() + static_cast<double>(index) * rangeDelta;
    const double tolerance =
        32.0 * std::numeric_limits<double>::epsilon() *
        std::max({1.0, std::abs(expected), std::abs(ranges[index])});
    if (std::abs(ranges[index] - expected) > tolerance) {
      throw ValidationError(
          "Cartesian Cerveny receiver ranges must be equally spaced");
    }
  }
}

void validateAccumulateInput(
    const FrequencyWorkspace* pressureWorkspace,
    const IntensityWorkspace* intensityWorkspace, const RayPath& path,
    const RayFrequencyState& frequencyState, std::complex<double> epsilon,
    const ReceiverGrid& receivers, BeamWidthMode widthMode,
    const std::optional<CartesianCervenyDiagnosticRequest>& request) {
  if ((pressureWorkspace == nullptr) == (intensityWorkspace == nullptr)) {
    throw ValidationError(
        "Cartesian Cerveny influence requires exactly one workspace");
  }
  const double workspaceFrequency = pressureWorkspace != nullptr
                                        ? pressureWorkspace->frequency()
                                        : intensityWorkspace->frequency();
  const std::size_t workspaceDepthCount =
      pressureWorkspace != nullptr ? pressureWorkspace->depthCount()
                                   : intensityWorkspace->depthCount();
  const std::size_t workspaceRangeCount =
      pressureWorkspace != nullptr ? pressureWorkspace->rangeCount()
                                   : intensityWorkspace->rangeCount();
  if (path.points.empty()) {
    throw ValidationError(
        "Cartesian Cerveny influence requires a non-empty ray path");
  }
  if (path.points.size() != frequencyState.points.size()) {
    throw ValidationError(
        "Cartesian Cerveny geometry and frequency-state sizes must match");
  }
  if (workspaceFrequency != frequencyState.frequency) {
    throw ValidationError(
        "Cartesian Cerveny workspace and ray frequencies must match");
  }
  if (!frequencyState.points.front().active) {
    throw ValidationError(
        "Cartesian Cerveny source frequency point must be active");
  }
  bool inactiveSeen = false;
  for (const RayFrequencyPoint& point : frequencyState.points) {
    if (inactiveSeen && point.active) {
      throw ValidationError(
          "Cartesian Cerveny active state must remain inactive once stopped");
    }
    inactiveSeen = inactiveSeen || !point.active;
    requireFiniteComplex(point.complexTravelTime,
                         "Cartesian Cerveny complex travel time");
    requireFinite(point.amplitude, "Cartesian Cerveny amplitude");
    requireFinite(point.reflectionPhase, "Cartesian Cerveny reflection phase");
    if (point.amplitude < 0.0) {
      throw ValidationError("Cartesian Cerveny amplitude must be non-negative");
    }
  }
  if (workspaceDepthCount != receivers.receiversPerRange() ||
      workspaceRangeCount != receivers.rangeCount()) {
    throw ValidationError(
        "Cartesian Cerveny workspace and receiver-grid sizes must match");
  }
  if (pressureWorkspace != nullptr) {
    validateWorkspacePressure(*pressureWorkspace,
                              "Cartesian Cerveny existing workspace pressure");
  } else {
    validateWorkspaceIntensity(
        *intensityWorkspace, "Cartesian Cerveny existing workspace intensity");
  }
  if (!std::isfinite(path.launchAngle)) {
    throw ValidationError("Cartesian Cerveny launch angle must be finite");
  }
  double previousTravelTime = 0.0;
  for (const RayState& point : path.points) {
    if (!isFinite(point.position) || !isFinite(point.slowness) ||
        !std::isfinite(point.dynamicP[0U]) ||
        !std::isfinite(point.dynamicP[1U]) ||
        !std::isfinite(point.dynamicQ[0U]) ||
        !std::isfinite(point.dynamicQ[1U]) ||
        !std::isfinite(point.soundSpeed) ||
        !std::isfinite(point.realTravelTime)) {
      throw ValidationError(
          "Cartesian Cerveny ray path must contain only finite states");
    }
    if (point.soundSpeed <= 0.0) {
      throw ValidationError(
          "Cartesian Cerveny ray-state sound speed must be positive");
    }
    if (point.realTravelTime < previousTravelTime) {
      throw ValidationError(
          "Cartesian Cerveny ray-state travel time must be non-decreasing");
    }
    previousTravelTime = point.realTravelTime;
  }
  requireFiniteComplex(epsilon, "Cartesian Cerveny epsilon");
  if (widthMode == BeamWidthMode::Wkb && epsilon.imag() != 0.0) {
    throw ValidationError("Cartesian Cerveny WKB epsilon must be real");
  }
  if (widthMode != BeamWidthMode::Wkb &&
      (epsilon.real() != 0.0 || epsilon.imag() <= 0.0)) {
    throw ValidationError(
        "Cartesian Cerveny F/M epsilon must be positive imaginary");
  }
  if (request.has_value() &&
      (request->receiverRangeIndex >= receivers.rangeCount() ||
       request->receiverDepthIndex >= receivers.receiversPerRange())) {
    throw ValidationError(
        "Cartesian Cerveny diagnostic receiver index is out of range");
  }
}

void validatePrevalidatedInput(const FrequencyWorkspace* pressureWorkspace,
                               const IntensityWorkspace* intensityWorkspace,
                               const RayPath& path,
                               const RayFrequencyState& frequencyState,
                               std::complex<double> epsilon,
                               const ReceiverGrid& receivers,
                               BeamWidthMode widthMode) {
  if ((pressureWorkspace == nullptr) == (intensityWorkspace == nullptr)) {
    throw ValidationError(
        "prevalidated Cartesian Cerveny influence requires exactly one "
        "workspace");
  }
  const double workspaceFrequency = pressureWorkspace != nullptr
                                        ? pressureWorkspace->frequency()
                                        : intensityWorkspace->frequency();
  const std::size_t workspaceDepthCount =
      pressureWorkspace != nullptr ? pressureWorkspace->depthCount()
                                   : intensityWorkspace->depthCount();
  const std::size_t workspaceRangeCount =
      pressureWorkspace != nullptr ? pressureWorkspace->rangeCount()
                                   : intensityWorkspace->rangeCount();
  if (path.points.empty() ||
      path.points.size() != frequencyState.points.size()) {
    throw ValidationError(
        "prevalidated Cartesian Cerveny geometry and frequency-state "
        "sizes must match and be non-empty");
  }
  if (workspaceFrequency != frequencyState.frequency) {
    throw ValidationError(
        "prevalidated Cartesian Cerveny workspace and ray frequencies "
        "must match");
  }
  if (workspaceDepthCount != receivers.receiversPerRange() ||
      workspaceRangeCount != receivers.rangeCount()) {
    throw ValidationError(
        "prevalidated Cartesian Cerveny workspace and receiver-grid "
        "sizes must match");
  }
  if (!frequencyState.points.front().active) {
    throw ValidationError(
        "prevalidated Cartesian Cerveny source frequency point must "
        "be active");
  }
  if (!finiteComplex(epsilon)) {
    throw ValidationError(
        "prevalidated Cartesian Cerveny epsilon must be finite");
  }
  if (widthMode == BeamWidthMode::Wkb && epsilon.imag() != 0.0) {
    throw ValidationError(
        "prevalidated Cartesian Cerveny WKB epsilon must be real");
  }
  if (widthMode != BeamWidthMode::Wkb &&
      (epsilon.real() != 0.0 || epsilon.imag() <= 0.0)) {
    throw ValidationError(
        "prevalidated Cartesian Cerveny F/M epsilon must be "
        "positive imaginary");
  }
}

struct PrecomputedRayValues {
  std::vector<std::complex<double>> p;
  std::vector<std::complex<double>> q;
  std::vector<std::complex<double>> gamma;
  std::vector<int> kmah;
};

// Fused-kernel entry checks: identical conditions to validatePrevalidatedInput
// (pressure-workspace route) with fused-prefixed diagnostics so the failing
// layer is identifiable (design §4.2 / Obligation P5).
void validateFusedPrevalidatedInput(const FrequencyWorkspace& workspace,
                                    const RayPath& path,
                                    const RayFrequencyState& frequencyState,
                                    std::complex<double> epsilon,
                                    const ReceiverGrid& receivers,
                                    BeamWidthMode widthMode) {
  if (path.points.empty() ||
      path.points.size() != frequencyState.points.size()) {
    throw ValidationError(
        "fused Cartesian Cerveny geometry and frequency-state sizes must "
        "match and be non-empty");
  }
  if (workspace.frequency() != frequencyState.frequency) {
    throw ValidationError(
        "fused Cartesian Cerveny workspace and ray frequencies must match");
  }
  if (workspace.depthCount() != receivers.receiversPerRange() ||
      workspace.rangeCount() != receivers.rangeCount()) {
    throw ValidationError(
        "fused Cartesian Cerveny workspace and receiver-grid sizes must "
        "match");
  }
  if (!frequencyState.points.front().active) {
    throw ValidationError(
        "fused Cartesian Cerveny source frequency point must be active");
  }
  if (!finiteComplex(epsilon)) {
    throw ValidationError("fused Cartesian Cerveny epsilon must be finite");
  }
  if (widthMode == BeamWidthMode::Wkb && epsilon.imag() != 0.0) {
    throw ValidationError(
        "fused Cartesian Cerveny WKB epsilon must be real");
  }
  if (widthMode != BeamWidthMode::Wkb &&
      (epsilon.real() != 0.0 || epsilon.imag() <= 0.0)) {
    throw ValidationError(
        "fused Cartesian Cerveny F/M epsilon must be positive imaginary");
  }
}

[[nodiscard]] PrecomputedRayValues precomputeRayValues(
    const RayPath& path, const GeometrySspEvaluator& soundSpeedProfile,
    std::complex<double> epsilon, std::size_t pointCount,
    BeamWidthMode widthMode) {
  PrecomputedRayValues values;
  values.p.reserve(pointCount);
  values.q.reserve(pointCount);
  values.gamma.reserve(pointCount);
  values.kmah.reserve(pointCount);

  std::size_t segmentIndex = 0U;
  for (std::size_t index = 0U; index < pointCount; ++index) {
    const RayState& point = path.points[index];
    const SoundSpeedSample sample =
        soundSpeedProfile.evaluate(point.position, segmentIndex);
    segmentIndex = sample.segmentIndex;

    const std::complex<double> p =
        point.dynamicP[0U] + epsilon * point.dynamicP[1U];
    const std::complex<double> q =
        point.dynamicQ[0U] + epsilon * point.dynamicQ[1U];
    const Vec2 tangent = point.soundSpeed * point.slowness;
    const Vec2 normal{.range = tangent.depth, .depth = -tangent.range};
    const double soundSpeedSquared = sample.soundSpeed * sample.soundSpeed;
    const double alongGradient = dot(sample.soundSpeedGradient, tangent);
    const double normalGradient = dot(sample.soundSpeedGradient, normal);
    const double tangentRangeSquared = tangent.range * tangent.range;
    const double tangentDepthSquared = tangent.depth * tangent.depth;

    std::complex<double> gamma{};
    if (q != std::complex<double>{}) {
      gamma = 0.5 * (p / q * tangentRangeSquared +
                     2.0 * normalGradient / soundSpeedSquared * tangent.depth *
                         tangent.range -
                     alongGradient / soundSpeedSquared * tangentDepthSquared);
    }
    requireFiniteComplex(p, "Cartesian Cerveny p");
    requireFiniteComplex(q, "Cartesian Cerveny q");
    requireFiniteComplex(gamma, "Cartesian Cerveny gamma");
    values.p.push_back(p);
    values.q.push_back(q);
    values.gamma.push_back(gamma);

    int kmah = index == 0U ? 1 : values.kmah.back();
    if (index != 0U) {
      kmah = updateCervenyKmah(values.q[index - 1U], q, kmah, widthMode);
    }
    values.kmah.push_back(kmah);
  }
  return values;
}

[[nodiscard]] CartesianCervenyImageDiagnostic evaluateImage(
    CervenyImageKind kind, double receiverDepth, double interpolatedDepth,
    double seaSurfaceDepth, double seabedDepth, double angularFrequency,
    double beamWindowSquared, double radiusMax, Vec2 interpolatedSlowness,
    std::complex<double> tau, std::complex<double> gamma, double amplitude,
    double reflectionPhase) {
  double deltaDepth = 0.0;
  double polarity = 1.0;
  switch (kind) {
    case CervenyImageKind::True:
      deltaDepth = receiverDepth - interpolatedDepth;
      polarity = 1.0;
      break;
    case CervenyImageKind::Surface:
      deltaDepth = -receiverDepth + 2.0 * seaSurfaceDepth - interpolatedDepth;
      polarity = -1.0;
      break;
    case CervenyImageKind::Bottom:
      deltaDepth = -receiverDepth + 2.0 * seabedDepth - interpolatedDepth;
      polarity = 1.0;
      break;
  }

  const double deltaSquared = deltaDepth * deltaDepth;
  const double windowMetric = -angularFrequency * gamma.imag() * deltaSquared;
  const bool windowPassed = windowMetric < beamWindowSquared;
  const double taper =
      cervenyHermiteTaperUnchecked(deltaDepth, radiusMax, 2.0 * radiusMax);
  std::complex<double> exponential{};
  std::complex<double> contribution{};
  if (windowPassed) {
    const std::complex<double> phaseArgument =
        angularFrequency * (tau + interpolatedSlowness.depth * deltaDepth +
                            gamma * deltaSquared) -
        reflectionPhase;
    exponential = negativeImaginaryExponential(phaseArgument);
    contribution = polarity * amplitude * taper * exponential;
  }
  requireFinite(windowMetric, "Cartesian Cerveny window metric");
  requireFinite(taper, "Cartesian Cerveny Hermite taper");
  requireFiniteComplex(exponential, "Cartesian Cerveny image exponential");
  requireFiniteComplex(contribution, "Cartesian Cerveny image contribution");
  return CartesianCervenyImageDiagnostic{.kind = kind,
                                         .deltaDepth = deltaDepth,
                                         .polarity = polarity,
                                         .windowMetric = windowMetric,
                                         .windowPassed = windowPassed,
                                         .hermiteTaper = taper,
                                         .exponential = exponential,
                                         .contribution = contribution};
}

template <bool CollectStatistics, CervenyImageKind Kind>
[[nodiscard]] std::complex<double> evaluateImageContribution(
    double receiverDepth, double interpolatedDepth, double seaSurfaceDepth,
    double seabedDepth, double angularFrequency, double beamWindowSquared,
    double radiusMax, Vec2 interpolatedSlowness, std::complex<double> tau,
    std::complex<double> gamma, double amplitude, double reflectionPhase,
    CartesianCervenyStatistics* statistics) {
  if constexpr (CollectStatistics) {
    ++statistics->imageEvaluations;
    ++statistics->frequencyImageKernelEvaluations;
  }
  double deltaDepth = 0.0;
  double polarity = 1.0;
  if constexpr (Kind == CervenyImageKind::True) {
    deltaDepth = receiverDepth - interpolatedDepth;
  } else if constexpr (Kind == CervenyImageKind::Surface) {
    deltaDepth = -receiverDepth + 2.0 * seaSurfaceDepth - interpolatedDepth;
    polarity = -1.0;
  } else {
    static_assert(Kind == CervenyImageKind::Bottom);
    deltaDepth = -receiverDepth + 2.0 * seabedDepth - interpolatedDepth;
  }
  if constexpr (CollectStatistics) {
    ++statistics->geometryImageGeometryEvaluations;
  }

  const double deltaSquared = deltaDepth * deltaDepth;
  const double windowMetric = -angularFrequency * gamma.imag() * deltaSquared;
#ifndef NDEBUG
  requireFinite(windowMetric, "Cartesian Cerveny window metric");
#endif
  if (windowMetric >= beamWindowSquared) {
    if constexpr (CollectStatistics) {
      ++statistics->windowRejections;
    }
    return {};
  }

  const double taper =
      cervenyHermiteTaperUnchecked(deltaDepth, radiusMax, 2.0 * radiusMax);
  if (taper == 0.0) {
    if constexpr (CollectStatistics) {
      ++statistics->taperRejections;
    }
    return {};
  }
  const std::complex<double> phaseArgument =
      angularFrequency * (tau + interpolatedSlowness.depth * deltaDepth +
                          gamma * deltaSquared) -
      reflectionPhase;
  const std::complex<double> contribution =
      polarity * amplitude * taper *
      negativeImaginaryExponential(phaseArgument);
#ifndef NDEBUG
  requireFiniteComplex(contribution, "Cartesian Cerveny image contribution");
#endif
  if constexpr (CollectStatistics) {
    if (contribution != std::complex<double>{}) {
      ++statistics->nonzeroImageContributions;
    }
  }
  return contribution;
}

template <std::size_t ImageCount, bool CollectStatistics>
[[nodiscard]] std::complex<double> evaluateImageContributions(
    double receiverDepth, double interpolatedDepth, double seaSurfaceDepth,
    double seabedDepth, double angularFrequency, double beamWindowSquared,
    double radiusMax, Vec2 interpolatedSlowness, std::complex<double> tau,
    std::complex<double> gamma, double amplitude, double reflectionPhase,
    CartesianCervenyStatistics* statistics) {
  static_assert(ImageCount >= 1U && ImageCount <= 3U);
  std::complex<double> imageSum{};
  imageSum +=
      evaluateImageContribution<CollectStatistics, CervenyImageKind::True>(
          receiverDepth, interpolatedDepth, seaSurfaceDepth, seabedDepth,
          angularFrequency, beamWindowSquared, radiusMax, interpolatedSlowness,
          tau, gamma, amplitude, reflectionPhase, statistics);
  if constexpr (ImageCount >= 2U) {
    imageSum +=
        evaluateImageContribution<CollectStatistics, CervenyImageKind::Surface>(
            receiverDepth, interpolatedDepth, seaSurfaceDepth, seabedDepth,
            angularFrequency, beamWindowSquared, radiusMax,
            interpolatedSlowness, tau, gamma, amplitude, reflectionPhase,
            statistics);
  }
  if constexpr (ImageCount >= 3U) {
    imageSum +=
        evaluateImageContribution<CollectStatistics, CervenyImageKind::Bottom>(
            receiverDepth, interpolatedDepth, seaSurfaceDepth, seabedDepth,
            angularFrequency, beamWindowSquared, radiusMax,
            interpolatedSlowness, tau, gamma, amplitude, reflectionPhase,
            statistics);
  }
  return imageSum;
}

}  // namespace

int updateCervenyKmah(std::complex<double> qLeft, std::complex<double> qRight,
                      int currentKmah, BeamWidthMode widthMode) {
  requireFiniteComplex(qLeft, "Cerveny branch-cut left q");
  requireFiniteComplex(qRight, "Cerveny branch-cut right q");
  if (currentKmah != -1 && currentKmah != 1) {
    throw ValidationError("Cerveny KMAH must be -1 or +1");
  }
  if (widthMode == BeamWidthMode::Wkb) {
    const double leftReal = qLeft.real();
    const double rightReal = qRight.real();
    if ((leftReal < 0.0 && rightReal >= 0.0) ||
        (leftReal > 0.0 && rightReal <= 0.0)) {
      return -currentKmah;
    }
    return currentKmah;
  }
  if (widthMode != BeamWidthMode::SpaceFilling &&
      widthMode != BeamWidthMode::MinimumWidth) {
    throw ValidationError("unknown Cerveny beam-width mode");
  }
  if (qRight.real() < 0.0) {
    const double leftImaginary = qLeft.imag();
    const double rightImaginary = qRight.imag();
    if ((leftImaginary < 0.0 && rightImaginary >= 0.0) ||
        (leftImaginary > 0.0 && rightImaginary <= 0.0)) {
      return -currentKmah;
    }
  }
  return currentKmah;
}

double cervenyHermiteTaper(double offset, double fullValueRadius,
                           double zeroValueRadius) {
  requireFinite(offset, "Hermite offset");
  requireFinite(fullValueRadius, "Hermite full-value radius");
  requireFinite(zeroValueRadius, "Hermite zero-value radius");
  if (fullValueRadius < 0.0 || zeroValueRadius <= fullValueRadius) {
    throw ValidationError("Hermite radii must satisfy 0 <= full < zero");
  }

  return cervenyHermiteTaperUnchecked(offset, fullValueRadius, zeroValueRadius);
}

void accumulateCartesianCervenyStatistics(
    CartesianCervenyStatistics& total,
    const CartesianCervenyStatistics& value) noexcept {
  total.rayAccumulations += value.rayAccumulations;
  total.validatedRayPoints += value.validatedRayPoints;
  total.validatedWorkspaceValues += value.validatedWorkspaceValues;
  total.activeRayPoints += value.activeRayPoints;
  total.segmentCandidates += value.segmentCandidates;
  total.eligibleSegments += value.eligibleSegments;
  total.receiverRangeEvaluations += value.receiverRangeEvaluations;
  total.receiverDepthEvaluations += value.receiverDepthEvaluations;
  total.imageEvaluations += value.imageEvaluations;
  total.windowRejections += value.windowRejections;
  total.taperRejections += value.taperRejections;
  total.nonzeroImageContributions += value.nonzeroImageContributions;
  total.geometrySegmentEvaluations += value.geometrySegmentEvaluations;
  total.geometryRangeEvaluations += value.geometryRangeEvaluations;
  total.geometryDepthEvaluations += value.geometryDepthEvaluations;
  total.geometryImageGeometryEvaluations +=
      value.geometryImageGeometryEvaluations;
  total.frequencyRangeKernelEvaluations +=
      value.frequencyRangeKernelEvaluations;
  total.frequencyImageKernelEvaluations +=
      value.frequencyImageKernelEvaluations;
  total.validationSeconds += value.validationSeconds;
  total.precomputeSeconds += value.precomputeSeconds;
  total.hotLoopSeconds += value.hotLoopSeconds;
}

CartesianCervenyInfluence::CartesianCervenyInfluence(
    Environment environment, ReceiverGrid receivers,
    CartesianCervenySettings settings, BeamWidthMode widthMode,
    SourceGeometry sourceGeometry)
    : environment_(std::move(environment)),
      receivers_(std::move(receivers)),
      settings_(settings),
      widthMode_(widthMode),
      sourceGeometry_(sourceGeometry),
      soundSpeedProfile_(environment_.soundSpeedProfile()),
      receiverRangeDelta_(receivers_.rangeCount() >= 2U
                              ? receivers_.ranges()[1U] -
                                    receivers_.ranges()[0U]
                              : 0.0) {
  if (settings_.imageCount == 0U || settings_.imageCount > 3U) {
    throw ValidationError("Cartesian Cerveny image count must lie in [1, 3]");
  }
  if (settings_.beamWindow <= 0) {
    throw ValidationError("Cartesian Cerveny beam window must be positive");
  }
  switch (widthMode_) {
    case BeamWidthMode::SpaceFilling:
    case BeamWidthMode::MinimumWidth:
    case BeamWidthMode::Wkb:
      break;
    default:
      throw ValidationError("Cartesian Cerveny beam width mode is invalid");
  }
  validateUniformReceiverRanges(receivers_, receiverRangeDelta_);
}

template <bool CollectStatistics>
std::optional<CartesianCervenyDiagnostic>
CartesianCervenyInfluence::accumulateWithImageCount(
    FrequencyWorkspace* pressureWorkspace,
    IntensityWorkspace* intensityWorkspace, const RayPath& path,
    const RayFrequencyState& frequencyState, std::complex<double> epsilon,
    std::optional<CartesianCervenyDiagnosticRequest> diagnosticRequest,
    CartesianCervenyStatistics* statistics) const {
  if (settings_.imageCount == 1U) {
    return accumulateImpl<CollectStatistics, 1U>(
        pressureWorkspace, intensityWorkspace, path, frequencyState, epsilon,
        diagnosticRequest, statistics);
  }
  if (settings_.imageCount == 2U) {
    return accumulateImpl<CollectStatistics, 2U>(
        pressureWorkspace, intensityWorkspace, path, frequencyState, epsilon,
        diagnosticRequest, statistics);
  }
  return accumulateImpl<CollectStatistics, 3U>(
      pressureWorkspace, intensityWorkspace, path, frequencyState, epsilon,
      diagnosticRequest, statistics);
}

template <bool CollectStatistics, std::size_t ImageCount>
std::optional<CartesianCervenyDiagnostic>
CartesianCervenyInfluence::accumulateImpl(
    FrequencyWorkspace* pressureWorkspace,
    IntensityWorkspace* intensityWorkspace, const RayPath& path,
    const RayFrequencyState& frequencyState, std::complex<double> epsilon,
    std::optional<CartesianCervenyDiagnosticRequest> diagnosticRequest,
    CartesianCervenyStatistics* statistics) const {
  if constexpr (CollectStatistics) {
    ++statistics->rayAccumulations;
  }

  std::optional<CartesianCervenyDiagnostic> diagnostic;
  if (diagnosticRequest.has_value()) {
    diagnostic.emplace();
    diagnostic->receiverRangeIndex = diagnosticRequest->receiverRangeIndex;
    diagnostic->receiverDepthIndex = diagnosticRequest->receiverDepthIndex;
  }

  Clock::time_point precomputeBegin{};
  if constexpr (CollectStatistics) {
    precomputeBegin = Clock::now();
  }
  std::size_t activePrefixPointCount = path.points.size();
  for (std::size_t index = 0U; index < frequencyState.points.size(); ++index) {
    if (!frequencyState.points[index].active) {
      activePrefixPointCount = index + 1U;
      break;
    }
  }
  const PrecomputedRayValues ray = precomputeRayValues(
      path, soundSpeedProfile_, epsilon, activePrefixPointCount, widthMode_);
  if constexpr (CollectStatistics) {
    statistics->activeRayPoints += activePrefixPointCount;
    statistics->precomputeSeconds +=
        elapsedSeconds(precomputeBegin, Clock::now());
  }
  const double angularFrequency =
      2.0 * std::numbers::pi * frequencyState.frequency;
  const double radiusMax =
      30.0 * path.points.front().soundSpeed / frequencyState.frequency;
  const double beamWindowSquared = static_cast<double>(settings_.beamWindow) *
                                   static_cast<double>(settings_.beamWindow);
  const double ratio = sourceGeometry_ == SourceGeometry::Line
                           ? 1.0
                           : std::sqrt(std::abs(std::cos(path.launchAngle)));
  const std::vector<double>& receiverRanges = receivers_.ranges();
  const std::vector<double>& receiverDepths = receivers_.depths();
  const std::size_t receiversPerRange = receivers_.receiversPerRange();
  // InfluenceCervenyCart in the 2-D Origin tree allocates one pressure row
  // for an irregular grid but still reads Pos%Rz(iz), where iz is always
  // one, instead of Pos%Rz(ir).  F2CPP preserves that observable legacy
  // behavior for CC, so a paired irregular CC run evaluates every range at
  // the first depth; the complete coordinate vectors remain in the SHD
  // header for compatibility with the irregular file layout.
  const bool irregularReceivers = receivers_.isIrregular();
  const double irregularReceiverDepth = receiverDepths.front();
  const double seaSurfaceDepth = environment_.seaSurface().depth();
  const double seabedDepth = environment_.seabed().depth();
  const std::span<std::complex<double>> pressure =
      pressureWorkspace != nullptr ? pressureWorkspace->pressure()
                                   : std::span<std::complex<double>>{};
  const std::size_t receiverRangeCount = receiverRanges.size();
  Clock::time_point hotLoopBegin{};
  if constexpr (CollectStatistics) {
    hotLoopBegin = Clock::now();
  }

  for (std::size_t rightIndex = 2U; rightIndex < activePrefixPointCount;
       ++rightIndex) {
    if constexpr (CollectStatistics) {
      ++statistics->segmentCandidates;
      ++statistics->geometrySegmentEvaluations;
    }
    const std::size_t leftIndex = rightIndex - 1U;
    const double rightAmplitude = frequencyState.points[rightIndex].amplitude;
    const double rightReflectionPhase =
        frequencyState.points[rightIndex].reflectionPhase;
    const double leftRange = path.points[leftIndex].position.range;
    const double rightRange = path.points[rightIndex].position.range;
    if (rightRange > receiverRanges.back()) {
      if constexpr (CollectStatistics) {
        statistics->hotLoopSeconds +=
            elapsedSeconds(hotLoopBegin, Clock::now());
      }
      return diagnostic;
    }
    if (std::abs(rightRange - leftRange) <
        1000.0 * floatingSpacing(rightRange)) {
      continue;
    }
    // active means "may continue from this point".  The first inactive point
    // is still the terminal point retained by legacy Beam%Nsteps, so the
    // segment ending there remains eligible; only a false left endpoint
    // suppresses the geometry suffix.
    if (!frequencyState.points[leftIndex].active) {
      continue;
    }

    const std::size_t firstUpper =
        fortranUpperRangeIndex(leftRange, receiverRanges, receiverRangeDelta_);
    const std::size_t secondUpper =
        fortranUpperRangeIndex(rightRange, receiverRanges, receiverRangeDelta_);
    if (firstUpper >= secondUpper) {
      continue;
    }
    if constexpr (CollectStatistics) {
      ++statistics->eligibleSegments;
    }

    for (std::size_t oneBasedRange = firstUpper + 1U;
         oneBasedRange <= secondUpper; ++oneBasedRange) {
      if constexpr (CollectStatistics) {
        ++statistics->receiverRangeEvaluations;
        ++statistics->geometryRangeEvaluations;
      }
      const std::size_t rangeIndex = oneBasedRange - 1U;
      const double weight =
          (receiverRanges[rangeIndex] - leftRange) / (rightRange - leftRange);
      const Vec2 position = path.points[leftIndex].position +
                            weight * (path.points[rightIndex].position -
                                      path.points[leftIndex].position);
      const Vec2 slowness = path.points[leftIndex].slowness +
                            weight * (path.points[rightIndex].slowness -
                                      path.points[leftIndex].slowness);
      const double soundSpeed = path.points[leftIndex].soundSpeed +
                                weight * (path.points[rightIndex].soundSpeed -
                                          path.points[leftIndex].soundSpeed);
      if constexpr (CollectStatistics) {
        // The shared range geometry (position/slowness/sound-speed
        // interpolation) is complete; the remaining range work
        // (q/tau/gamma interpolation, guard, principal) is
        // frequency-kernel work on the prepared geometry.
        ++statistics->frequencyRangeKernelEvaluations;
      }
      const std::complex<double> q =
          ray.q[leftIndex] + weight * (ray.q[rightIndex] - ray.q[leftIndex]);
      const std::complex<double> tau =
          frequencyState.points[leftIndex].complexTravelTime +
          weight * (frequencyState.points[rightIndex].complexTravelTime -
                    frequencyState.points[leftIndex].complexTravelTime);
      const std::complex<double> gamma =
          ray.gamma[leftIndex] +
          weight * (ray.gamma[rightIndex] - ray.gamma[leftIndex]);
      if (gamma.imag() > 0.0) {
        continue;
      }

      const std::complex<double> principal =
          ratio * std::sqrt(soundSpeed * std::abs(epsilon) / q);
      int finalKmah = ray.kmah[leftIndex];
      finalKmah = updateCervenyKmah(ray.q[leftIndex], q, finalKmah, widthMode_);
      const std::complex<double> corrected =
          finalKmah < 0 ? -principal : principal;
      requireFiniteComplex(principal, "Cartesian Cerveny principal constant");
      requireFiniteComplex(corrected, "Cartesian Cerveny corrected constant");

      for (std::size_t depthIndex = 0U; depthIndex < receiversPerRange;
           ++depthIndex) {
        if constexpr (CollectStatistics) {
          ++statistics->receiverDepthEvaluations;
          ++statistics->geometryDepthEvaluations;
        }
        const double receiverDepth = irregularReceivers
                                         ? irregularReceiverDepth
                                         : receiverDepths[depthIndex];
        const bool captureDiagnostic =
            diagnosticRequest.has_value() &&
            diagnosticRequest->receiverRangeIndex == rangeIndex &&
            diagnosticRequest->receiverDepthIndex == depthIndex;
        std::complex<double> imageSum{};
        std::array<CartesianCervenyImageDiagnostic, 3> images;
        if (captureDiagnostic) {
          images = {};
          for (std::size_t imageIndex = 0U; imageIndex < ImageCount;
               ++imageIndex) {
            if constexpr (CollectStatistics) {
              ++statistics->imageEvaluations;
              ++statistics->frequencyImageKernelEvaluations;
              ++statistics->geometryImageGeometryEvaluations;
            }
            const CervenyImageKind kind =
                imageIndex == 0U
                    ? CervenyImageKind::True
                    : (imageIndex == 1U ? CervenyImageKind::Surface
                                        : CervenyImageKind::Bottom);
            images[imageIndex] = evaluateImage(
                kind, receiverDepth, position.depth, seaSurfaceDepth,
                seabedDepth, angularFrequency, beamWindowSquared, radiusMax,
                slowness, tau, gamma, rightAmplitude, rightReflectionPhase);
            if constexpr (CollectStatistics) {
              if (!images[imageIndex].windowPassed) {
                ++statistics->windowRejections;
              } else if (images[imageIndex].hermiteTaper == 0.0) {
                ++statistics->taperRejections;
              } else if (images[imageIndex].contribution !=
                         std::complex<double>{}) {
                ++statistics->nonzeroImageContributions;
              }
            }
            imageSum += images[imageIndex].contribution;
          }
        } else {
          imageSum = evaluateImageContributions<ImageCount, CollectStatistics>(
              receiverDepth, position.depth, seaSurfaceDepth, seabedDepth,
              angularFrequency, beamWindowSquared, radiusMax, slowness, tau,
              gamma, rightAmplitude, rightReflectionPhase, statistics);
        }
        const std::complex<double> contribution = corrected * imageSum;
#ifndef NDEBUG
        requireFiniteComplex(contribution,
                             "Cartesian Cerveny final contribution");
#endif
        double intensityIncrement = 0.0;
        if (intensityWorkspace == nullptr) {
          std::complex<double>& pressureValue =
              pressure[depthIndex * receiverRangeCount + rangeIndex];
          const std::complex<double> updatedPressure =
              pressureValue + contribution;
#ifndef NDEBUG
          requireFiniteComplex(
              updatedPressure,
              "Cartesian Cerveny accumulated workspace pressure");
#endif
          pressureValue = updatedPressure;
        } else {
          // Origin coherently sums the true/surface/bottom images for one
          // beam, forms ABS(z)**2, and only then adds power from different
          // beams. std::norm is intentionally avoided because its rounding
          // and overflow path differs from ABS followed by multiplication.
          const double magnitude = std::abs(contribution);
          intensityIncrement = magnitude * magnitude;
          if (!std::isfinite(magnitude) || !std::isfinite(intensityIncrement) ||
              intensityIncrement < 0.0) {
            throw ValidationError(
                "Cartesian Cerveny intensity increment must be finite and "
                "non-negative");
          }
          intensityWorkspace->add(depthIndex, rangeIndex, intensityIncrement);
        }

        if (captureDiagnostic) {
          ++diagnostic->evaluationCount;
          if (diagnostic->evaluationCount == 1U) {
            diagnostic->evaluated = true;
            diagnostic->leftPointIndex = leftIndex;
            diagnostic->rightPointIndex = rightIndex;
            diagnostic->kmahLeft = ray.kmah[leftIndex];
            diagnostic->kmahFinal = finalKmah;
            diagnostic->interpolationWeight = weight;
            diagnostic->interpolatedPosition = position;
            diagnostic->interpolatedSlowness = slowness;
            diagnostic->interpolatedSoundSpeed = soundSpeed;
            diagnostic->rightAmplitude = rightAmplitude;
            diagnostic->rightPhase = rightReflectionPhase;
            diagnostic->epsilonLeft = epsilon;
            diagnostic->pLeft = ray.p[leftIndex];
            diagnostic->pRight = ray.p[rightIndex];
            diagnostic->qLeft = ray.q[leftIndex];
            diagnostic->qRight = ray.q[rightIndex];
            diagnostic->qInterpolated = q;
            diagnostic->tauInterpolated = tau;
            diagnostic->gammaLeft = ray.gamma[leftIndex];
            diagnostic->gammaRight = ray.gamma[rightIndex];
            diagnostic->gammaInterpolated = gamma;
            diagnostic->constantPrincipal = principal;
            diagnostic->constantCorrected = corrected;
            diagnostic->images = images;
            diagnostic->rawImageSum = imageSum;
            diagnostic->finalContribution = contribution;
            diagnostic->intensityIncrement = intensityIncrement;
          }
        }
      }
    }
  }
  if constexpr (CollectStatistics) {
    statistics->hotLoopSeconds += elapsedSeconds(hotLoopBegin, Clock::now());
  }
  return diagnostic;
}

std::optional<CartesianCervenyDiagnostic> CartesianCervenyInfluence::accumulate(
    FrequencyWorkspace& workspace, const RayPath& path,
    const RayFrequencyState& frequencyState, std::complex<double> epsilon,
    std::optional<CartesianCervenyDiagnosticRequest> diagnosticRequest,
    CartesianCervenyStatistics* statistics) const {
  if (statistics != nullptr) {
    const Clock::time_point validationBegin = Clock::now();
    validateAccumulateInput(&workspace, nullptr, path, frequencyState, epsilon,
                            receivers_, widthMode_, diagnosticRequest);
    statistics->validatedRayPoints += path.points.size();
    statistics->validatedWorkspaceValues += workspace.pressure().size();
    statistics->validationSeconds +=
        elapsedSeconds(validationBegin, Clock::now());
    const auto diagnostic = accumulateWithImageCount<true>(
        &workspace, nullptr, path, frequencyState, epsilon, diagnosticRequest,
        statistics);
    const Clock::time_point outputValidationBegin = Clock::now();
    validateWorkspacePressure(workspace,
                              "Cartesian Cerveny computed workspace pressure");
    statistics->validatedWorkspaceValues += workspace.pressure().size();
    statistics->validationSeconds +=
        elapsedSeconds(outputValidationBegin, Clock::now());
    return diagnostic;
  }

  validateAccumulateInput(&workspace, nullptr, path, frequencyState, epsilon,
                          receivers_, widthMode_, diagnosticRequest);
  const auto diagnostic =
      accumulateWithImageCount<false>(&workspace, nullptr, path, frequencyState,
                                      epsilon, diagnosticRequest, nullptr);
  validateWorkspacePressure(workspace,
                            "Cartesian Cerveny computed workspace pressure");
  return diagnostic;
}

std::optional<CartesianCervenyDiagnostic>
CartesianCervenyInfluence::accumulateIntensity(
    IntensityWorkspace& workspace, const RayPath& path,
    const RayFrequencyState& frequencyState, std::complex<double> epsilon,
    std::optional<CartesianCervenyDiagnosticRequest> diagnosticRequest,
    CartesianCervenyStatistics* statistics) const {
  if (statistics != nullptr) {
    const Clock::time_point validationBegin = Clock::now();
    validateAccumulateInput(nullptr, &workspace, path, frequencyState, epsilon,
                            receivers_, widthMode_, diagnosticRequest);
    statistics->validatedRayPoints += path.points.size();
    statistics->validatedWorkspaceValues += workspace.intensity().size();
    statistics->validationSeconds +=
        elapsedSeconds(validationBegin, Clock::now());
    const auto diagnostic = accumulateWithImageCount<true>(
        nullptr, &workspace, path, frequencyState, epsilon, diagnosticRequest,
        statistics);
    const Clock::time_point outputValidationBegin = Clock::now();
    validateWorkspaceIntensity(
        workspace, "Cartesian Cerveny computed workspace intensity");
    statistics->validatedWorkspaceValues += workspace.intensity().size();
    statistics->validationSeconds +=
        elapsedSeconds(outputValidationBegin, Clock::now());
    return diagnostic;
  }

  validateAccumulateInput(nullptr, &workspace, path, frequencyState, epsilon,
                          receivers_, widthMode_, diagnosticRequest);
  const auto diagnostic =
      accumulateWithImageCount<false>(nullptr, &workspace, path, frequencyState,
                                      epsilon, diagnosticRequest, nullptr);
  validateWorkspaceIntensity(workspace,
                             "Cartesian Cerveny computed workspace intensity");
  return diagnostic;
}

std::optional<CartesianCervenyDiagnostic>
CartesianCervenyInfluence::accumulatePrevalidated(
    FrequencyWorkspace& workspace, const RayPath& path,
    const RayFrequencyState& frequencyState, std::complex<double> epsilon,
    CartesianCervenyStatistics* statistics) const {
  if (statistics != nullptr) {
    const Clock::time_point validationBegin = Clock::now();
    validatePrevalidatedInput(&workspace, nullptr, path, frequencyState,
                              epsilon, receivers_, widthMode_);
    statistics->validationSeconds +=
        elapsedSeconds(validationBegin, Clock::now());
    return accumulateWithImageCount<true>(&workspace, nullptr, path,
                                          frequencyState, epsilon, std::nullopt,
                                          statistics);
  }

  validatePrevalidatedInput(&workspace, nullptr, path, frequencyState, epsilon,
                            receivers_, widthMode_);
  return accumulateWithImageCount<false>(&workspace, nullptr, path,
                                         frequencyState, epsilon, std::nullopt,
                                         nullptr);
}

std::optional<CartesianCervenyDiagnostic>
CartesianCervenyInfluence::accumulateIntensityPrevalidated(
    IntensityWorkspace& workspace, const RayPath& path,
    const RayFrequencyState& frequencyState, std::complex<double> epsilon,
    CartesianCervenyStatistics* statistics) const {
  if (statistics != nullptr) {
    const Clock::time_point validationBegin = Clock::now();
    validatePrevalidatedInput(nullptr, &workspace, path, frequencyState,
                              epsilon, receivers_, widthMode_);
    statistics->validationSeconds +=
        elapsedSeconds(validationBegin, Clock::now());
    return accumulateWithImageCount<true>(nullptr, &workspace, path,
                                          frequencyState, epsilon, std::nullopt,
                                          statistics);
  }

  validatePrevalidatedInput(nullptr, &workspace, path, frequencyState, epsilon,
                            receivers_, widthMode_);
  return accumulateWithImageCount<false>(nullptr, &workspace, path,
                                         frequencyState, epsilon, std::nullopt,
                                         nullptr);
}

bool CartesianCervenyInfluence::accumulateFusedPrevalidated(
    std::span<FrequencyWorkspace> workspaces, const RayPath& path,
    std::span<const RayFrequencyState> frequencyStates,
    std::span<const std::complex<double>> epsilons,
    CartesianCervenyStatistics* statistics) const {
  if (statistics != nullptr) {
    if (settings_.imageCount == 1U) {
      return accumulateFusedImpl<true, 1U>(workspaces, path, frequencyStates,
                                           epsilons, statistics);
    }
    if (settings_.imageCount == 2U) {
      return accumulateFusedImpl<true, 2U>(workspaces, path, frequencyStates,
                                           epsilons, statistics);
    }
    return accumulateFusedImpl<true, 3U>(workspaces, path, frequencyStates,
                                         epsilons, statistics);
  }
  if (settings_.imageCount == 1U) {
    return accumulateFusedImpl<false, 1U>(workspaces, path, frequencyStates,
                                          epsilons, nullptr);
  }
  if (settings_.imageCount == 2U) {
    return accumulateFusedImpl<false, 2U>(workspaces, path, frequencyStates,
                                          epsilons, nullptr);
  }
  return accumulateFusedImpl<false, 3U>(workspaces, path, frequencyStates,
                                        epsilons, nullptr);
}

// IGR-1 fused kernel (design §4, worklist §5 hierarchy).  Per fixed frequency
// the arithmetic reproduces accumulateImpl exactly: shared segment/range/
// image geometry is computed once per ray (frequency-independent by
// construction, Obligation P4), while every frequency-local quantity
// (epsilon, p/q/gamma/KMAH, tau, principal/corrected, omega, radiusMax,
// window, taper, phase, exponential, reflection amplitude/phase) is
// evaluated per frequency with the per-frequency kernel's exact expressions
// (Obligations P1-P3, P6).  Returns false on the shared early range exit.
template <bool CollectStatistics, std::size_t ImageCount>
bool CartesianCervenyInfluence::accumulateFusedImpl(
    std::span<FrequencyWorkspace> workspaces, const RayPath& path,
    std::span<const RayFrequencyState> frequencyStates,
    std::span<const std::complex<double>> epsilons,
    CartesianCervenyStatistics* statistics) const {
  static_assert(ImageCount >= 1U && ImageCount <= 3U);
  const std::size_t frequencyCount = frequencyStates.size();
  if (frequencyCount == 0U) {
    throw ValidationError(
        "fused Cartesian Cerveny influence requires at least one frequency");
  }
  if (workspaces.size() != frequencyCount ||
      epsilons.size() != frequencyCount) {
    throw ValidationError(
        "fused Cartesian Cerveny influence requires workspace, "
        "frequency-state, and epsilon spans of equal size");
  }
  if constexpr (CollectStatistics) {
    // One fused ray call covers Nf per-(ray, frequency) accumulations.
    statistics->rayAccumulations += frequencyCount;
  }
  {
    Clock::time_point validationBegin{};
    if constexpr (CollectStatistics) {
      validationBegin = Clock::now();
    }
    for (std::size_t frequencyIndex = 0U; frequencyIndex < frequencyCount;
         ++frequencyIndex) {
      validateFusedPrevalidatedInput(workspaces[frequencyIndex], path,
                                     frequencyStates[frequencyIndex],
                                     epsilons[frequencyIndex], receivers_,
                                     widthMode_);
    }
    if constexpr (CollectStatistics) {
      statistics->validationSeconds +=
          elapsedSeconds(validationBegin, Clock::now());
    }
  }

  Clock::time_point precomputeBegin{};
  if constexpr (CollectStatistics) {
    precomputeBegin = Clock::now();
  }
  std::vector<std::size_t> activePrefixPointCount(frequencyCount);
  std::vector<PrecomputedRayValues> ray(frequencyCount);
  std::vector<double> angularFrequency(frequencyCount);
  std::vector<double> radiusMax(frequencyCount);
  for (std::size_t frequencyIndex = 0U; frequencyIndex < frequencyCount;
       ++frequencyIndex) {
    // Exact per-frequency active-prefix scan of accumulateImpl (the first
    // inactive point is retained).
    std::size_t prefixPointCount = path.points.size();
    for (std::size_t index = 0U;
         index < frequencyStates[frequencyIndex].points.size(); ++index) {
      if (!frequencyStates[frequencyIndex].points[index].active) {
        prefixPointCount = index + 1U;
        break;
      }
    }
    activePrefixPointCount[frequencyIndex] = prefixPointCount;
    // Own-prefix precompute only (Obligation P3); never extended to the
    // union prefix.
    ray[frequencyIndex] = precomputeRayValues(
        path, soundSpeedProfile_, epsilons[frequencyIndex], prefixPointCount,
        widthMode_);
    if constexpr (CollectStatistics) {
      statistics->activeRayPoints += prefixPointCount;
    }
    angularFrequency[frequencyIndex] =
        2.0 * std::numbers::pi * frequencyStates[frequencyIndex].frequency;
    radiusMax[frequencyIndex] =
        30.0 * path.points.front().soundSpeed /
        frequencyStates[frequencyIndex].frequency;
  }
  if constexpr (CollectStatistics) {
    statistics->precomputeSeconds +=
        elapsedSeconds(precomputeBegin, Clock::now());
  }
  const double beamWindowSquared = static_cast<double>(settings_.beamWindow) *
                                   static_cast<double>(settings_.beamWindow);
  const double ratio = sourceGeometry_ == SourceGeometry::Line
                           ? 1.0
                           : std::sqrt(std::abs(std::cos(path.launchAngle)));
  const std::vector<double>& receiverRanges = receivers_.ranges();
  const std::vector<double>& receiverDepths = receivers_.depths();
  const std::size_t receiversPerRange = receivers_.receiversPerRange();
  // Irregular CC runs evaluate every range at the first depth (the Origin/F2CPP
  // legacy behavior preserved by the per-frequency kernel); retained verbatim.
  // Fused scope rejects irregular grids upstream anyway.
  const bool irregularReceivers = receivers_.isIrregular();
  const double irregularReceiverDepth = receiverDepths.front();
  const double seaSurfaceDepth = environment_.seaSurface().depth();
  const double seabedDepth = environment_.seabed().depth();
  const std::size_t receiverRangeCount = receiverRanges.size();
  // D5: traversal upper bound = union of the per-frequency active prefixes.
  std::size_t unionPrefix = 0U;
  for (const std::size_t prefixPointCount : activePrefixPointCount) {
    unionPrefix = std::max(unionPrefix, prefixPointCount);
  }
  // O(Nf) kernel-local scratch, rewritten per segment/range/depth (design §7).
  std::vector<bool> activeMask(frequencyCount, false);
  std::vector<bool> rangeEligible(frequencyCount, false);
  std::vector<std::complex<double>> q(frequencyCount);
  std::vector<std::complex<double>> tau(frequencyCount);
  std::vector<std::complex<double>> gamma(frequencyCount);
  std::vector<std::complex<double>> principal(frequencyCount);
  std::vector<std::complex<double>> corrected(frequencyCount);
  std::vector<std::complex<double>> imageSum(frequencyCount);
  Clock::time_point hotLoopBegin{};
  if constexpr (CollectStatistics) {
    hotLoopBegin = Clock::now();
  }

  for (std::size_t rightIndex = 2U; rightIndex < unionPrefix; ++rightIndex) {
    if constexpr (CollectStatistics) {
      ++statistics->segmentCandidates;
      ++statistics->geometrySegmentEvaluations;
    }
    const std::size_t leftIndex = rightIndex - 1U;
    const double leftRange = path.points[leftIndex].position.range;
    const double rightRange = path.points[rightIndex].position.range;
    if (rightRange > receiverRanges.back()) {
      if constexpr (CollectStatistics) {
        statistics->hotLoopSeconds +=
            elapsedSeconds(hotLoopBegin, Clock::now());
      }
      return false;
    }
    if (std::abs(rightRange - leftRange) <
        1000.0 * floatingSpacing(rightRange)) {
      continue;
    }
    bool anyActive = false;
    for (std::size_t frequencyIndex = 0U; frequencyIndex < frequencyCount;
         ++frequencyIndex) {
      // Loop-bound + left-endpoint gate, exactly the per-frequency kernel's
      // segment conditions for that frequency (a false left endpoint
      // suppresses the geometry suffix; segments beyond a frequency's prefix
      // are gated out as the per-frequency loop bound gated them).
      activeMask[frequencyIndex] =
          rightIndex < activePrefixPointCount[frequencyIndex] &&
          frequencyStates[frequencyIndex].points[leftIndex].active;
      anyActive = anyActive || activeMask[frequencyIndex];
    }
    if (!anyActive) {
      continue;
    }

    const std::size_t firstUpper =
        fortranUpperRangeIndex(leftRange, receiverRanges, receiverRangeDelta_);
    const std::size_t secondUpper =
        fortranUpperRangeIndex(rightRange, receiverRanges, receiverRangeDelta_);
    if (firstUpper >= secondUpper) {
      continue;
    }
    if constexpr (CollectStatistics) {
      ++statistics->eligibleSegments;
    }

    for (std::size_t oneBasedRange = firstUpper + 1U;
         oneBasedRange <= secondUpper; ++oneBasedRange) {
      if constexpr (CollectStatistics) {
        ++statistics->receiverRangeEvaluations;
        ++statistics->geometryRangeEvaluations;
      }
      const std::size_t rangeIndex = oneBasedRange - 1U;
      const double weight =
          (receiverRanges[rangeIndex] - leftRange) / (rightRange - leftRange);
      const Vec2 position = path.points[leftIndex].position +
                            weight * (path.points[rightIndex].position -
                                      path.points[leftIndex].position);
      const Vec2 slowness = path.points[leftIndex].slowness +
                            weight * (path.points[rightIndex].slowness -
                                      path.points[leftIndex].slowness);
      const double soundSpeed = path.points[leftIndex].soundSpeed +
                                weight * (path.points[rightIndex].soundSpeed -
                                          path.points[leftIndex].soundSpeed);
      bool anyEligible = false;
      for (std::size_t frequencyIndex = 0U; frequencyIndex < frequencyCount;
           ++frequencyIndex) {
        rangeEligible[frequencyIndex] = activeMask[frequencyIndex];
        if (!rangeEligible[frequencyIndex]) {
          continue;
        }
        if constexpr (CollectStatistics) {
          // Counted for every active frequency at this range, including
          // frequencies the gamma guard below then rejects.
          ++statistics->frequencyRangeKernelEvaluations;
        }
        q[frequencyIndex] =
            ray[frequencyIndex].q[leftIndex] +
            weight * (ray[frequencyIndex].q[rightIndex] -
                      ray[frequencyIndex].q[leftIndex]);
        tau[frequencyIndex] =
            frequencyStates[frequencyIndex].points[leftIndex]
                .complexTravelTime +
            weight * (frequencyStates[frequencyIndex].points[rightIndex]
                          .complexTravelTime -
                      frequencyStates[frequencyIndex].points[leftIndex]
                          .complexTravelTime);
        gamma[frequencyIndex] =
            ray[frequencyIndex].gamma[leftIndex] +
            weight * (ray[frequencyIndex].gamma[rightIndex] -
                      ray[frequencyIndex].gamma[leftIndex]);
        if (gamma[frequencyIndex].imag() > 0.0) {
          rangeEligible[frequencyIndex] = false;
          continue;
        }

        principal[frequencyIndex] =
            ratio * std::sqrt(soundSpeed * std::abs(epsilons[frequencyIndex]) /
                              q[frequencyIndex]);
        int finalKmah = ray[frequencyIndex].kmah[leftIndex];
        finalKmah = updateCervenyKmah(ray[frequencyIndex].q[leftIndex],
                                      q[frequencyIndex], finalKmah, widthMode_);
        corrected[frequencyIndex] = finalKmah < 0
                                        ? -principal[frequencyIndex]
                                        : principal[frequencyIndex];
        requireFiniteComplex(principal[frequencyIndex],
                             "Cartesian Cerveny principal constant");
        requireFiniteComplex(corrected[frequencyIndex],
                             "Cartesian Cerveny corrected constant");
        anyEligible = true;
      }
      if (!anyEligible) {
        continue;
      }

      for (std::size_t depthIndex = 0U; depthIndex < receiversPerRange;
           ++depthIndex) {
        if constexpr (CollectStatistics) {
          ++statistics->receiverDepthEvaluations;
          ++statistics->geometryDepthEvaluations;
        }
        const double receiverDepth = irregularReceivers
                                         ? irregularReceiverDepth
                                         : receiverDepths[depthIndex];
        for (std::size_t frequencyIndex = 0U; frequencyIndex < frequencyCount;
             ++frequencyIndex) {
          imageSum[frequencyIndex] = std::complex<double>{};
        }
        // Runtime image loop (kind order True -> Surface -> Bottom).
        for (std::size_t imageIndex = 0U; imageIndex < ImageCount;
             ++imageIndex) {
          double deltaDepth = 0.0;
          double polarity = 1.0;
          if (imageIndex == 0U) {
            deltaDepth = receiverDepth - position.depth;
          } else if (imageIndex == 1U) {
            deltaDepth =
                -receiverDepth + 2.0 * seaSurfaceDepth - position.depth;
            polarity = -1.0;
          } else {
            deltaDepth = -receiverDepth + 2.0 * seabedDepth - position.depth;
          }
          if constexpr (CollectStatistics) {
            ++statistics->geometryImageGeometryEvaluations;
          }
          const double deltaSquared = deltaDepth * deltaDepth;
          for (std::size_t frequencyIndex = 0U;
               frequencyIndex < frequencyCount; ++frequencyIndex) {
            if (!rangeEligible[frequencyIndex]) {
              continue;
            }
            if constexpr (CollectStatistics) {
              ++statistics->imageEvaluations;
              ++statistics->frequencyImageKernelEvaluations;
            }
            const double windowMetric =
                -angularFrequency[frequencyIndex] *
                gamma[frequencyIndex].imag() * deltaSquared;
#ifndef NDEBUG
            requireFinite(windowMetric, "Cartesian Cerveny window metric");
#endif
            if (windowMetric >= beamWindowSquared) {
              if constexpr (CollectStatistics) {
                ++statistics->windowRejections;
              }
              // Obligation P1: the zero add is unconditional (the
              // per-frequency kernel adds the returned zero contribution).
              imageSum[frequencyIndex] += std::complex<double>{};
              continue;
            }
            const double taper = cervenyHermiteTaperUnchecked(
                deltaDepth, radiusMax[frequencyIndex],
                2.0 * radiusMax[frequencyIndex]);
            if (taper == 0.0) {
              if constexpr (CollectStatistics) {
                ++statistics->taperRejections;
              }
              imageSum[frequencyIndex] += std::complex<double>{};
              continue;
            }
            const std::complex<double> phaseArgument =
                angularFrequency[frequencyIndex] *
                    (tau[frequencyIndex] +
                     slowness.depth * deltaDepth +
                     gamma[frequencyIndex] * deltaSquared) -
                frequencyStates[frequencyIndex]
                    .points[rightIndex]
                    .reflectionPhase;
            const std::complex<double> contribution =
                polarity *
                frequencyStates[frequencyIndex].points[rightIndex].amplitude *
                taper * negativeImaginaryExponential(phaseArgument);
#ifndef NDEBUG
            requireFiniteComplex(contribution,
                                 "Cartesian Cerveny image contribution");
#endif
            if constexpr (CollectStatistics) {
              if (contribution != std::complex<double>{}) {
                ++statistics->nonzeroImageContributions;
              }
            }
            imageSum[frequencyIndex] += contribution;
          }
        }
        for (std::size_t frequencyIndex = 0U; frequencyIndex < frequencyCount;
             ++frequencyIndex) {
          if (!rangeEligible[frequencyIndex]) {
            continue;
          }
          // Obligation P6: exactly one workspace add per (ray, range, depth,
          // eligible frequency), read-add-assign like the current kernel.
          const std::complex<double> frequencyContribution =
              corrected[frequencyIndex] * imageSum[frequencyIndex];
#ifndef NDEBUG
          requireFiniteComplex(frequencyContribution,
                               "Cartesian Cerveny final contribution");
#endif
          const std::span<std::complex<double>> pressure =
              workspaces[frequencyIndex].pressure();
          std::complex<double>& pressureValue =
              pressure[depthIndex * receiverRangeCount + rangeIndex];
          const std::complex<double> updatedPressure =
              pressureValue + frequencyContribution;
#ifndef NDEBUG
          requireFiniteComplex(
              updatedPressure,
              "Cartesian Cerveny accumulated workspace pressure");
#endif
          pressureValue = updatedPressure;
        }
      }
    }
  }
  if constexpr (CollectStatistics) {
    statistics->hotLoopSeconds += elapsedSeconds(hotLoopBegin, Clock::now());
  }
  return true;
}

}  // namespace rayreuse
