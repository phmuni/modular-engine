#pragma once
// Transform component for position, rotation, and scale.

#include <glm/glm.hpp>

struct Transform {
  glm::vec3 position{0, 0, 0};
  glm::vec3 rotation{0, 0, 0};
  glm::vec3 scale{1, 1, 1};

  Transform() = default;
  Transform(glm::vec3 position, glm::vec3 rotation, glm::vec3 scale)
      : position(position), rotation(rotation), scale(scale) {}
};
