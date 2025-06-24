#pragma once

#include "component/lightComponent.h" // IWYU pragma: keep
#include "ecs/componentManager.h"
#include "loader/shaderLoader.h"
#include <vector>

class LightSystem {
public:
  void addLight(Entity entity);

  const std::vector<Entity> &getLights() const;

  void uploadLightsToShader(Shader &shader, ComponentManager &componentManager);

private:
  std::vector<Entity> m_lights;
};
