#pragma once
// Light component supporting directional, point, and spot lights.

#include <glm/glm.hpp>

enum class LightType { Directional, Point, Spot };

struct Light {
  LightType type = LightType::Point;
  glm::vec3 position{0.0f, 0.0f, 0.0f};
  glm::vec3 direction{0.0f, -1.0f, 0.0f};
  glm::vec3 color{1.0f, 1.0f, 1.0f};
  float intensity = 1.0f;
  float ambient = 0.2f;
  float constant = 1.0f;
  float linear = 0.09f;
  float quadratic = 0.032f;
  float cutOff = glm::cos(glm::radians(12.5f));
  float outerCutOff = glm::cos(glm::radians(17.5f));

  Light() = default;
  Light(LightType type, glm::vec3 position, glm::vec3 direction, glm::vec3 color)
      : type(type), position(position), direction(direction), color(color) {}
};
