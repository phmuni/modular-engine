#pragma once

#include "ecs/componentManager.h"
#include <glm/glm.hpp>

enum class LightType { Directional, Point, Spot };

struct LightComponent : public BaseComponent {
  LightType type;

  glm::vec3 position = glm::vec3(0.0f);               // point/spot
  glm::vec3 direction = glm::vec3(0.0f, -1.0f, 0.0f); // directional/spot

  glm::vec3 color = glm::vec3(1.0f);
  float intensity = 1.0f;
  float ambient = 0.2f;

  // Attenuation (point and spot)
  float constant = 1.0f;
  float linear = 0.09f;
  float quadratic = 0.032f;

  // Spotlights: cutoff angles pre-converted to cosine values (pre-computed dot products)
  // cutOff = 12.5°, outerCutOff = 17.5° stored as cos() for efficient fragment shader comparison
  float cutOff = glm::cos(glm::radians(12.5f));
  float outerCutOff = glm::cos(glm::radians(17.5f));

  LightComponent(LightType type, const glm::vec3 &position = glm::vec3(0.0f),
                 const glm::vec3 &direction = glm::vec3(0.0f, -1.0f, 0.0f), const glm::vec3 &color = glm::vec3(1.0f),
                 float intensity = 1.0f, float ambient = 0.2f, float constant = 1.0f, float linear = 0.09f,
                 float quadratic = 0.032f, float cutOff = glm::cos(glm::radians(12.5f)),
                 float outerCutOff = glm::cos(glm::radians(17.5f)))
      : type(type), position(position), direction(direction), color(color), intensity(intensity), ambient(ambient),
        constant(constant), linear(linear), quadratic(quadratic), cutOff(cutOff), outerCutOff(outerCutOff) {}
};
