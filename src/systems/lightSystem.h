#pragma once
// Light component and system for managing scene lights and uploading to shaders.

#include "foundation/ecs/componentManager.h"
#include "foundation/ecs/entityManager.h"
#include "foundation/ecs/systemManager.h"
#include "components/light.h"
#include "rendering/resources/shader.h"
#include <glm/glm.hpp>

struct Transform;

// A point or spot light rendering a cube shadow map this frame
struct PointShadowCaster {
  Entity entity;
  int slot; // cube map slot in the renderer and the shader
  glm::vec3 position;
  float range;
};

class LightSystem : public BaseSystem {
private:
  std::vector<Entity> m_lights;
  std::vector<PointShadowCaster> m_pointShadows;

public:
  LightSystem() = default;
  ~LightSystem() = default;

  void createLight(Entity entity);
  void destroyLight(Entity entity);
  const std::vector<Entity> &getLights() const;
  // Picks the point and spot lights with castShadows (in light order, up to `maxCasters`) that render a cube shadow
  // map this frame; with `enabled` false, none do
  const std::vector<PointShadowCaster> &updatePointShadows(ComponentManager &componentManager, bool enabled,
                                                           int maxCasters);
  void uploadLightsToShader(Shader &shader, ComponentManager &componentManager);

  // A light's position in the world: its offset moved by the entity's Transform, if it has one
  static glm::vec3 worldPosition(const Light &light, const Transform *transform);
};
