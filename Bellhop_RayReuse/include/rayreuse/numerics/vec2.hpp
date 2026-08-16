#pragma once

#include <cmath>

namespace rayreuse {

struct Vec2 {
  double range{};
  double depth{};

  friend constexpr bool operator==(const Vec2&, const Vec2&) = default;
};

[[nodiscard]] constexpr Vec2 operator+(Vec2 lhs, Vec2 rhs) noexcept {
  return {lhs.range + rhs.range, lhs.depth + rhs.depth};
}

[[nodiscard]] constexpr Vec2 operator-(Vec2 lhs, Vec2 rhs) noexcept {
  return {lhs.range - rhs.range, lhs.depth - rhs.depth};
}

[[nodiscard]] constexpr Vec2 operator*(double scalar, Vec2 value) noexcept {
  return {scalar * value.range, scalar * value.depth};
}

[[nodiscard]] constexpr Vec2 operator*(Vec2 value, double scalar) noexcept {
  return scalar * value;
}

[[nodiscard]] constexpr Vec2 operator/(Vec2 value, double scalar) noexcept {
  return {value.range / scalar, value.depth / scalar};
}

[[nodiscard]] constexpr double dot(Vec2 lhs, Vec2 rhs) noexcept {
  return lhs.range * rhs.range + lhs.depth * rhs.depth;
}

// Match the locked gfortran two-element DOT_PRODUCT rounding used by strict
// boundary sign tests.
[[nodiscard]] inline double fortranDotProduct2D(Vec2 lhs, Vec2 rhs) noexcept {
  const double rangeProduct = lhs.range * rhs.range;
  return std::fma(lhs.depth, rhs.depth, rangeProduct);
}

[[nodiscard]] constexpr double squaredNorm(Vec2 value) noexcept {
  return dot(value, value);
}

[[nodiscard]] inline double norm(Vec2 value) noexcept {
  return std::sqrt(squaredNorm(value));
}

[[nodiscard]] inline bool isFinite(Vec2 value) noexcept {
  return std::isfinite(value.range) && std::isfinite(value.depth);
}

}  // namespace rayreuse
