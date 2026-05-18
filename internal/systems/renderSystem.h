#pragma once
// Render system for managing renderable entities and issuing draw calls.

#include "foundation/ecs/componentManager.h"
#include "foundation/ecs/entityManager.h"
#include "foundation/ecs/systemManager.h"
#include "rendering/renderer.h"
#include <unordered_map>
#include <unordered_set>

class ResourceSystem;

class RenderSystem : public BaseSystem {
public:
  void insertRenderable(Entity entity);
  void removeRenderable(Entity entity);
  void renderPipeline(SystemManager &systemManager, EntityManager &entityManager, ComponentManager &componentManager);
  Renderer &getRenderer();
  const std::vector<Entity> &getRenderQueue() const;
  void setShadowShaderHandle(uint32_t handle);
  void markBatchesDirty();

private:
  using RenderBatch = std::vector<std::pair<Entity, size_t>>;

  std::vector<Entity> m_entries;
  std::unordered_set<Entity> m_entrySet;
  std::unordered_map<uint32_t, RenderBatch> m_renderBatches;
  bool m_batchesDirty = true;
  uint32_t m_shadowShaderHandle = 0;
  Renderer m_renderer;
  void setupLights();
  void rebuildRenderBatches(ComponentManager &componentManager, ResourceSystem &resourceSystem);
};
