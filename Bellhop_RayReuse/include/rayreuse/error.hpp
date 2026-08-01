#pragma once

#include <stdexcept>
#include <string>

namespace rayreuse {

class BellhopError : public std::runtime_error {
 public:
  explicit BellhopError(const std::string& message)
      : std::runtime_error(message) {}
};

class ValidationError final : public BellhopError {
 public:
  explicit ValidationError(const std::string& message)
      : BellhopError(message) {}
};

}  // namespace rayreuse
