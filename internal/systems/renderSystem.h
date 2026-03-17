#pragma once
// Render pipeline orchestrator: shadow, opaque, and transparent passes.

#include "foundation/ecs/componentManager.h"
#include "foundation/ecs/entityManager.h"
#include "foundation/ecs/systemManager.h"
#include "rendering/renderer.h"

#include <vector>

class RenderSystem : public BaseSystem {
public:
  void insertRenderable(Entity entity);
  void removeRenderable(Entity entity);
  void renderPipeline(SystemManager &systemManager, EntityManager &entityManager, ComponentManager &componentManager);
  Renderer &getRenderer();
  const std::vector<Entity> &getRenderQueue() const;

private:
  std::vector<Entity> m_entries;
  Renderer m_renderer;
  void setupLights();
};
