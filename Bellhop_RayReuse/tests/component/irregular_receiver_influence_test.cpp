#include "rayreuse/field/arrival_workspace.hpp"
#include "rayreuse/field/cartesian_cerveny_influence.hpp"
#include "rayreuse/field/frequency_workspace.hpp"
#include "rayreuse/field/beam_epsilon.hpp"
#include "rayreuse/field/frequency_projector.hpp"
#include "rayreuse/field/geometric_gaussian_influence.hpp"
#include "rayreuse/field/geometric_hat_influence.hpp"
#include "rayreuse/model/environment.hpp"
#include "rayreuse/model/simulation_case.hpp"
#include "rayreuse/ray/geometry_tracer.hpp"
#include "rayreuse/solver/single_frequency_solver.hpp"

#include <cmath>
#include <complex>
#include <cstddef>
#include <iostream>
#include <numbers>
#include <string>
#include <vector>

#include "rayreuse/error.hpp"
#include "support/test_harness.hpp"

namespace {

using rayreuse::AcousticMaterial;
using rayreuse::ArrivalWorkspace;
using rayreuse::BeamFamily;
using rayreuse::BeamWidthMode;
using rayreuse::BoundaryCurvatureMode;
using rayreuse::BoundaryModel;
using rayreuse::CartesianCervenyInfluence;
using rayreuse::CartesianCervenySettings;
using rayreuse::EigenrayHit;
using rayreuse::Environment;
using rayreuse::FieldComponent;
using rayreuse::FrequencyGrid;
using rayreuse::FrequencyProjector;
using rayreuse::FrequencyWorkspace;
using rayreuse::GeometricGaussianInfluence;
using rayreuse::GeometricHatDiagnosticRequest;
using rayreuse::GeometricHatInfluence;
using rayreuse::GeometryTracer;
using rayreuse::IntensityWorkspace;
using rayreuse::IntegratorSettings;
using rayreuse::LaunchFan;
using rayreuse::pickMinimumWidthEpsilon;
using rayreuse::RayFrequencyPoint;
using rayreuse::RayFrequencyState;
using rayreuse::RayPath;
using rayreuse::RayState;
using rayreuse::ReceiverGrid;
using rayreuse::ReceiverGridLayout;
using rayreuse::SimulationCase;
using rayreuse::SimulationRunMode;
using rayreuse::SingleFrequencyResult;
using rayreuse::SingleFrequencySolver;
using rayreuse::SoundSpeedProfile;
using rayreuse::Source;
using rayreuse::SourceBeamPattern;
using rayreuse::ValidationError;
using rayreuse::Vec2;
using rayreuse::test::Context;

constexpr double kSoundSpeed = 1500.0;
constexpr double kFrequency = 50.0;
constexpr double kDalpha = 0.1;

// Paired irregular receiver line: each range owns exactly one depth, and the
// depths are strictly increasing (a ReceiverGrid requirement).  Only the
// middle receiver pair sits on the horizontal reference ray at 500 m, which
// makes every column's paired depth directly observable in the workspace.
constexpr std::size_t kPairedRangeCount = 3U;

ReceiverGrid makePairedIrregularGrid() {
  return ReceiverGrid({400.0, 500.0, 600.0}, {100.0, 300.0, 500.0},
                      ReceiverGridLayout::Irregular);
}

RayPath makeHorizontalPath() {
  RayPath path;
  path.launchAngle = 0.0;
  const std::vector<double> rayRanges{0.0, 200.0, 400.0, 600.0};
  const std::vector<double> q{0.0, 150.0, 300.0, 450.0};
  for (std::size_t index = 0U; index < rayRanges.size(); ++index) {
    path.points.push_back(
        RayState{.position = {.range = rayRanges[index], .depth = 500.0},
                 .slowness = {.range = 1.0 / kSoundSpeed, .depth = 0.0},
                 .dynamicP = {},
                 .dynamicQ = {q[index], 0.0},
                 .soundSpeed = kSoundSpeed,
                 .realTravelTime = 0.0});
  }
  return path;
}

RayFrequencyState makeFrequencyState() {
  RayFrequencyState state;
  state.frequency = kFrequency;
  for (const double range : {0.0, 200.0, 400.0, 600.0}) {
    state.points.push_back(
        RayFrequencyPoint{.complexTravelTime = {range / kSoundSpeed, 0.0},
                          .amplitude = 1.0,
                          .reflectionPhase = 0.0,
                          .active = true});
  }
  return state;
}

void testIrregularWorkspaceDimensions(Context& context) {
  const ReceiverGrid receivers = makePairedIrregularGrid();
  context.check(receivers.receiversPerRange() == 1U,
                "irregular grid pairs one depth per range");
  for (std::size_t rangeIndex = 0U; rangeIndex < kPairedRangeCount;
       ++rangeIndex) {
    context.checkNear(receivers.depthAt(0U, rangeIndex),
                      400.0 + 100.0 * static_cast<double>(rangeIndex), 0.0,
                      "irregular depthAt pairs depth with range");
  }

  const FrequencyWorkspace pressure(kFrequency, receivers);
  const IntensityWorkspace intensity(kFrequency, receivers);
  const ArrivalWorkspace arrivals(kFrequency, receivers);
  context.check(pressure.depthCount() == 1U &&
                    pressure.rangeCount() == kPairedRangeCount,
                "irregular pressure workspace is receiversPerRange x ranges");
  context.check(intensity.depthCount() == 1U &&
                    intensity.rangeCount() == kPairedRangeCount,
                "irregular intensity workspace is receiversPerRange x ranges");
  context.check(arrivals.depthCount() == 1U &&
                    arrivals.rangeCount() == kPairedRangeCount,
                "irregular arrival workspace is receiversPerRange x ranges");

  // Rectilinear grids keep the historical depth-count workspace shape.
  const ReceiverGrid rectilinear({400.0, 500.0, 600.0}, {100.0, 300.0, 500.0});
  const FrequencyWorkspace rectilinearPressure(kFrequency, rectilinear);
  context.check(rectilinearPressure.depthCount() == 3U &&
                    rectilinearPressure.rangeCount() == 3U,
                "rectilinear workspace keeps depths x ranges");
}

void testGeometricHatPairedAddressing(Context& context) {
  const ReceiverGrid irregular = makePairedIrregularGrid();
  const RayPath path = makeHorizontalPath();
  const RayFrequencyState state = makeFrequencyState();

  FrequencyWorkspace irregularWorkspace(kFrequency, irregular);
  static_cast<void>(GeometricHatInfluence(irregular).accumulate(
      irregularWorkspace, path, state, kDalpha));
  context.check(irregularWorkspace.at(0U, 0U) == std::complex<double>{} &&
                    irregularWorkspace.at(0U, 2U) ==
                        std::complex<double>{},
                "Cartesian G skips off-axis paired depths");
  context.check(irregularWorkspace.at(0U, 1U) != std::complex<double>{},
                "Cartesian G accumulates into the on-axis paired receiver");

  // The paired result must reproduce the rectilinear diagonal exactly: the
  // per-cell arithmetic is identical, so equality is bit-exact.
  const ReceiverGrid rectilinear({400.0, 500.0, 600.0},
                                 {100.0, 300.0, 500.0});
  FrequencyWorkspace rectilinearWorkspace(kFrequency, rectilinear);
  static_cast<void>(GeometricHatInfluence(rectilinear).accumulate(
      rectilinearWorkspace, path, state, kDalpha));
  for (std::size_t rangeIndex = 0U; rangeIndex < kPairedRangeCount;
       ++rangeIndex) {
    context.check(irregularWorkspace.at(0U, rangeIndex) ==
                      rectilinearWorkspace.at(rangeIndex, rangeIndex),
                  "Cartesian G paired value equals the rectilinear diagonal");
  }

  // Diagnostic requests are bounded by receiversPerRange on irregular grids.
  bool rejected = false;
  try {
    FrequencyWorkspace workspace(kFrequency, irregular);
    static_cast<void>(GeometricHatInfluence(irregular).accumulate(
        workspace, path, state, kDalpha,
        GeometricHatDiagnosticRequest{.receiverRangeIndex = 0U,
                                      .receiverDepthIndex = 1U}));
  } catch (const ValidationError&) {
    rejected = true;
  }
  context.check(rejected,
                "Cartesian G diagnostic depth index is bounded by "
                "receiversPerRange");
}

void testGeometricGaussianPairedAddressing(Context& context) {
  const ReceiverGrid irregular = makePairedIrregularGrid();
  const RayPath path = makeHorizontalPath();
  const RayFrequencyState state = makeFrequencyState();

  FrequencyWorkspace irregularWorkspace(kFrequency, irregular);
  static_cast<void>(GeometricGaussianInfluence(irregular).accumulate(
      irregularWorkspace, path, state, kDalpha));
  // sigma1 is capped by the near-field branch (about 2.7 m at the middle
  // receiver), so the 100 m off-axis paired depths fall outside the
  // 4-sigma beam window while the on-axis pair stays inside.
  context.check(irregularWorkspace.at(0U, 0U) == std::complex<double>{} &&
                    irregularWorkspace.at(0U, 2U) ==
                        std::complex<double>{},
                "Cartesian B skips off-axis paired depths");
  context.check(irregularWorkspace.at(0U, 1U) != std::complex<double>{},
                "Cartesian B accumulates into the on-axis paired receiver");

  const ReceiverGrid rectilinear({400.0, 500.0, 600.0},
                                 {100.0, 300.0, 500.0});
  FrequencyWorkspace rectilinearWorkspace(kFrequency, rectilinear);
  static_cast<void>(GeometricGaussianInfluence(rectilinear).accumulate(
      rectilinearWorkspace, path, state, kDalpha));
  for (std::size_t rangeIndex = 0U; rangeIndex < kPairedRangeCount;
       ++rangeIndex) {
    context.check(irregularWorkspace.at(0U, rangeIndex) ==
                      rectilinearWorkspace.at(rangeIndex, rangeIndex),
                  "Cartesian B paired value equals the rectilinear diagonal");
  }
}

void testGeometricHatArrivalAndEigenrayPairedTraversal(Context& context) {
  const ReceiverGrid irregular = makePairedIrregularGrid();
  const RayPath path = makeHorizontalPath();
  const RayFrequencyState state = makeFrequencyState();
  const GeometricHatInfluence influence(irregular);

  ArrivalWorkspace arrivals(kFrequency, irregular);
  influence.accumulateArrivals(arrivals, path, state, kDalpha);
  context.check(arrivals.arrivalCountAt(0U, 0U) == 0U &&
                    arrivals.arrivalCountAt(0U, 1U) == 1U &&
                    arrivals.arrivalCountAt(0U, 2U) == 0U,
                "Cartesian G arrivals land only on the paired on-axis "
                "receiver cell");

  std::vector<EigenrayHit> hits;
  influence.collectEigenrayHits(
      [&](const EigenrayHit& hit) { hits.push_back(hit); }, path, state,
      kDalpha);
  context.check(hits.size() == 1U && hits.front().receiverRangeIndex == 1U &&
                    hits.front().receiverDepthIndex == 0U &&
                    hits.front().prefixPointCount == 3U,
                "Cartesian G eigenray hits address the paired receiver "
                "column only");
}

Environment makeIsovelocityEnvironment() {
  return Environment(
      SoundSpeedProfile(
          {{.depth = 0.0, .soundSpeed = 1500.0, .density = 1000.0},
           {.depth = 1000.0, .soundSpeed = 1500.0, .density = 1000.0}}),
      BoundaryModel::vacuum(0.0),
      BoundaryModel::acousticHalfSpace(
          1000.0, AcousticMaterial{.compressionalSoundSpeed = 1600.0,
                                   .shearSoundSpeed = 0.0,
                                   .density = 1800.0}));
}

std::vector<double> linearGrid(double first, double last,
                               std::size_t count) {
  std::vector<double> values;
  values.reserve(count);
  const double denominator = static_cast<double>(count - 1U);
  for (std::size_t index = 0U; index < count; ++index) {
    values.push_back(first +
                     (last - first) * static_cast<double>(index) /
                         denominator);
  }
  return values;
}

// InfluenceCervenyCart keeps Origin's irregular-grid legacy: the depth loop
// is one row and reads Pos%Rz(1) for every range instead of Pos%Rz(ir).  A
// paired irregular CC run must therefore match the single-depth rectilinear
// run at the first depth bit-for-bit, and must differ from any other depth.
void testCartesianCervenyLegacyFirstDepthAddressing(Context& context) {
  const std::vector<double> ranges = linearGrid(100.0, 5000.0, 51U);
  const std::vector<double> pairedDepths = linearGrid(400.0, 600.0, 51U);
  const ReceiverGrid irregular(pairedDepths, ranges,
                               ReceiverGridLayout::Irregular);
  const ReceiverGrid firstDepthOnly(
      std::vector<double>{pairedDepths.front()}, ranges);

  const Environment environment = makeIsovelocityEnvironment();
  const RayPath path = GeometryTracer(
      environment, IntegratorSettings{.stepLength = 10.0,
                                      .rangeLimit = 5100.0,
                                      .depthLimit = 1100.0,
                                      .maximumRayPoints = 10000U})
      .trace(Source{.depth = 500.0}, 0.0);
  const RayFrequencyState frequencyState =
      FrequencyProjector(environment).project(path, kFrequency, 1.0);
  const auto epsilon =
      pickMinimumWidthEpsilon(kFrequency, 1500.0, 2500.0, 1.0);
  const CartesianCervenySettings settings{.imageCount = 3U,
                                          .beamWindow = 5};

  FrequencyWorkspace irregularWorkspace(kFrequency, irregular);
  static_cast<void>(CartesianCervenyInfluence(
      environment, irregular, settings, BeamWidthMode::MinimumWidth)
      .accumulate(irregularWorkspace, path, frequencyState, epsilon.value));
  FrequencyWorkspace firstDepthWorkspace(kFrequency, firstDepthOnly);
  static_cast<void>(CartesianCervenyInfluence(
      environment, firstDepthOnly, settings, BeamWidthMode::MinimumWidth)
      .accumulate(firstDepthWorkspace, path, frequencyState,
                  epsilon.value));

  context.check(irregularWorkspace.depthCount() == 1U,
                "Cartesian C irregular workspace keeps one depth row");
  bool identicalToFirstDepth = true;
  for (std::size_t rangeIndex = 0U; rangeIndex < ranges.size();
       ++rangeIndex) {
    identicalToFirstDepth =
        identicalToFirstDepth &&
        irregularWorkspace.at(0U, rangeIndex) ==
            firstDepthWorkspace.at(0U, rangeIndex);
  }
  context.check(identicalToFirstDepth,
                "Cartesian C irregular run reproduces the Rz(1) single-depth "
                "rectilinear run bit-for-bit");

  const ReceiverGrid middleDepthOnly(
      std::vector<double>{pairedDepths[25U]}, ranges);
  FrequencyWorkspace middleDepthWorkspace(kFrequency, middleDepthOnly);
  static_cast<void>(CartesianCervenyInfluence(
      environment, middleDepthOnly, settings, BeamWidthMode::MinimumWidth)
      .accumulate(middleDepthWorkspace, path, frequencyState,
                  epsilon.value));
  bool differsFromMiddleDepth = false;
  for (std::size_t rangeIndex = 0U; rangeIndex < ranges.size();
       ++rangeIndex) {
    differsFromMiddleDepth =
        differsFromMiddleDepth ||
        irregularWorkspace.at(0U, rangeIndex) !=
            middleDepthWorkspace.at(0U, rangeIndex);
  }
  context.check(differsFromMiddleDepth,
                "Cartesian C irregular run does not silently follow the ray "
                "or a paired depth");
}

}  // namespace

