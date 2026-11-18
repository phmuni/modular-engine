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
  void renderPipeline(SystemManager &systemManager, ComponentManager &componentManager);
  Renderer &getRenderer();
  void setShadowShaderHandle(uint32_t handle);
  void markBatchesDirty();
  // Models skipped by frustum culling in the last rendered frame
  int getCulledCount() const { return m_culledCount; }

private:
  // Entities with at least one part whose material uses the batch's shader
  using RenderBatch = std::vector<Entity>;

  // Draws sharing mesh, submesh and material, gathered every frame for instancing.
  // Grouped per frame, not per rebuild, because material handles can change without dirtying the batches.
  struct InstanceItem {
    uint32_t meshHandle;
    uint32_t indexStart; // a range of parts merged by material, see forEachDrawRange
    uint32_t indexCount;
    uint32_t materialHandle;
    InstanceData instance;
  };

  std::unordered_map<uint32_t, RenderBatch> m_renderBatches;
  std::vector<InstanceItem> m_instanceItems;
  std::vector<InstanceData> m_instanceData;
  bool m_batchesDirty = true;
  int m_culledCount = 0;
  uint32_t m_shadowShaderHandle = 0;
  Renderer m_renderer;
  void setupLights();
  void rebuildRenderBatches(ComponentManager &componentManager, ResourceSystem &resourceSystem);

  // Sorts m_instanceItems into runs of identical keys and calls drawRun(firstItem, instanceCount) for each,
  // with the run's instances already uploaded.
  template <typename SameRun, typename DrawRun> void drawInstanceRuns(SameRun sameRun, DrawRun drawRun);
};
