#pragma once
#include "foundation/ecs/componentManager.h"
#include "foundation/ecs/systemManager.h"
#include <vector>

// Forward declarations
class Shader;
class ComponentManager;

class LightSystem : public BaseSystem {
private:
  std::vector<Entity> m_lights;

public:
  void createLight(Entity entity);
  void destroyLight(Entity entity);
  const std::vector<Entity> &getLights() const;
  void uploadLightsToShader(Shader &shader, ComponentManager &componentManager);
};
