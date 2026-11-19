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

  // Point and spot lights: render a cube shadow map (up to EngineConfig::MAX_POINT_SHADOWS lights at once).
  // Directional lights ignore it; the first directional light always casts the sun shadow.
  bool castShadows = false;
  float shadowRange = 25.0f; // world units; nothing beyond it casts or receives this light's shadow

  Light() = default;
  Light(LightType type, glm::vec3 position, glm::vec3 direction, glm::vec3 color, float intensity = 1.0f,
        float cutOff = glm::cos(glm::radians(12.5f)), float outerCutOff = glm::cos(glm::radians(17.5f)))
      : type(type), position(position), direction(direction), color(color), intensity(intensity), cutOff(cutOff),
        outerCutOff(outerCutOff) {}
};
