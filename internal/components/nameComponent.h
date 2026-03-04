#pragma once
// Name component for entity display labels.

#include <string>

struct NameComponent {
  std::string name;

  NameComponent(const std::string &n) : name(n) {}
};