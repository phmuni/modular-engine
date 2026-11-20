#pragma once
// AABB collision component.

#include <glm/glm.hpp>
#include <string>

struct Collision {
  glm::vec3 min{-0.5f, -0.5f, -0.5f};
  glm::vec3 max{0.5f, 0.5f, 0.5f};
  bool isStatic = false;
  // The collision layer it is on, by name (see CollisionLayers): boxes on layers that do not collide never overlap
  std::string layer = "default";

  Collision() = default;
};
