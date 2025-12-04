#pragma once

#include "foundation/ecs/componentManager.h"
#include "foundation/ecs/systemManager.h"
#include "rendering/resources/shader.h"
#include <vector>

// Manages light entities and their properties.
// Supports three light types: Directional (global), Point (radial), Spot (cone with softness).
// Uploads light data to fragment shader using uniform arrays for per-fragment calculations.
class LightSystem : public BaseSystem {
public:
  void addLight(Entity entity);
  void removeLight(Entity entity);

  const std::vector<Entity> &getLights() const;

  void uploadLightsToShader(Shader &shader, ComponentManager &componentManager);

private:
  std::vector<Entity> m_lights;
};
