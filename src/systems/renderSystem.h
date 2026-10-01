#pragma once
// Render system for managing renderable entities and issuing draw calls.

#include "foundation/ecs/componentManager.h"
#include "foundation/ecs/entityManager.h"
#include "foundation/ecs/systemManager.h"
#include "rendering/renderer.h"
#include <unordered_map>

class ResourceSystem;

class RenderSystem : public BaseSystem {
public:
  void renderPipeline(SystemManager &systemManager, EntityManager &entityManager, ComponentManager &componentManager);
  Renderer &getRenderer();
  void setShadowShaderHandle(uint32_t handle);
  void markBatchesDirty();

private:
  using RenderBatch = std::vector<std::pair<Entity, size_t>>;

  std::unordered_map<uint32_t, RenderBatch> m_renderBatches;
  bool m_batchesDirty = true;
  uint32_t m_shadowShaderHandle = 0;
  Renderer m_renderer;
  void setupLights();
  void rebuildRenderBatches(ComponentManager &componentManager, ResourceSystem &resourceSystem);
};
