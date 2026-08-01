#include <array>
#include <complex>
#include <optional>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

#include "bellhop/field/cartesian_cerveny_influence.hpp"
#include "bellhop/field/frequency_projector.hpp"
#include "bellhop/field/frequency_workspace.hpp"
#include "bellhop/numerics/vec2.hpp"
#include "bellhop/ray/geometry_tracer.hpp"
#include "bellhop/ray/ray_path.hpp"

namespace {

template <typename Actual, typename Expected>
constexpr bool same = std::is_same_v<Actual, Expected>;

static_assert(same<decltype(bellhop::Vec2::range), double>);
static_assert(same<decltype(bellhop::Vec2::depth), double>);
static_assert(same<decltype(bellhop::RayState::position), bellhop::Vec2>);
static_assert(same<decltype(bellhop::RayState::slowness), bellhop::Vec2>);
static_assert(
    same<decltype(bellhop::RayState::dynamicP), std::array<double, 2>>);
static_assert(
    same<decltype(bellhop::RayState::dynamicQ), std::array<double, 2>>);
static_assert(same<decltype(bellhop::StepQuadrature::stepLength), double>);
static_assert(
    same<decltype(bellhop::ReflectionEvent::rayPointIndex), std::size_t>);
static_assert(
    same<decltype(bellhop::RayPath::points), std::vector<bellhop::RayState>>);
static_assert(same<decltype(bellhop::RayPath::steps),
                   std::vector<bellhop::StepQuadrature>>);
static_assert(same<decltype(bellhop::RayPath::events),
                   std::vector<bellhop::ReflectionEvent>>);

static_assert(same<decltype(bellhop::RayFrequencyPoint::complexTravelTime),
                   std::complex<double>>);
static_assert(same<decltype(bellhop::RayFrequencyPoint::amplitude), double>);
static_assert(
    same<decltype(bellhop::RayFrequencyPoint::reflectionPhase), double>);
static_assert(same<decltype(bellhop::RayFrequencyPoint::active), bool>);
static_assert(
    same<decltype(std::declval<bellhop::FrequencyWorkspace &>().pressure()),
         std::span<std::complex<double>>>);

static_assert(same<decltype(std::declval<const bellhop::GeometryTracer &>()
                                .trace(std::declval<bellhop::Source>(), 0.0)),
                   bellhop::RayPath>);
static_assert(
    same<decltype(std::declval<const bellhop::FrequencyProjector &>().project(
             std::declval<const bellhop::RayPath &>(), 50.0, 1.0)),
         bellhop::RayFrequencyState>);
static_assert(
    same<decltype(std::declval<const bellhop::CartesianCervenyInfluence &>()
                      .accumulate(
                          std::declval<bellhop::FrequencyWorkspace &>(),
                          std::declval<const bellhop::RayPath &>(),
                          std::declval<const bellhop::RayFrequencyState &>(),
                          std::complex<double>{})),
         std::optional<bellhop::CartesianCervenyDiagnostic>>);

} // namespace

int main() { return 0; }
