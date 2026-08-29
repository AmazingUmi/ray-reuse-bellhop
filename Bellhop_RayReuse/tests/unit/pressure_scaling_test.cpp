#include "rayreuse/field/pressure_scaling.hpp"

#include <bit>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>

#include "rayreuse/error.hpp"
#include "support/test_harness.hpp"

namespace {

using rayreuse::FrequencyWorkspace;
using rayreuse::IntensityWorkspace;
using rayreuse::ReceiverGrid;
using rayreuse::scaleCartesianIntensityToPressure;
using rayreuse::scaleCartesianPointIntensityToPressure;
using rayreuse::scaleCoherentCartesianPointPressure;
using rayreuse::scaleCoherentCartesianPressure;
using rayreuse::scaleCoherentGeometricPointPressure;
using rayreuse::scaleCoherentGeometricPressure;
using rayreuse::scaleGeometricIntensityToPressure;
using rayreuse::scaleGeometricPointIntensityToPressure;
using rayreuse::SourceGeometry;
using rayreuse::ValidationError;
using rayreuse::test::Context;

void checkComplexNear(Context& context, std::complex<double> actual,
                      std::complex<double> expected, double tolerance,
                      const std::string& message) {
  context.checkNear(actual.real(), expected.real(), tolerance,
                    message + " real");
  context.checkNear(actual.imag(), expected.imag(), tolerance,
                    message + " imaginary");
}

void testSmallMatrix(Context& context) {
  const ReceiverGrid receivers({10.0, 20.0}, {0.0, 1000.0, 4000.0});
  FrequencyWorkspace workspace(100.0, receivers);
  workspace.at(0U, 0U) = {1.0, 2.0};
  workspace.at(0U, 1U) = {3.0, 4.0};
  workspace.at(0U, 2U) = {5.0, 6.0};
  workspace.at(1U, 0U) = {-1.0, 0.5};
  workspace.at(1U, 1U) = {-3.0, -4.0};
  workspace.at(1U, 2U) = {2.0, 1.0};

  scaleCoherentCartesianPointPressure(workspace, receivers, 0.001, 1500.0);

  checkComplexNear(context, workspace.at(0U, 0U), {}, 0.0,
                   "zero range multiplies the first depth by zero");
  checkComplexNear(context, workspace.at(1U, 0U), {}, 0.0,
                   "zero range multiplies every depth by zero");
  checkComplexNear(context, workspace.at(0U, 1U),
                   {-6.3245553203367584e-7, -8.4327404271156782e-7}, 1.0e-21,
                   "1000 m row-zero scale");
  checkComplexNear(context, workspace.at(1U, 1U),
                   {6.3245553203367584e-7, 8.4327404271156782e-7}, 1.0e-21,
                   "1000 m row-one scale");
  checkComplexNear(context, workspace.at(0U, 2U),
                   {-5.2704627669472990e-7, -6.3245553203367584e-7}, 1.0e-21,
                   "4000 m row-zero scale");
  checkComplexNear(context, workspace.at(1U, 2U),
                   {-2.1081851067789195e-7, -1.0540925533894598e-7}, 1.0e-21,
                   "4000 m row-one scale");
}

void testO1ContributionAnchors(Context& context) {
  constexpr double directSpacing = 5.8372215785763523e-4;
  const ReceiverGrid directReceivers({500.0}, {0.0, 1080.0});
  FrequencyWorkspace direct(50.0, directReceivers);
  direct.at(0U, 1U) = {3.10088407099787347e1, -2.03824548128207503e1};
  scaleCoherentCartesianPointPressure(direct, directReceivers, directSpacing,
                                      1500.0);
  checkComplexNear(context, direct.at(0U, 1U),
                   {-2.5964118030924930e-6, 1.7066502661925371e-6}, 1.0e-20,
                   "direct O1 contribution scaling");

  const float directLegacyFactor = static_cast<float>(-8.3731340599810301e-8);
  context.check(std::bit_cast<std::uint32_t>(directLegacyFactor) == 0xb3b3cfcbU,
                "direct legacy SNGL factor bits");

  constexpr double munkSpacing = 7.09312989298996810e-4;
  constexpr double munkSourceSoundSpeed = 1501.38000000000011;
  const ReceiverGrid munkReceivers({575.0},
                                   {0.0, 16600.0, 59800.0000000000073});
  FrequencyWorkspace munk(50.0, munkReceivers);
  munk.at(0U, 1U) = {1.93208362125029010, -3.82389713319614799e1};
  munk.at(0U, 2U) = {4.84915532011030948e1, -1.36951960913375501e1};
  scaleCoherentCartesianPointPressure(munk, munkReceivers, munkSpacing,
                                      munkSourceSoundSpeed);
  checkComplexNear(context, munk.at(0U, 1U),
                   {-5.0096128433638360e-8, 9.9148111290158276e-7}, 1.0e-20,
                   "Munk focus O1 contribution scaling");
  checkComplexNear(context, munk.at(0U, 2U),
                   {-6.6244189637775524e-7, 1.8708973153296501e-7}, 1.0e-20,
                   "Munk negative-KMAH O1 contribution scaling");

  const float focusLegacyFactor = static_cast<float>(-2.5928550857038036e-8);
  const float branchLegacyFactor = static_cast<float>(-1.3660975007966664e-8);
  context.check(std::bit_cast<std::uint32_t>(focusLegacyFactor) == 0xb2deb97cU,
                "Munk focus legacy SNGL factor bits");
  context.check(std::bit_cast<std::uint32_t>(branchLegacyFactor) == 0xb26ab19aU,
                "Munk branch legacy SNGL factor bits");
}

void testIntensityToPressureScaling(Context& context) {
  const ReceiverGrid receivers({10.0}, {0.0, 1000.0, 4000.0});
  IntensityWorkspace intensity(100.0, receivers);
  intensity.add(0U, 0U, 4.0);
  intensity.add(0U, 1U, 9.0);
  intensity.add(0U, 2U, 25.0);

  const FrequencyWorkspace pressure = scaleCartesianPointIntensityToPressure(
      intensity, receivers, 0.001, 1500.0);
  const double beamScale = -0.001 * std::sqrt(100.0) / 1500.0;
  checkComplexNear(context, pressure.at(0U, 0U), {}, 0.0,
                   "point intensity conversion preserves zero-range rule");
  checkComplexNear(
      context, pressure.at(0U, 1U),
      {3.0 * beamScale / std::sqrt(1000.0), -0.0}, 1.0e-21,
      "point intensity is square-rooted exactly once before scaling");
  checkComplexNear(
      context, pressure.at(0U, 2U),
      {5.0 * beamScale / std::sqrt(4000.0), -0.0}, 1.0e-21,
      "point intensity conversion keeps the range spreading factor");
  context.check(intensity.at(0U, 0U) == 4.0 &&
                    intensity.at(0U, 1U) == 9.0 &&
                    intensity.at(0U, 2U) == 25.0,
                "intensity conversion leaves its strong input unchanged");

  context.expectThrows<ValidationError>(
      [&] {
        static_cast<void>(scaleCartesianPointIntensityToPressure(
            intensity, ReceiverGrid({10.0, 20.0}, {0.0, 1000.0, 4000.0}),
            0.001, 1500.0));
      },
      "intensity conversion rejects a receiver-grid size mismatch");
}

void testGeometricPointScaling(Context& context) {
  const ReceiverGrid receivers({10.0}, {0.0, 1000.0, 4000.0});
  FrequencyWorkspace coherent(100.0, receivers);
  coherent.at(0U, 0U) = {2.0, -1.0};
  coherent.at(0U, 1U) = {3.0, -4.0};
  coherent.at(0U, 2U) = {-5.0, 2.0};
  scaleCoherentGeometricPointPressure(coherent, receivers, 0.001, 1500.0);
  checkComplexNear(context, coherent.at(0U, 0U), {}, 0.0,
                   "geometric point scaling preserves zero-range rule");
  checkComplexNear(context, coherent.at(0U, 1U),
                   {3.0 * (-1.0 / std::sqrt(1000.0)),
                    -4.0 * (-1.0 / std::sqrt(1000.0))},
                   0.0, "geometric point pressure uses -1/sqrt(range)");
  checkComplexNear(context, coherent.at(0U, 2U),
                   {-5.0 * (-1.0 / std::sqrt(4000.0)),
                    2.0 * (-1.0 / std::sqrt(4000.0))},
                   0.0,
                   "geometric normalization omits Cerveny frequency scale");

  IntensityWorkspace intensity(100.0, receivers);
  intensity.add(0U, 0U, 4.0);
  intensity.add(0U, 1U, 9.0);
  intensity.add(0U, 2U, 25.0);
  const FrequencyWorkspace pressure = scaleGeometricPointIntensityToPressure(
      intensity, receivers, 0.001, 1500.0);
  checkComplexNear(context, pressure.at(0U, 0U), {}, 0.0,
                   "geometric intensity preserves zero-range rule");
  checkComplexNear(context, pressure.at(0U, 1U),
                   {3.0 * (-1.0 / std::sqrt(1000.0)), 0.0}, 0.0,
                   "geometric intensity takes sqrt before range scaling");
  checkComplexNear(context, pressure.at(0U, 2U),
                   {5.0 * (-1.0 / std::sqrt(4000.0)), 0.0}, 0.0,
                   "geometric intensity applies geometric normalization");
}

void testLineSourceScaling(Context& context) {
  const ReceiverGrid receivers({10.0, 20.0}, {0.0, 1000.0, 4000.0});
  FrequencyWorkspace workspace(100.0, receivers);
  workspace.at(0U, 0U) = {1.0, 2.0};
  workspace.at(0U, 1U) = {3.0, 4.0};
  workspace.at(0U, 2U) = {5.0, 6.0};
  workspace.at(1U, 0U) = {-1.0, 0.5};
  workspace.at(1U, 1U) = {-3.0, -4.0};
  workspace.at(1U, 2U) = {2.0, 1.0};

  scaleCoherentCartesianPressure(
      workspace, receivers, 0.001, 1500.0, SourceGeometry::Line);

  constexpr float legacyPi = 3.14159265F;
  const float linePrefix = -4.0F * std::sqrt(legacyPi);
  const double beamScale = (-0.001 * std::sqrt(100.0)) / 1500.0;
  const double lineFactor = static_cast<double>(linePrefix) * beamScale;

  context.check(std::bit_cast<std::uint32_t>(legacyPi) == 0x40490fdbU,
                "line-source legacy pi bits");
  context.check(std::bit_cast<std::uint32_t>(linePrefix) == 0xc0e2dfc5U,
                "line-source legacy prefix bits");
  context.checkNear(lineFactor, 4.7265437444051105e-5, 1.0e-20,
                    "line-source mixed-precision scale anchor");

  checkComplexNear(context, workspace.at(0U, 0U),
                   {1.0 * lineFactor, 2.0 * lineFactor}, 1.0e-20,
                   "line source retains the zero-range field");
  checkComplexNear(context, workspace.at(0U, 1U),
                   {3.0 * lineFactor, 4.0 * lineFactor}, 1.0e-20,
                   "line-source scaling is range independent");
  checkComplexNear(context, workspace.at(1U, 2U),
                   {2.0 * lineFactor, 1.0 * lineFactor}, 1.0e-20,
                   "line-source scaling uses the same factor at all ranges");

  IntensityWorkspace intensity(100.0, receivers);
  intensity.add(0U, 0U, 4.0);
  intensity.add(0U, 1U, 9.0);
  intensity.add(0U, 2U, 25.0);

  const FrequencyWorkspace lineIntensity = scaleCartesianIntensityToPressure(
      intensity, receivers, 0.001, 1500.0, SourceGeometry::Line);
  checkComplexNear(context, lineIntensity.at(0U, 0U),
                   {2.0 * lineFactor, 0.0}, 1.0e-20,
                   "line intensity retains zero range");
  checkComplexNear(context, lineIntensity.at(0U, 1U),
                   {3.0 * lineFactor, 0.0}, 1.0e-20,
                   "line intensity preserves mixed-precision factor");
  checkComplexNear(context, lineIntensity.at(0U, 2U),
                   {5.0 * lineFactor, 0.0}, 1.0e-20,
                   "line intensity is range independent");

  // Geometric normalization line source test
  FrequencyWorkspace geomLine(100.0, receivers);
  geomLine.at(0U, 0U) = {2.0, -3.0};
  geomLine.at(0U, 1U) = {4.0, 5.0};
  scaleCoherentGeometricPressure(
      geomLine, receivers, 0.001, 1500.0, SourceGeometry::Line);
  const double geomLineFactor = static_cast<double>(linePrefix) * (-1.0);
  checkComplexNear(context, geomLine.at(0U, 0U),
                   {2.0 * geomLineFactor, -3.0 * geomLineFactor}, 1.0e-15,
                   "geometric line normalization retains zero range");
  checkComplexNear(context, geomLine.at(0U, 1U),
                   {4.0 * geomLineFactor, 5.0 * geomLineFactor}, 1.0e-15,
                   "geometric line normalization uses -4*sqrt(pi)*(-1)");
}

void testValidation(Context& context) {
  const ReceiverGrid receivers({10.0}, {0.0, 1000.0});

  context.expectThrows<ValidationError>(
      [&] {
        FrequencyWorkspace workspace(50.0, receivers);
        scaleCoherentCartesianPointPressure(workspace, receivers, 0.0, 1500.0);
      },
      "zero launch-angle spacing is rejected");
  context.expectThrows<ValidationError>(
      [&] {
        FrequencyWorkspace workspace(50.0, receivers);
        scaleCoherentCartesianPointPressure(
            workspace, receivers, std::numeric_limits<double>::quiet_NaN(),
            1500.0);
      },
      "non-finite launch-angle spacing is rejected");
  context.expectThrows<ValidationError>(
      [&] {
        FrequencyWorkspace workspace(50.0, receivers);
        scaleCoherentCartesianPointPressure(workspace, receivers, 0.001,
                                            -1500.0);
      },
      "non-positive source sound speed is rejected");
  context.expectThrows<ValidationError>(
      [&] {
        FrequencyWorkspace workspace(50.0,
                                     ReceiverGrid({10.0, 20.0}, {0.0, 1000.0}));
        scaleCoherentCartesianPointPressure(workspace, receivers, 0.001,
                                            1500.0);
      },
      "workspace and receiver-grid size mismatch is rejected");
  context.expectThrows<ValidationError>(
      [&] {
        FrequencyWorkspace workspace(50.0, receivers);
        workspace.at(0U, 1U) = {std::numeric_limits<double>::infinity(), 0.0};
        scaleCoherentCartesianPointPressure(workspace, receivers, 0.001,
                                            1500.0);
      },
      "non-finite input pressure is rejected");

  const ReceiverGrid overflowReceivers({10.0}, {0.0, 1.0});
  FrequencyWorkspace overflow(100.0, overflowReceivers);
  const double maximum = std::numeric_limits<double>::max();
  overflow.at(0U, 0U) = {maximum, maximum};
  overflow.at(0U, 1U) = {maximum, maximum};
  context.expectThrows<ValidationError>(
      [&] {
        scaleCoherentCartesianPointPressure(overflow, overflowReceivers, 1.0,
                                            1.0);
      },
      "scaling overflow is rejected");
  context.check(
      overflow.at(0U, 0U) == std::complex<double>{maximum, maximum} &&
          overflow.at(0U, 1U) == std::complex<double>{maximum, maximum},
      "failed scaling leaves the workspace unchanged");
}

}  // namespace

int main() {
  Context context;
  testSmallMatrix(context);
  testO1ContributionAnchors(context);
  testIntensityToPressureScaling(context);
  testGeometricPointScaling(context);
  testLineSourceScaling(context);
  testValidation(context);

  if (context.failureCount() != 0) {
    std::cerr << context.failureCount()
              << " pressure-scaling assertion(s) failed\n";
    return 1;
  }
  std::cout << "All Bellhop RayReuse pressure-scaling tests passed\n";
  return 0;
}
