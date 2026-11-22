#pragma once
// Light component supporting directional, point, and spot lights.

#include <glm/glm.hpp>

enum class LightType { Directional, Point, Spot };

// Like Unity's, Godot's and Unreal's lights, it is placed and aimed by its entity: `position` and `direction` are LOCAL,
// relative to the entity's Transform, so turning the entity (or a parent it follows) turns the light. World direction =
// R * direction and world position = Transform.position + R * position, with R the Transform's rotation
// (see LightSystem::worldDirection and worldPosition). A spot light on a child entity is a headlight or a torch.
// Light colors are authored in sRGB; there is no per-light ambient (the fill comes from the sky, see Skybox, and the
// scene's ambient: Engine::setAmbient).
struct Light {
  LightType type = LightType::Point;
  glm::vec3 position{0.0f, 0.0f, 0.0f};   // local offset from the entity
  glm::vec3 direction{0.0f, -1.0f, 0.0f}; // local; directional and spot lights shine along it (default: down)
  glm::vec3 color{1.0f, 1.0f, 1.0f};
  float intensity = 1.0f;
  // Point and spot lights: how far the light reaches, in world units. Its brightness falls off like the inverse square
  // of the distance (softened near the light: a core 1/25 of the range wide) and is windowed to exactly 0 at `range` (see lightAttenuation in
  // clusteredLights.h). Directional lights ignore it.
  float range = 25.0f;
  float cutOff = glm::cos(glm::radians(12.5f));
  float outerCutOff = glm::cos(glm::radians(17.5f));

  // Point and spot lights: render a cube shadow map (up to EngineConfig::MAX_POINT_SHADOWS lights at once).
  // Directional lights ignore it; the first directional light always casts the sun shadow.
  bool castShadows = false;
  float shadowRange = 25.0f; // world units; nothing beyond it casts or receives this light's shadow

  Light() = default;

  // Named constructors, so a light reads as what it is. `direction` is local to the entity (see above). Spot angles are
  // in degrees (the half-angles where the light starts to fade and where it ends); chain withShadows() on point and
  // spot lights for a cube shadow map and withRange() to set how far a point or spot light reaches.
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
  Light withRange(float worldUnits) const {
    Light light = *this;
    light.range = worldUnits;
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
