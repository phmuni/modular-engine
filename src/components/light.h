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
  // Attenuation 1 / (constant + linear d + quadratic d^2) for point and spot lights. A light reaches as far as that
  // leaves it visible (see lightRange in clusteredLights.h; there is no range field) and fades smoothly to exactly 0 there.
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

  // Named constructors, so a light reads as what it is. Spot angles are in degrees (the half-angles where the light
  // starts to fade and where it ends); chain withShadows() on point and spot lights for a cube shadow map.
  static Light directional(glm::vec3 direction, glm::vec3 color = glm::vec3(1.0f), float intensity = 1.0f) {
    Light light;
    light.type = LightType::Directional;
    light.direction = direction;
    light.color = color;
    light.intensity = intensity;
    return light;
  }
  static Light point(glm::vec3 color = glm::vec3(1.0f), float intensity = 1.0f) {
    Light light;
    light.type = LightType::Point;
    light.color = color;
    light.intensity = intensity;
    return light;
  }
  static Light spot(glm::vec3 direction, glm::vec3 color, float intensity, float innerDegrees, float outerDegrees) {
    Light light;
    light.type = LightType::Spot;
    light.direction = direction;
    light.color = color;
    light.intensity = intensity;
    light.cutOff = glm::cos(glm::radians(innerDegrees));
    light.outerCutOff = glm::cos(glm::radians(outerDegrees));
    return light;
  }
  Light withShadows(float range = 25.0f) const {
    Light light = *this;
    light.castShadows = true;
    light.shadowRange = range;
    return light;
  }
  Light(LightType type, glm::vec3 position, glm::vec3 direction, glm::vec3 color, float intensity = 1.0f,
        float cutOff = glm::cos(glm::radians(12.5f)), float outerCutOff = glm::cos(glm::radians(17.5f)))
      : type(type), position(position), direction(direction), color(color), intensity(intensity), cutOff(cutOff),
        outerCutOff(outerCutOff) {}
};
