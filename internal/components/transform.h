#pragma once
// Transform component: position, rotation, scale.

#include "glm/ext/vector_float3.hpp"

struct Transform {
  glm::vec3 position;
  glm::vec3 rotation;
  glm::vec3 scale;

  Transform(glm::vec3 pos = {0, 0, 0}, glm::vec3 rot = {0, 0, 0}, glm::vec3 scl = {1, 1, 1})
      : position(pos), rotation(rot), scale(scl) {}
};