// Solver-level TL parity: a uniform paired irregular grid (depths equal to
// the rectilinear depth vector) must reproduce the matching rectilinear run
// exactly — paired columns for geometric beams, the full single-depth
// product for Cerveny's legacy Rz(1) addressing.  This exercises trace,
// projection, influence, and pressure scaling end-to-end (writers are F06).
namespace {

SimulationCase makeTlCase(const ReceiverGrid& receivers,
                          BeamFamily beamFamily) {
  return SimulationCase(
      Environment(
          SoundSpeedProfile(
              {{.depth = 0.0, .soundSpeed = 1500.0, .density = 1000.0},
               {.depth = 100.0, .soundSpeed = 1510.0, .density = 1000.0}}),
          BoundaryModel::vacuum(0.0), BoundaryModel::rigid(100.0)),
      Source{.depth = 50.0, .amplitude = 1.0}, receivers,
      FrequencyGrid({kFrequency}),
      LaunchFan{.minimumAngle = -2.0 * std::numbers::pi / 180.0,
                .maximumAngle = 2.0 * std::numbers::pi / 180.0,
                .explicitLaunchAngleCount = 101U},
      IntegratorSettings{.stepLength = 10.0,
                         .rangeLimit = 110.0,
                         .depthLimit = 110.0,
                         .maximumRayPoints = 1000U},
      SourceBeamPattern::omnidirectional(), SimulationRunMode::Coherent,
      beamFamily, FieldComponent::Pressure, BoundaryCurvatureMode::Standard);
}

void checkSolverParity(Context& context, BeamFamily beamFamily,
                       const std::string& label) {
  const ReceiverGrid irregular({25.0, 50.0, 75.0}, {10.0, 55.0, 100.0},
                               ReceiverGridLayout::Irregular);
  const ReceiverGrid rectilinear({25.0, 50.0, 75.0}, {10.0, 55.0, 100.0});

  const SingleFrequencyResult irregularResult = SingleFrequencySolver::
      solveAtFrequency(makeTlCase(irregular, beamFamily), kFrequency, 1.0,
                       2500.0);
  const SingleFrequencyResult rectilinearResult = SingleFrequencySolver::
      solveAtFrequency(makeTlCase(rectilinear, beamFamily), kFrequency, 1.0,
                       2500.0);
  const FrequencyWorkspace& irregularWorkspace = irregularResult.workspace;
  const FrequencyWorkspace& rectilinearWorkspace = rectilinearResult.workspace;
  context.check(irregularWorkspace.depthCount() == 1U &&
                    rectilinearWorkspace.depthCount() == 3U,
                label + " solver workspaces follow receiversPerRange");

  if (beamFamily == BeamFamily::CervenyGaussian) {
    // Legacy Rz(1) addressing: the whole irregular product equals the
    // single-depth rectilinear run at the first paired depth, and differs
    // from the rectilinear diagonal outside the first column.
    const ReceiverGrid firstDepthOnly(
        std::vector<double>{25.0}, {10.0, 55.0, 100.0});
    const SingleFrequencyResult firstDepthResult = SingleFrequencySolver::
        solveAtFrequency(makeTlCase(firstDepthOnly, beamFamily), kFrequency,
                         1.0, 2500.0);
    bool firstDepthMatches = true;
    for (std::size_t rangeIndex = 0U; rangeIndex < 3U; ++rangeIndex) {
      firstDepthMatches =
          firstDepthMatches &&
          irregularWorkspace.at(0U, rangeIndex) ==
              firstDepthResult.workspace.at(0U, rangeIndex);
    }
    context.check(firstDepthMatches,
                  label + " paired irregular TL equals the Rz(1) "
                          "single-depth rectilinear run bit-for-bit");
    bool diagonalDiffers = false;
    for (std::size_t rangeIndex = 1U; rangeIndex < 3U; ++rangeIndex) {
      diagonalDiffers =
          diagonalDiffers ||
          irregularWorkspace.at(0U, rangeIndex) !=
              rectilinearWorkspace.at(rangeIndex, rangeIndex);
    }
    context.check(diagonalDiffers,
                  label + " keeps Origin's Rz(1) depth instead of paired "
                          "diagonal depths");
    return;
  }

  bool diagonalMatches = true;
  for (std::size_t rangeIndex = 0U; rangeIndex < 3U; ++rangeIndex) {
    diagonalMatches =
        diagonalMatches &&
        irregularWorkspace.at(0U, rangeIndex) ==
            rectilinearWorkspace.at(rangeIndex, rangeIndex);
  }
  context.check(diagonalMatches,
                label + " paired irregular TL equals the rectilinear "
                        "diagonal bit-for-bit");
}

void testSolverLevelIrregularTlParity(Context& context) {
  checkSolverParity(context, BeamFamily::CervenyGaussian, "CC");
  checkSolverParity(context, BeamFamily::GeometricHat, "CG");
  checkSolverParity(context, BeamFamily::GeometricGaussian, "CB");
}

}  // namespace

int main() {
  Context context;
  testIrregularWorkspaceDimensions(context);
  testGeometricHatPairedAddressing(context);
  testGeometricGaussianPairedAddressing(context);
  testGeometricHatArrivalAndEigenrayPairedTraversal(context);
  testCartesianCervenyLegacyFirstDepthAddressing(context);
  testSolverLevelIrregularTlParity(context);
  if (context.failureCount() != 0) {
    std::cerr << context.failureCount()
              << " irregular receiver influence assertion(s) failed\n";
    return 1;
  }
  std::cout << "All irregular receiver influence tests passed\n";
  return 0;
}
