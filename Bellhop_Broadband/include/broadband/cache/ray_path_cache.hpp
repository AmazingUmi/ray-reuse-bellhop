#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "broadband/ray/ray_path.hpp"

namespace broadband {

class RayPathCache {
 public:
  void reserve(std::size_t rayCount);
  void append(RayPath path);
  void freeze();

  [[nodiscard]] bool frozen() const noexcept;
  [[nodiscard]] bool empty() const noexcept;
  [[nodiscard]] std::size_t size() const noexcept;
  [[nodiscard]] const RayPath& at(std::size_t index) const;
  [[nodiscard]] std::span<const RayPath> paths() const;
  [[nodiscard]] std::size_t memoryFootprintBytes() const noexcept;
  [[nodiscard]] std::uint64_t contentFingerprint() const;

 private:
  std::vector<RayPath> paths_;
  bool frozen_{false};
};

}  // namespace broadband
