#include "rayreuse/field/cartesian_cerveny_influence.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <numbers>
#include <string>
#include <vector>

#include "rayreuse/error.hpp"
#include "rayreuse/field/beam_epsilon.hpp"
#include "rayreuse/field/frequency_projector.hpp"
#include "rayreuse/ray/geometry_tracer.hpp"
#include "support/munk_case_fixture.hpp"
#include "support/test_harness.hpp"

namespace {

using rayreuse::AcousticMaterial;
using rayreuse::BeamWidthMode;
using rayreuse::BoundaryModel;
using rayreuse::CartesianCervenyDiagnostic;
using rayreuse::CartesianCervenyDiagnosticRequest;
using rayreuse::CartesianCervenyInfluence;
using rayreuse::CartesianCervenySettings;
using rayreuse::CartesianCervenyStatistics;
using rayreuse::cervenyHermiteTaper;
using rayreuse::CervenyImageKind;
using rayreuse::Environment;
using rayreuse::FrequencyProjector;
using rayreuse::FrequencyWorkspace;
using rayreuse::GeometryTracer;
using rayreuse::IntegratorSettings;
using rayreuse::IntensityWorkspace;
using rayreuse::pickMinimumWidthEpsilon;
using rayreuse::RayFrequencyPoint;
using rayreuse::RayFrequencyState;
using rayreuse::RayPath;
using rayreuse::RayState;
using rayreuse::ReceiverGrid;
using rayreuse::SoundSpeedProfile;
using rayreuse::Source;
using rayreuse::updateCervenyKmah;
using rayreuse::ValidationError;
using rayreuse::Vec2;
using rayreuse::test::Context;

constexpr double kDirectLaunchAngle = -2.91861078928821462e-4;
constexpr double kMunkInfluenceLaunchAngle = -1.77682903819398719e-1;
// libstdc++ and libc++ complex elementary functions differ by a few ulps in
// this reflected diagnostic while preserving the legacy binary32 result.
constexpr double kCrossCompilerImageTolerance = 1.0e-12;
constexpr double kCrossCompilerContributionTolerance = 2.0e-11;

std::vector<double> linearGrid(double first, double last, std::size_t count) {
  std::vector<double> values;
  values.reserve(count);
  const double denominator = static_cast<double>(count - 1U);
  for (std::size_t index = 0U; index < count; ++index) {
    values.push_back(first +
                     (last - first) * static_cast<double>(index) / denominator);
  }
  return values;
}

Environment makeDirectEnvironment() {
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

void checkComplexNear(Context& context, std::complex<double> actual,
                      std::complex<double> expected, double tolerance,
                      const std::string& message) {
  context.checkNear(actual.real(), expected.real(), tolerance,
                    message + " real");
  context.checkNear(actual.imag(), expected.imag(), tolerance,
                    message + " imaginary");
}

void checkFloatBits(Context& context, std::complex<double> value,
                    std::uint32_t expectedReal, std::uint32_t expectedImaginary,
                    const std::string& message) {
  const std::complex<float> quantized{static_cast<float>(value.real()),
                                      static_cast<float>(value.imag())};
  context.check(std::bit_cast<std::uint32_t>(quantized.real()) == expectedReal,
                message + " real float bits");
  context.check(
      std::bit_cast<std::uint32_t>(quantized.imag()) == expectedImaginary,
      message + " imaginary float bits");
}

void checkImage(Context& context,
                const rayreuse::CartesianCervenyImageDiagnostic& image,
                CervenyImageKind kind, double deltaDepth, double windowMetric,
                bool passed, double taper, std::complex<double> contribution,
                const std::string& message) {
  context.check(image.kind == kind, message + " kind");
  context.checkNear(image.deltaDepth, deltaDepth, 1.0e-8,
                    message + " delta depth");
  context.checkNear(image.windowMetric, windowMetric, 1.0e-12,
                    message + " window metric");
  context.check(image.windowPassed == passed,
                message + " strict window decision");
  context.checkNear(image.hermiteTaper, taper, 1.0e-12,
                    message + " Hermite taper");
  checkComplexNear(context, image.contribution, contribution, 1.0e-11,
                   message + " contribution");
}

CartesianCervenyDiagnostic runDirectOracle(Context& context,
                                           FrequencyWorkspace& workspace) {
  const Environment environment = makeDirectEnvironment();
  const ReceiverGrid receivers(linearGrid(400.0, 600.0, 21U),
                               linearGrid(100.0, 5000.0, 51U));
  const RayPath path =
      GeometryTracer(environment,
                     IntegratorSettings{.stepLength = 10.0,
                                        .rangeLimit = 5100.0,
                                        .depthLimit = 1100.0,
                                        .maximumRayPoints = 10000U})
          .trace(Source{.depth = 500.0}, kDirectLaunchAngle);
  const RayFrequencyState frequencyState =
      FrequencyProjector(environment).project(path, 50.0, 1.0);
  const auto epsilon = pickMinimumWidthEpsilon(50.0, 1500.0, 2500.0, 1.0);
  const CartesianCervenyInfluence influence(
      environment, receivers,
      CartesianCervenySettings{.imageCount = 3U, .beamWindow = 5});
  const auto diagnostic = influence.accumulate(
      workspace, path, frequencyState, epsilon.value,
      CartesianCervenyDiagnosticRequest{.receiverRangeIndex = 10U,
                                        .receiverDepthIndex = 10U});
  context.check(diagnostic.has_value() && diagnostic->evaluated,
                "direct oracle receiver is evaluated");
  return diagnostic.value();
}

void testDirectOracle(Context& context) {
  const ReceiverGrid receivers(linearGrid(400.0, 600.0, 21U),
                               linearGrid(100.0, 5000.0, 51U));
  FrequencyWorkspace workspace(50.0, receivers);
  const CartesianCervenyDiagnostic result = runDirectOracle(context, workspace);

  context.check(result.evaluationCount == 1U,
                "direct receiver is owned by one ray segment");
  context.check(result.leftPointIndex == 108U && result.rightPointIndex == 109U,
                "direct Fortran point indices map to C++ zero base");
  context.checkNear(result.interpolationWeight, 4.59987619743631741e-6, 1.0e-14,
                    "direct interpolation weight");
  context.checkNear(result.interpolatedPosition.range, 1080.0, 1.0e-10,
                    "direct interpolated range");
  context.checkNear(result.interpolatedPosition.depth, 4.99684790025809491e2,
                    1.0e-8, "direct interpolated depth");
  checkComplexNear(context, result.qInterpolated,
                   {1.62000006899814308e6, 3.75000000000000047e6}, 1.0e-7,
                   "direct interpolated q");
  checkComplexNear(context, result.gammaInterpolated,
                   {4.85410684147624472e-8, -1.12363579521666120e-7}, 1.0e-15,
                   "direct interpolated gamma");
  context.check(result.kmahLeft == 1 && result.kmahFinal == 1,
                "direct KMAH stays positive");
  checkComplexNear(context, result.constantCorrected,
                   {3.10087839855024470e1, -2.03827780553720643e1}, 2.0e-12,
                   "direct corrected constant");
  checkImage(context, result.images[0U], CervenyImageKind::True,
             3.15209974190509001e-1, 3.50731959363283536e-6, true, 1.0,
             {9.99996492653599689e-1, 8.11876686914479274e-6},
             "direct true image");
  checkImage(context, result.images[1U], CervenyImageKind::Surface,
             -9.99684790025809434e2, 3.52778092412033430e1, false,
             9.65913706549500928e-1, {}, "direct surface image");
  checkImage(context, result.images[2U], CervenyImageKind::Bottom,
             1.00031520997419057e3, 3.53223169646996027e1, false,
             9.65498615397307192e-1, {}, "direct bottom image");
  checkComplexNear(context, result.finalContribution,
                   {3.10088407099787347e1, -2.03824548128207503e1}, 4.0e-11,
                   "direct final ray contribution");
  checkComplexNear(context, workspace.at(10U, 10U), result.finalContribution,
                   0.0,
                   "direct double workspace receives unquantized contribution");
  checkFloatBits(context, result.finalContribution, 0x41f8121bU, 0xc1a30f44U,
                 "direct legacy quantization");
}

struct MunkOracleRun {
  CartesianCervenyDiagnostic focus;
  CartesianCervenyDiagnostic branch;
};

MunkOracleRun runMunkOracles(Context& context) {
  const Environment environment = rayreuse::test::makeMunkEnvironment();
  const ReceiverGrid receivers(linearGrid(0.0, 5000.0, 201U),
                               linearGrid(0.0, 100000.0, 501U));
  const RayPath path =
      GeometryTracer(environment, rayreuse::test::makeMunkIntegratorSettings())
          .trace(Source{.depth = 1000.0}, kMunkInfluenceLaunchAngle);
  const RayFrequencyState frequencyState =
      FrequencyProjector(environment).project(path, 50.0, 1.0);
  const auto epsilon =
      pickMinimumWidthEpsilon(50.0, 1501.38000000000011, 25000.0, 1.0);
  const CartesianCervenyInfluence influence(environment, receivers);

  FrequencyWorkspace focusWorkspace(50.0, receivers);
  const auto focus = influence.accumulate(
      focusWorkspace, path, frequencyState, epsilon.value,
      CartesianCervenyDiagnosticRequest{.receiverRangeIndex = 83U,
                                        .receiverDepthIndex = 65U});
  FrequencyWorkspace branchWorkspace(50.0, receivers);
  const auto branch = influence.accumulate(
      branchWorkspace, path, frequencyState, epsilon.value,
      CartesianCervenyDiagnosticRequest{.receiverRangeIndex = 299U,
                                        .receiverDepthIndex = 23U});
  context.check(focus.has_value() && focus->evaluated,
                "Munk focus receiver is evaluated");
  context.check(branch.has_value() && branch->evaluated,
                "Munk branch receiver is evaluated");
  if (focus.has_value() && focus->evaluated) {
    checkComplexNear(context, focusWorkspace.at(65U, 83U),
                     focus->finalContribution, 0.0,
                     "Munk focus workspace receives diagnostic contribution");
  }
  if (branch.has_value() && branch->evaluated) {
    checkComplexNear(context, branchWorkspace.at(23U, 299U),
                     branch->finalContribution, 0.0,
                     "Munk branch workspace receives diagnostic contribution");
  }
  return MunkOracleRun{.focus = focus.value(), .branch = branch.value()};
}

void testMunkOracles(Context& context) {
  const MunkOracleRun run = runMunkOracles(context);
  const CartesianCervenyDiagnostic& focus = run.focus;
  context.check(focus.leftPointIndex == 53U && focus.rightPointIndex == 54U,
                "Munk focus point indices map to C++ zero base");
  context.checkNear(focus.interpolationWeight, 1.72167933213289992e-1, 1.0e-14,
                    "Munk focus interpolation weight");
  checkComplexNear(context, focus.qInterpolated,
                   {-6.05129208201102010e4, -3.78017538757674024e7}, 1.0e-6,
                   "Munk focus interpolated q");
  checkComplexNear(context, focus.gammaInterpolated,
                   {7.30334138896506214e-9, -1.27190553964798013e-8}, 2.0e-15,
                   "Munk focus interpolated gamma");
  context.check(focus.kmahLeft == 1 && focus.kmahFinal == 1,
                "Munk real-q caustic does not falsely flip KMAH");
  checkComplexNear(context, focus.constantCorrected,
                   {2.70620424359742024e1, 2.71053979366365496e1}, 4.0e-10,
                   "Munk focus corrected constant");
  checkImage(context, focus.images[0U], CervenyImageKind::True,
             9.70946017883443346, 3.76699376501307215e-4, true, 1.0,
             {-6.70864582211274985e-1, -7.41071924522222147e-1},
             "Munk focus true image");
  context.check(
      !focus.images[1U].windowPassed && !focus.images[2U].windowPassed,
      "Munk focus rejects both image beams");
  checkComplexNear(context, focus.finalContribution,
                   {1.93208362125029010, -3.82389713319614799e1}, 8.0e-9,
                   "Munk focus final ray contribution");
  checkFloatBits(context, focus.finalContribution, 0x3ff74e84U, 0xc218f4b5U,
                 "Munk focus legacy quantization");

  const CartesianCervenyDiagnostic& branch = run.branch;
  context.check(branch.leftPointIndex == 186U && branch.rightPointIndex == 187U,
                "Munk negative-KMAH indices map to C++ zero base");
  context.check(branch.kmahLeft == -1 && branch.kmahFinal == -1,
                "Munk prior branch crossing remains negative");
  checkComplexNear(context, branch.constantPrincipal,
                   {4.78825735552960374e-1, -5.03903428481553988e1}, 8.0e-10,
                   "Munk principal constant");
  checkComplexNear(context, branch.constantCorrected,
                   {-4.78825735552960374e-1, 5.03903428481553988e1}, 8.0e-10,
                   "negative KMAH reverses the complex root");
  checkImage(context, branch.images[0U], CervenyImageKind::True,
             -2.63179390808556946, 8.39963404413040946e-5, true, 1.0,
             {-2.80901139404037714e-1, -9.59649191793861323e-1},
             "Munk branch true image");
  checkImage(context, branch.images[1U], CervenyImageKind::Surface,
             -1.15263179390808546e3, 1.61115676894208377e1, true,
             8.09278466911276806e-1,
             {7.76213230040356598e-8, 2.47045457737501207e-8},
             "Munk branch surface image");
  context.check(!branch.images[2U].windowPassed,
                "Munk branch rejects bottom image");
  checkComplexNear(context, branch.rawImageSum,
                   {-2.80901061782714734e-1, -9.59649167089315580e-1}, 1.0e-11,
                   "Munk image sum preserves true/surface order");
  checkComplexNear(context, branch.finalContribution,
                   {4.84915532011030948e1, -1.36951960913375501e1}, 1.2e-8,
                   "Munk branch final ray contribution");
  checkFloatBits(context, branch.finalContribution, 0x4241f75aU, 0xc15b1f86U,
                 "Munk branch legacy quantization");
}

void testRigidReflectionOracle(Context& context) {
  constexpr double launchAngle = -7.71226401338552248e-1;
  const Environment environment(
      SoundSpeedProfile(
          {{.depth = 0.0, .soundSpeed = 1500.0, .density = 1000.0},
           {.depth = 100.0, .soundSpeed = 1500.0, .density = 1000.0}}),
      BoundaryModel::vacuum(0.0), BoundaryModel::rigid(100.0));
  const ReceiverGrid receivers(linearGrid(0.0, 100.0, 51U),
                               linearGrid(10.0, 5000.0, 201U));
  const RayPath path =
      GeometryTracer(environment,
                     IntegratorSettings{.stepLength = 10.0,
                                        .rangeLimit = 5100.0,
                                        .depthLimit = 101.0,
                                        .maximumRayPoints = 2000000U})
          .trace(Source{.depth = 50.0}, launchAngle);
  context.check(path.points.size() == 798U && path.steps.size() == 747U &&
                    path.events.size() == 50U,
                "near-boundary rigid ray preserves the Fortran point sequence");
  context.check(
      std::bit_cast<std::uint64_t>(path.points.front().slowness.depth) ==
          0xbf3e73fb8c891f9cULL,
      "launch sine uses the independent Fortran-compatible libm result");
  const RayFrequencyState frequencyState =
      FrequencyProjector(environment).project(path, 250.0, 1.0);
  const auto epsilon = pickMinimumWidthEpsilon(250.0, 1500.0, 1000.0, 2.0);
  FrequencyWorkspace workspace(250.0, receivers);
  const auto diagnostic =
      CartesianCervenyInfluence(environment, receivers)
          .accumulate(
              workspace, path, frequencyState, epsilon.value,
              CartesianCervenyDiagnosticRequest{.receiverRangeIndex = 110U,
                                                .receiverDepthIndex = 48U});
  context.check(diagnostic.has_value() && diagnostic->evaluated,
                "rigid reflection oracle receiver is evaluated");
  const CartesianCervenyDiagnostic& value = diagnostic.value();
  IntensityWorkspace intensityWorkspace(250.0, receivers);
  const auto intensityDiagnostic =
      CartesianCervenyInfluence(environment, receivers)
          .accumulateIntensity(
              intensityWorkspace, path, frequencyState, epsilon.value,
              CartesianCervenyDiagnosticRequest{.receiverRangeIndex = 110U,
                                                .receiverDepthIndex = 48U});
  context.check(
      intensityDiagnostic.has_value() && intensityDiagnostic->evaluated,
      "rigid reflection intensity receiver is evaluated");
  const double coherentMagnitude = std::abs(value.finalContribution);
  const double coherentImageSumIntensity =
      coherentMagnitude * coherentMagnitude;
  context.checkNear(
      intensityDiagnostic->intensityIncrement, coherentImageSumIntensity,
      2.0e-10, "rigid images sum coherently before beam intensity is formed");
  context.checkNear(
      intensityWorkspace.at(48U, 110U), coherentImageSumIntensity, 2.0e-10,
      "intensity workspace adds the per-beam ABS squared increment");
  double separateImageIntensity = 0.0;
  for (const auto& image : value.images) {
    const double imageMagnitude =
        std::abs(value.constantCorrected * image.contribution);
    separateImageIntensity += imageMagnitude * imageMagnitude;
  }
  context.check(
      std::abs(separateImageIntensity - coherentImageSumIntensity) > 1.0,
      "fixture distinguishes coherent image sum from per-image power");
  context.check(value.evaluationCount == 1U && value.leftPointIndex == 429U &&
                    value.rightPointIndex == 430U,
                "rigid reflection receiver is owned by the oracle segment");
  context.checkNear(value.interpolationWeight, 9.45351890934747030e-1, 1.0e-14,
                    "rigid reflection interpolation weight");
  context.checkNear(value.interpolatedPosition.depth, 2.74997008542718504e1,
                    1.0e-12, "rigid reflection interpolated depth");
  checkComplexNear(context, value.qInterpolated,
                   {5.76209913870936353e6, 3.00000000000000047e6}, 1.0e-8,
                   "rigid reflection interpolated q");
  checkComplexNear(context, value.gammaInterpolated,
                   {3.51015691548127185e-8, -1.82754171845220517e-8}, 1.0e-20,
                   "rigid reflection interpolated gamma");
  context.check(value.kmahLeft == 1 && value.kmahFinal == 1,
                "rigid reflection KMAH remains positive");
  checkComplexNear(context, value.constantCorrected,
                   {2.16480708940017799e1, -5.29794011577855617}, 2.0e-14,
                   "rigid reflection corrected constant");
  context.checkNear(value.rightPhase, 4.39822971502571107e1, 0.0,
                    "rigid reflection accumulates 14 pi phase");
  checkImage(context, value.images[0U], CervenyImageKind::True,
             6.85002991457281496e1, 1.34701401038128110e-1, true, 1.0,
             {9.84219396825598003e-2, -8.68417319570494395e-1},
             "rigid reflection true image");
  checkImage(context, value.images[1U], CervenyImageKind::Surface,
             -1.23499700854271850e2, 4.37843581837594031e-1, true, 1.0,
             {-6.40224503426137126e-1, 8.17817268776530049e-2},
             "rigid reflection surface image");
  checkImage(context, value.images[2U], CervenyImageKind::Bottom,
             7.65002991457281496e1, 1.68001609932696860e-1, true, 1.0,
             {3.99915559349885930e-1, -7.44774026278724133e-1},
             "rigid reflection bottom image");
  checkComplexNear(context, value.rawImageSum,
                   {-1.41887004393691341e-1, -1.53140961897156558},
                   kCrossCompilerImageTolerance, "rigid reflection image sum");
  checkComplexNear(context, value.finalContribution,
                   {-1.11848963840907825e1, -3.24003551467677156e1},
                   kCrossCompilerContributionTolerance,
                   "rigid reflection final ray contribution");
  checkFloatBits(context, value.finalContribution, 0xc132f556U, 0xc20199f7U,
                 "rigid reflection legacy quantization");
}

RayState makeSyntheticState(double range) {
  return RayState{.position = {.range = range, .depth = 50.0},
                  .slowness = {.range = 1.0 / 1500.0, .depth = 0.0},
                  .dynamicP = {1.0, 0.0},
                  .dynamicQ = {0.0, 1.0},
                  .soundSpeed = 1500.0,
                  .realTravelTime = range / 1500.0};
}

void testImageCountDispatch(Context& context) {
  const Environment environment(
      SoundSpeedProfile(
          {{.depth = 0.0, .soundSpeed = 1500.0, .density = 1000.0},
           {.depth = 100.0, .soundSpeed = 1500.0, .density = 1000.0}}),
      BoundaryModel::vacuum(0.0), BoundaryModel::rigid(100.0));
  const ReceiverGrid receivers({50.0}, {0.0, 10.0});
  RayPath path;
  path.launchAngle = 0.0;
  path.points = {makeSyntheticState(0.0), makeSyntheticState(5.0),
                 makeSyntheticState(10.0)};
  const RayFrequencyState state{
      .frequency = 50.0,
      .points = {RayFrequencyPoint{.complexTravelTime = {0.0, 0.0},
                                   .amplitude = 1.0,
                                   .reflectionPhase = 0.0,
                                   .active = true},
                 RayFrequencyPoint{.complexTravelTime = {5.0 / 1500.0, 0.0},
                                   .amplitude = 1.0,
                                   .reflectionPhase = 0.0,
                                   .active = true},
                 RayFrequencyPoint{.complexTravelTime = {10.0 / 1500.0, 0.0},
                                   .amplitude = 1.0,
                                   .reflectionPhase = 0.0,
                                   .active = true}}};

  for (std::size_t imageCount = 1U; imageCount <= 3U; ++imageCount) {
    const CartesianCervenyInfluence influence(
        environment, receivers,
        CartesianCervenySettings{.imageCount = imageCount, .beamWindow = 5});
    FrequencyWorkspace hotWorkspace(50.0, receivers);
    static_cast<void>(
        influence.accumulate(hotWorkspace, path, state, {0.0, 100.0}));

    FrequencyWorkspace diagnosticWorkspace(50.0, receivers);
    const auto diagnostic = influence.accumulate(
        diagnosticWorkspace, path, state, {0.0, 100.0},
        CartesianCervenyDiagnosticRequest{.receiverRangeIndex = 1U,
                                          .receiverDepthIndex = 0U});
    const std::string label = "image-count " + std::to_string(imageCount);
    context.check(diagnostic.has_value() && diagnostic->evaluated,
                  label + " diagnostic receiver is evaluated");
    context.check(std::equal(hotWorkspace.pressure().begin(),
                             hotWorkspace.pressure().end(),
                             diagnosticWorkspace.pressure().begin(),
                             diagnosticWorkspace.pressure().end()),
                  label + " hot and diagnostic paths are bitwise identical");
    if (!diagnostic.has_value() || !diagnostic->evaluated) {
      continue;
    }
    std::complex<double> expectedImageSum{};
    for (std::size_t imageIndex = 0U; imageIndex < imageCount; ++imageIndex) {
      expectedImageSum += diagnostic->images[imageIndex].contribution;
    }
    checkComplexNear(context, diagnostic->rawImageSum, expectedImageSum, 0.0,
                     label + " evaluates the requested images");
  }
}

void testTerminalInactivePointIsRetained(Context& context) {
  const Environment environment(
      SoundSpeedProfile(
          {{.depth = 0.0, .soundSpeed = 1500.0, .density = 1000.0},
           {.depth = 100.0, .soundSpeed = 1500.0, .density = 1000.0}}),
      BoundaryModel::vacuum(0.0), BoundaryModel::rigid(100.0));
  const ReceiverGrid receivers({50.0}, {0.0, 10.0});
  const CartesianCervenyInfluence influence(
      environment, receivers,
      CartesianCervenySettings{.imageCount = 1U, .beamWindow = 5});
  RayPath path;
  path.launchAngle = 0.0;
  path.points = {makeSyntheticState(0.0), makeSyntheticState(5.0),
                 makeSyntheticState(10.0)};
  RayFrequencyState state{
      .frequency = 50.0,
      .points = {RayFrequencyPoint{.complexTravelTime = {0.0, 0.0},
                                   .amplitude = 1.0,
                                   .reflectionPhase = 0.0,
                                   .active = true},
                 RayFrequencyPoint{.complexTravelTime = {5.0 / 1500.0, 0.0},
                                   .amplitude = 1.0,
                                   .reflectionPhase = 0.0,
                                   .active = true},
                 RayFrequencyPoint{.complexTravelTime = {10.0 / 1500.0, 0.0},
                                   .amplitude = 1.0,
                                   .reflectionPhase = 0.0,
                                   .active = false}}};
  FrequencyWorkspace retained(50.0, receivers);
  static_cast<void>(influence.accumulate(retained, path, state, {0.0, 100.0}));
  context.check(retained.at(0U, 1U) != std::complex<double>{},
                "segment ending at first inactive point is retained");

  state.points[1U].active = false;
  FrequencyWorkspace suppressed(50.0, receivers);
  static_cast<void>(
      influence.accumulate(suppressed, path, state, {0.0, 100.0}));
  context.check(suppressed.at(0U, 1U) == std::complex<double>{},
                "suffix starting from inactive point is suppressed");
}

void testStatisticsAreOptInAndCountHotPathWork(Context& context) {
  const Environment environment(
      SoundSpeedProfile(
          {{.depth = 0.0, .soundSpeed = 1500.0, .density = 1000.0},
           {.depth = 100.0, .soundSpeed = 1500.0, .density = 1000.0}}),
      BoundaryModel::vacuum(0.0), BoundaryModel::rigid(100.0));
  const ReceiverGrid receivers({50.0}, {0.0, 10.0});
  const CartesianCervenyInfluence influence(
      environment, receivers,
      CartesianCervenySettings{.imageCount = 1U, .beamWindow = 5});
  RayPath path;
  path.launchAngle = 0.0;
  path.points = {makeSyntheticState(0.0), makeSyntheticState(5.0),
                 makeSyntheticState(10.0)};
  const RayFrequencyState state{
      .frequency = 50.0,
      .points = {RayFrequencyPoint{.complexTravelTime = {0.0, 0.0},
                                   .amplitude = 1.0,
                                   .reflectionPhase = 0.0,
                                   .active = true},
                 RayFrequencyPoint{.complexTravelTime = {5.0 / 1500.0, 0.0},
                                   .amplitude = 1.0,
                                   .reflectionPhase = 0.0,
                                   .active = true},
                 RayFrequencyPoint{.complexTravelTime = {10.0 / 1500.0, 0.0},
                                   .amplitude = 1.0,
                                   .reflectionPhase = 0.0,
                                   .active = true}}};
  FrequencyWorkspace workspace(50.0, receivers);
  CartesianCervenyStatistics statistics;

  static_cast<void>(influence.accumulate(workspace, path, state, {0.0, 100.0},
                                         std::nullopt, &statistics));

  context.check(statistics.rayAccumulations == 1U &&
                    statistics.validatedRayPoints == path.points.size() &&
                    statistics.validatedWorkspaceValues ==
                        2U * workspace.pressure().size(),
                "opt-in statistics count defensive validation work");
  context.check(statistics.activeRayPoints == path.points.size() &&
                    statistics.segmentCandidates == 1U &&
                    statistics.eligibleSegments == 1U &&
                    statistics.receiverRangeEvaluations == 1U &&
                    statistics.receiverDepthEvaluations == 1U &&
                    statistics.imageEvaluations == 1U,
                "opt-in statistics count precompute and hot-loop work");
  context.check(statistics.windowRejections + statistics.taperRejections +
                        statistics.nonzeroImageContributions ==
                    statistics.imageEvaluations,
                "each image evaluation records one terminal outcome");
  context.check(statistics.validationSeconds >= 0.0 &&
                    statistics.precomputeSeconds >= 0.0 &&
                    statistics.hotLoopSeconds >= 0.0,
                "opt-in statistics expose non-negative phase timings");
}

void testBranchCutAndHermitePrimitives(Context& context) {
  context.check(
      updateCervenyKmah({-2.4288939311484493e7, -6.9996709949595225e5},
                        {-2.2991860400679678e7, 4.2938731999627344e3}, 1) == -1,
      "Munk q imaginary crossing flips KMAH");
  context.check(
      updateCervenyKmah({6.7944202263062631e4, -3.7722193856823377e7},
                        {-6.7817106255825528e5, -3.8184300925539017e7}, 1) == 1,
      "real-q sign change without imaginary crossing does not flip");
  context.check(
      updateCervenyKmah({1.0, -7.0}, {-1.0, -7.0}, 1, BeamWidthMode::Wkb) == -1,
      "WKB real-q zero crossing flips KMAH");
  context.check(
      updateCervenyKmah({1.0, -7.0}, {2.0, 7.0}, 1, BeamWidthMode::Wkb) == 1,
      "WKB ignores an imaginary-q crossing without a real crossing");
  context.expectThrows<ValidationError>(
      [] { static_cast<void>(updateCervenyKmah({1.0, -1.0}, {-1.0, 1.0}, 2)); },
      "KMAH contract accepts only the two legacy signs");
  context.expectThrows<ValidationError>(
      [] {
        static_cast<void>(updateCervenyKmah({1.0, -1.0}, {-1.0, 1.0}, 1,
                                            static_cast<BeamWidthMode>(999)));
      },
      "KMAH rejects an unknown beam-width mode");
  context.checkNear(cervenyHermiteTaper(1.0, 1.0, 2.0), 1.0, 0.0,
                    "Hermite inner boundary is inclusive");
  context.checkNear(cervenyHermiteTaper(2.0, 1.0, 2.0), 0.0, 0.0,
                    "Hermite outer boundary is inclusive");
  context.checkNear(cervenyHermiteTaper(1.5, 1.0, 2.0), 0.5, 0.0,
                    "Hermite cubic midpoint");
}

void testValidationContracts(Context& context) {
  const Environment environment(
      SoundSpeedProfile(
          {{.depth = 0.0, .soundSpeed = 1500.0, .density = 1000.0},
           {.depth = 100.0, .soundSpeed = 1500.0, .density = 1000.0}}),
      BoundaryModel::vacuum(0.0), BoundaryModel::rigid(100.0));

  context.expectThrows<ValidationError>(
      [&] {
        static_cast<void>(CartesianCervenyInfluence(
            environment, ReceiverGrid({50.0}, {0.0, 10.0}),
            CartesianCervenySettings{.imageCount = 0U, .beamWindow = 5}));
      },
      "zero image count is rejected");
  context.expectThrows<ValidationError>(
      [&] {
        static_cast<void>(CartesianCervenyInfluence(
            environment, ReceiverGrid({50.0}, {0.0, 10.0}),
            CartesianCervenySettings{.imageCount = 4U, .beamWindow = 5}));
      },
      "image count above the three legacy images is rejected");
  context.expectThrows<ValidationError>(
      [&] {
        static_cast<void>(CartesianCervenyInfluence(
            environment, ReceiverGrid({50.0}, {0.0, 10.0}),
            CartesianCervenySettings{.imageCount = 1U, .beamWindow = 0}));
      },
      "non-positive beam window is rejected");
  context.expectThrows<ValidationError>(
      [&] {
        static_cast<void>(CartesianCervenyInfluence(
            environment, ReceiverGrid({50.0}, {0.0, 10.0}),
            CartesianCervenySettings{}, static_cast<BeamWidthMode>(999)));
      },
      "unknown beam-width mode is rejected");
  context.expectThrows<ValidationError>(
      [&] {
        static_cast<void>(CartesianCervenyInfluence(
            environment, ReceiverGrid({50.0}, {10.0})));
      },
      "single receiver range is rejected");
  context.expectThrows<ValidationError>(
      [&] {
        static_cast<void>(CartesianCervenyInfluence(
            environment, ReceiverGrid({50.0}, {0.0, 10.0, 21.0})));
      },
      "nonuniform receiver ranges are rejected");

  const ReceiverGrid receivers({50.0}, {0.0, 10.0});
  const CartesianCervenyInfluence influence(
      environment, receivers,
      CartesianCervenySettings{.imageCount = 1U, .beamWindow = 5});
  const CartesianCervenyInfluence wkbInfluence(
      environment, receivers,
      CartesianCervenySettings{.imageCount = 1U, .beamWindow = 5},
      BeamWidthMode::Wkb);
  RayPath path;
  path.launchAngle = 0.0;
  path.points = {makeSyntheticState(0.0), makeSyntheticState(5.0),
                 makeSyntheticState(10.0)};
  const auto makeState = [] {
    return RayFrequencyState{
        .frequency = 50.0,
        .points = {RayFrequencyPoint{.complexTravelTime = {0.0, 0.0},
                                     .amplitude = 1.0,
                                     .reflectionPhase = 0.0,
                                     .active = true},
                   RayFrequencyPoint{.complexTravelTime = {5.0 / 1500.0, 0.0},
                                     .amplitude = 1.0,
                                     .reflectionPhase = 0.0,
                                     .active = true},
                   RayFrequencyPoint{.complexTravelTime = {10.0 / 1500.0, 0.0},
                                     .amplitude = 1.0,
                                     .reflectionPhase = 0.0,
                                     .active = true}}};
  };
  {
    FrequencyWorkspace workspace(50.0, receivers);
    static_cast<void>(
        wkbInfluence.accumulate(workspace, path, makeState(), {100.0, 0.0}));
  }
  context.expectThrows<ValidationError>(
      [&] {
        FrequencyWorkspace workspace(50.0, receivers);
        static_cast<void>(wkbInfluence.accumulate(workspace, path, makeState(),
                                                  {0.0, 100.0}));
      },
      "WKB influence rejects an imaginary epsilon");

  context.expectThrows<ValidationError>(
      [&] {
        RayFrequencyState state = makeState();
        state.points[0U].active = false;
        FrequencyWorkspace workspace(50.0, receivers);
        static_cast<void>(
            influence.accumulate(workspace, path, state, {0.0, 100.0}));
      },
      "inactive source frequency point is rejected");
  context.expectThrows<ValidationError>(
      [&] {
        RayFrequencyState state = makeState();
        state.points[1U].active = false;
        FrequencyWorkspace workspace(50.0, receivers);
        static_cast<void>(
            influence.accumulate(workspace, path, state, {0.0, 100.0}));
      },
      "active state cannot restart after becoming inactive");
  context.expectThrows<ValidationError>(
      [&] {
        RayFrequencyState state = makeState();
        state.points[2U].amplitude = std::numeric_limits<double>::quiet_NaN();
        FrequencyWorkspace workspace(50.0, receivers);
        static_cast<void>(
            influence.accumulate(workspace, path, state, {0.0, 100.0}));
      },
      "non-finite frequency-point acoustics are rejected");
  context.expectThrows<ValidationError>(
      [&] {
        RayFrequencyState state = makeState();
        state.points[2U].amplitude = -1.0;
        FrequencyWorkspace workspace(50.0, receivers);
        static_cast<void>(
            influence.accumulate(workspace, path, state, {0.0, 100.0}));
      },
      "negative frequency-point amplitude is rejected");
  context.expectThrows<ValidationError>(
      [&] {
        RayFrequencyState state = makeState();
        state.frequency = 100.0;
        FrequencyWorkspace workspace(50.0, receivers);
        static_cast<void>(
            influence.accumulate(workspace, path, state, {0.0, 100.0}));
      },
      "workspace and frequency-state mismatch is rejected");
  context.expectThrows<ValidationError>(
      [&] {
        RayFrequencyState state = makeState();
        state.points.pop_back();
        FrequencyWorkspace workspace(50.0, receivers);
        static_cast<void>(
            influence.accumulate(workspace, path, state, {0.0, 100.0}));
      },
      "geometry and frequency-state size mismatch is rejected");
  context.expectThrows<ValidationError>(
      [&] {
        RayFrequencyState state = makeState();
        FrequencyWorkspace workspace(50.0,
                                     ReceiverGrid({40.0, 50.0}, {0.0, 10.0}));
        static_cast<void>(
            influence.accumulate(workspace, path, state, {0.0, 100.0}));
      },
      "workspace and receiver-grid size mismatch is rejected");
  context.expectThrows<ValidationError>(
      [&] {
        RayFrequencyState state = makeState();
        FrequencyWorkspace workspace(50.0, receivers);
        static_cast<void>(
            influence.accumulate(workspace, path, state, {1.0, 100.0}));
      },
      "epsilon must remain purely positive imaginary");
  context.expectThrows<ValidationError>(
      [&] {
        RayFrequencyState state = makeState();
        FrequencyWorkspace workspace(50.0, receivers);
        workspace.at(0U, 1U) = {std::numeric_limits<double>::infinity(), 0.0};
        static_cast<void>(
            influence.accumulate(workspace, path, state, {0.0, 100.0}));
      },
      "non-finite existing workspace pressure is rejected");
  context.expectThrows<ValidationError>(
      [&] {
        RayFrequencyState state = makeState();
        state.points[2U].amplitude = std::numeric_limits<double>::max();
        FrequencyWorkspace workspace(50.0, receivers);
        static_cast<void>(
            influence.accumulate(workspace, path, state, {0.0, 100.0}));
      },
      "non-finite computed workspace pressure is rejected");
}

}  // namespace

int main() {
  Context context;
  testBranchCutAndHermitePrimitives(context);
  testDirectOracle(context);
  testMunkOracles(context);
  testRigidReflectionOracle(context);
  testImageCountDispatch(context);
  testTerminalInactivePointIsRetained(context);
  testStatisticsAreOptInAndCountHotPathWork(context);
  testValidationContracts(context);

  if (context.failureCount() != 0) {
    std::cerr << context.failureCount()
              << " Cartesian Cerveny influence assertion(s) failed\n";
    return 1;
  }
  std::cout
      << "All Bellhop RayReuse Cartesian Cerveny influence tests passed\n";
  return 0;
}
