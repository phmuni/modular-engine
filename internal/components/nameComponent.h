#pragma once

#include <string>

struct NameComponent {
  std::string name;

  NameComponent(const std::string &n) : name(n) {}
};