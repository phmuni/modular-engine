#pragma once
// Name component for easy entity identification.

#include <string>

struct Name {
  std::string name{};

  Name() = default;
  explicit Name(std::string name) : name(std::move(name)) {}
};