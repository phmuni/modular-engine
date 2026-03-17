#pragma once
// Name component for entity display labels.

#include <string>

struct Name {
  std::string name;

  Name(const std::string &n) : name(n) {}
};