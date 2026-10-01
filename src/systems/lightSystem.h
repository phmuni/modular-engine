#pragma once
// Light component and system for managing scene lights and uploading to shaders.

#include "foundation/ecs/componentManager.h"
#include "foundation/ecs/entityManager.h"
#include "foundation/ecs/systemManager.h"
#include "rendering/resources/shader.h"

class LightSystem : public BaseSystem {
private:
  std::vector<Entity> m_lights;

public:
  LightSystem() = default;
  ~LightSystem() = default;

  void createLight(Entity entity);
  void destroyLight(Entity entity);
  const std::vector<Entity> &getLights() const;
  void uploadLightsToShader(Shader &shader, ComponentManager &componentManager);
};
