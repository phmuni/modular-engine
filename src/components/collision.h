#pragma once
// AABB collision component.

#include <glm/glm.hpp>

struct Collision {
  glm::vec3 min{-0.5f, -0.5f, -0.5f};
  glm::vec3 max{0.5f, 0.5f, 0.5f};
  bool isStatic = false;

  Collision() = default;
};
