#pragma once

#include "core/renderer.h"
#include "ecs/componentManager.h"
#include "ecs/entityManager.h"
#include "ecs/systemManager.h"

#include <vector>

// Manages rendering pipeline: shadow pass + main render pass.
// Implements Percentage Closer Filtering (PCF) 5x5 for soft shadow edges.
// Normal matrix computed on CPU (transpose(inverse(model))) to optimize vertex shader.
class RenderSystem : public BaseSystem {
public:
  struct RenderQueue {
    RenderQueue(Entity entity) : entity(entity) {}
    Entity entity;
  };

  void addRenderable(Entity entity);
  void removeRenderable(Entity entity);

  void renderCall(SystemManager &systemManager, EntityManager &entityManager, ComponentManager &componentManager);

  Renderer &getRenderer();
  const std::vector<RenderQueue> &getRenderQueue() const;

private:
  std::vector<RenderQueue> m_entries;
  Renderer m_renderer;

  void setupLights();
};
