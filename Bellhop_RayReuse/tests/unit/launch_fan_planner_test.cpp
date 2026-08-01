#include "rayreuse/model/launch_fan_planner.hpp"

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <numbers>
#include <optional>
#include <string>
#include <vector>

#include "rayreuse/error.hpp"
#include "support/test_harness.hpp"

namespace {

using rayreuse::LaunchFanPlan;
using rayreuse::LaunchFanPlanner;
using rayreuse::LaunchFanPlanningInput;
using rayreuse::ValidationError;
using rayreuse::test::Context;

LaunchFanPlanningInput directCaseInput() {
  return LaunchFanPlanningInput{
      .frequencies = {50.0, 250.0},
      .sourceSoundSpeed = 1500.0,
      .waterDepth = 1000.0,
      .maximumRange = 5000.0,
      .minimumLaunchAngle = -5.0 * std::numbers::pi / 180.0,
      .maximumLaunchAngle = 5.0 * std::numbers::pi / 180.0,
      .explicitLaunchAngleCount = std::nullopt,
  };
}

void testStandardCaseCounts(Context& context) {
  struct ExpectedCase {
    LaunchFanPlanningInput input;
    std::size_t phaseCount;
    std::size_t depthCount;
    std::size_t sufficiencyCount;
    std::size_t finalCount;
  };

  const std::vector<ExpectedCase> cases{
      {.input = directCaseInput(),
       .phaseCount = 300U,
       .depthCount = 157U,
       .sufficiencyCount = 14U,
       .finalCount = 300U},
      {.input =
           LaunchFanPlanningInput{
               .frequencies = {50.0},
               .sourceSoundSpeed = 1500.0,
               .waterDepth = 1000.0,
               .maximumRange = 5000.0,
               .minimumLaunchAngle = -5.0 * std::numbers::pi / 180.0,
               .maximumLaunchAngle = 5.0 * std::numbers::pi / 180.0,
               .explicitLaunchAngleCount = std::nullopt,
           },
       .phaseCount = 300U,
       .depthCount = 157U,
       .sufficiencyCount = 7U,
       .finalCount = 300U},
      {.input =
           LaunchFanPlanningInput{
               .frequencies = {100.0, 500.0},
               .sourceSoundSpeed = 1500.0,
               .waterDepth = 100.0,
               .maximumRange = 5000.0,
               .minimumLaunchAngle = -89.0 * std::numbers::pi / 180.0,
               .maximumLaunchAngle = 89.0 * std::numbers::pi / 180.0,
               .explicitLaunchAngleCount = std::nullopt,
           },
       .phaseCount = 500U,
       .depthCount = 1570U,
       .sufficiencyCount = 312U,
       .finalCount = 1570U},
      {.input =
           LaunchFanPlanningInput{
               .frequencies = {1000.0, 5000.0},
               .sourceSoundSpeed = 1500.0,
               .waterDepth = 1000.0,
               .maximumRange = 10000.0,
               .minimumLaunchAngle = -2.0 * std::numbers::pi / 180.0,
               .maximumLaunchAngle = 2.0 * std::numbers::pi / 180.0,
               .explicitLaunchAngleCount = std::nullopt,
           },
       .phaseCount = 10000U,
       .depthCount = 314U,
       .sufficiencyCount = 33U,
       .finalCount = 10000U},
      {.input =
           LaunchFanPlanningInput{
               .frequencies = {50.0, 250.0},
               .sourceSoundSpeed = 1501.38,
               .waterDepth = 5000.0,
               .maximumRange = 100000.0,
               .minimumLaunchAngle = -20.3 * std::numbers::pi / 180.0,
               .maximumLaunchAngle = 20.3 * std::numbers::pi / 180.0,
               .explicitLaunchAngleCount = std::nullopt,
           },
       .phaseCount = 5000U,
       .depthCount = 628U,
       .sufficiencyCount = 225U,
       .finalCount = 5000U},
      {.input =
           LaunchFanPlanningInput{
               .frequencies = {50.0},
               .sourceSoundSpeed = 1501.38,
               .waterDepth = 5000.0,
               .maximumRange = 100000.0,
               .minimumLaunchAngle = -20.3 * std::numbers::pi / 180.0,
               .maximumLaunchAngle = 20.3 * std::numbers::pi / 180.0,
               .explicitLaunchAngleCount = std::nullopt,
           },
       .phaseCount = 1000U,
       .depthCount = 628U,
       .sufficiencyCount = 102U,
       .finalCount = 1000U},
  };

  for (const ExpectedCase& expected : cases) {
    const LaunchFanPlan plan = LaunchFanPlanner::plan(expected.input);
    context.check(plan.phaseCriterionCount == expected.phaseCount,
                  "N_phase matches the Python standard-case model");
    context.check(plan.depthCriterionCount == expected.depthCount,
                  "N_depth matches the Python standard-case model");
    context.check(
        plan.minimumRecommendedAngleCount == expected.sufficiencyCount,
        "Nalpha_check matches the Python standard-case model");
    context.check(plan.launchAngleCount == expected.finalCount,
                  "Nalpha_final matches the Python standard-case model");
  }
}

void testHighestFrequencyAndAngles(Context& context) {
  LaunchFanPlanningInput input = directCaseInput();
  input.frequencies = {250.0, 50.0, 100.0};
  const LaunchFanPlan plan = LaunchFanPlanner::plan(input);

  context.check(plan.designFrequency == 250.0,
                "the highest frequency drives all count criteria");
  context.check(plan.launchAngles.size() == plan.launchAngleCount,
                "one launch angle is generated for each planned ray");
  context.check(plan.launchAngles.front() == input.minimumLaunchAngle,
                "the first launch angle is the exact lower endpoint");
  context.check(plan.launchAngles.back() == input.maximumLaunchAngle,
                "the last launch angle is the exact upper endpoint");
  context.checkNear(plan.launchAngleStep,
                    (input.maximumLaunchAngle - input.minimumLaunchAngle) /
                        static_cast<double>(plan.launchAngleCount - 1U),
                    0.0, "launch angle step spans both inclusive endpoints");
  context.checkNear(plan.launchAngles[123U] - plan.launchAngles[122U],
                    plan.launchAngleStep, 2.0e-17,
                    "interior launch angles are equally spaced in radians");
}

void testExplicitCountIsIgnored(Context& context) {
  LaunchFanPlanningInput input = directCaseInput();
  input.explicitLaunchAngleCount = 2U;
  const LaunchFanPlan smallExplicit = LaunchFanPlanner::plan(input);

  input.explicitLaunchAngleCount = std::numeric_limits<std::size_t>::max();
  const LaunchFanPlan largeExplicit = LaunchFanPlanner::plan(input);

  context.check(smallExplicit.launchAngleCount == 300U,
                "D-02 policy A ignores an undersized explicit count");
  context.check(largeExplicit.launchAngleCount == 300U,
                "D-02 policy A ignores an oversized explicit count");
}

void testFortranDegreeSubtabulationBits(Context& context) {
  LaunchFanPlanningInput input{
      .frequencies = {250.0},
      .sourceSoundSpeed = 1500.0,
      .waterDepth = 100.0,
      .maximumRange = 5000.0,
      .minimumLaunchAngle = -89.0 * (std::numbers::pi / 180.0),
      .maximumLaunchAngle = 89.0 * (std::numbers::pi / 180.0),
      .explicitLaunchAngleCount = 1570U,
      .inputDegreeBounds =
          rayreuse::LaunchAngleDegreeBounds{.minimum = -89.0, .maximum = 89.0},
  };
  const LaunchFanPlan plan = LaunchFanPlanner::plan(input);
  context.check(plan.launchAngleCount == 1570U,
                "rigid reflection case uses the depth-driven ray count");

  struct ExpectedBits {
    std::size_t index;
    std::uint64_t bits;
  };
  const std::vector<ExpectedBits> expected{
      {0U, 0xbff8da7e39bae2a3ULL},    {1U, 0xbff8d2620039429dULL},
      {392U, 0xbfe8de8c567bb2a6ULL},  {395U, 0xbfe8ade2fd71f281ULL},
      {784U, 0xbf50387303400ad2ULL},  {785U, 0x3f50387303400d83ULL},
      {1568U, 0x3ff8d2620039429eULL}, {1569U, 0x3ff8da7e39bae2a4ULL},
  };
  for (const ExpectedBits item : expected) {
    const std::uint64_t actual =
        std::bit_cast<std::uint64_t>(plan.launchAngles[item.index]);
    context.check(actual == item.bits,
                  "launch angle index " + std::to_string(item.index) +
                      " preserves Fortran degree-subtabulation bits; actual=" +
                      std::to_string(actual) +
                      ", expected=" + std::to_string(item.bits));
  }
  context.check(std::bit_cast<std::uint64_t>(plan.launchAngleStep) ==
                    0x3f60387303400c2aULL,
                "Dalpha preserves the Fortran endpoint-derived bit pattern");
}

void testInvalidInputs(Context& context) {
  const auto expectInvalid = [&context](LaunchFanPlanningInput input,
                                        const char* message) {
    context.expectThrows<ValidationError>(
        [&input] { static_cast<void>(LaunchFanPlanner::plan(input)); },
        message);
  };

  LaunchFanPlanningInput input = directCaseInput();
  input.frequencies.clear();
  expectInvalid(input, "an empty frequency collection is rejected");

  input = directCaseInput();
  input.frequencies = {50.0, 0.0};
  expectInvalid(input, "a non-positive frequency is rejected");

  input = directCaseInput();
  input.frequencies = {std::numeric_limits<double>::infinity()};
  expectInvalid(input, "an infinite frequency is rejected");

  input = directCaseInput();
  input.sourceSoundSpeed = 0.0;
  expectInvalid(input, "a non-positive source sound speed is rejected");

  input = directCaseInput();
  input.waterDepth = -1.0;
  expectInvalid(input, "a non-positive water depth is rejected");

  input = directCaseInput();
  input.maximumRange = 0.0;
  expectInvalid(input, "a non-positive maximum range is rejected");

  input = directCaseInput();
  input.minimumLaunchAngle = std::numeric_limits<double>::quiet_NaN();
  expectInvalid(input, "a non-finite angle bound is rejected");

  input = directCaseInput();
  input.maximumLaunchAngle = input.minimumLaunchAngle;
  expectInvalid(input, "an empty angle interval is rejected");

  input = directCaseInput();
  input.inputDegreeBounds = rayreuse::LaunchAngleDegreeBounds{
      .minimum = std::numeric_limits<double>::quiet_NaN(), .maximum = 5.0};
  expectInvalid(input, "a non-finite parser degree bound is rejected");

  input = directCaseInput();
  input.inputDegreeBounds =
      rayreuse::LaunchAngleDegreeBounds{.minimum = 5.0, .maximum = -5.0};
  expectInvalid(input, "a reversed parser degree interval is rejected");

  input = directCaseInput();
  input.inputDegreeBounds =
      rayreuse::LaunchAngleDegreeBounds{.minimum = -6.0, .maximum = 5.0};
  expectInvalid(input,
                "degree provenance inconsistent with radians is rejected");

  input = directCaseInput();
  input.frequencies = {std::numeric_limits<double>::max()};
  input.maximumRange = std::numeric_limits<double>::max();
  expectInvalid(input, "count arithmetic overflow is rejected");

  input = directCaseInput();
  input.maximumRange = 1.0;
  input.frequencies = {static_cast<double>(std::vector<double>{}.max_size()) *
                       10000.0};
  expectInvalid(input, "an unallocatable angle count is rejected");
}

}  // namespace

int main() {
  Context context;
  testStandardCaseCounts(context);
  testHighestFrequencyAndAngles(context);
  testExplicitCountIsIgnored(context);
  testFortranDegreeSubtabulationBits(context);
  testInvalidInputs(context);

  if (context.failureCount() != 0) {
    std::cerr << context.failureCount() << " test assertion(s) failed\n";
    return 1;
  }
  std::cout << "All Bellhop RayReuse launch fan planner tests passed\n";
  return 0;
}
