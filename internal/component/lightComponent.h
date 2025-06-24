#pragma once

#include <glm/glm.hpp>

enum class LightType { Point, Directional, Spot };

struct LightComponent {
  LightType type;

  glm::vec3 position = glm::vec3(0.0f);               // point/spot
  glm::vec3 direction = glm::vec3(0.0f, -1.0f, 0.0f); // directional/spot

  glm::vec3 color = glm::vec3(1.0f);
  float intensity = 1.0f;
  float ambient;

  // Atenuation (point and spot)
  float constant;
  float linear;
  float quadratic;

  // Spotlights
  float cutOff = glm::cos(glm::radians(12.5f));
  float outerCutOff = glm::cos(glm::radians(17.5f));
};
