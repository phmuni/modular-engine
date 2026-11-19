#pragma once
// Render system for managing renderable entities and issuing draw calls.

#include "foundation/ecs/componentManager.h"
#include "foundation/ecs/entityManager.h"
#include "foundation/ecs/systemManager.h"
#include "rendering/renderer.h"
#include <unordered_map>

class ResourceSystem;
class TransformSystem;
class LightSystem;
class Shader;
struct PointShadowCaster;
struct Camera;

class RenderSystem : public BaseSystem {
public:
  void renderPipeline(SystemManager &systemManager, ComponentManager &componentManager);
  Renderer &getRenderer();
  void setShadowShaderHandle(uint32_t handle);
  void setPostProcessShaders(uint32_t tonemap, uint32_t bloomDownsample, uint32_t bloomUpsample, uint32_t composite) {
    m_tonemapShader = tonemap;
    m_bloomDownsampleShader = bloomDownsample;
    m_bloomUpsampleShader = bloomUpsample;
    m_compositeShader = composite;
  }
  void markBatchesDirty();
  // Models skipped by frustum culling in the last rendered frame
  int getCulledCount() const { return m_culledCount; }
  // Point lights with a cube shadow map, and how many of them had to render it again in the last frame
  int getPointShadowCount() const { return m_pointShadowCount; }
  int getPointShadowUpdates() const { return m_pointShadowUpdates; }

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
  uint32_t m_tonemapShader = 0;
  uint32_t m_bloomDownsampleShader = 0;
  uint32_t m_bloomUpsampleShader = 0;
  uint32_t m_compositeShader = 0;
  Renderer m_renderer;
  void rebuildRenderBatches(ComponentManager &componentManager, ResourceSystem &resourceSystem);

  // What the main pass needs from the sun's shadow cascades
  struct DirectionalShadow {
    bool active = false;
    glm::mat4 matrices[EngineConfig::SHADOW_CASCADES]; // world to each cascade's light space
    float splits[EngineConfig::SHADOW_CASCADES] = {};  // where each cascade ends, in distance along the view
    float texels[EngineConfig::SHADOW_CASCADES] = {};  // the world size of one shadow texel in each cascade
  };
  // The first directional light's shadow, in cascades fitted to the camera's view out to SHADOW_DISTANCE
  DirectionalShadow renderDirectionalShadow(ComponentManager &componentManager, ResourceSystem &resourceSystem,
                                            TransformSystem &transformSystem, LightSystem &lightSystem,
                                            const Camera &camera, const glm::mat4 &view);
  // A depth cube map per caster: six 90-degree views, one per face
  void renderPointShadows(ComponentManager &componentManager, ResourceSystem &resourceSystem,
                          TransformSystem &transformSystem, const std::vector<PointShadowCaster> &casters);
  // Every model that can cast a shadow this frame, with its matrix and world bounding sphere, sorted by mesh.
  // Gathered once and reused by each shadow pass (3 sun cascades, 6 faces per point light).
  struct ShadowCaster {
    uint32_t meshHandle;
    glm::mat4 model;
    glm::vec3 center;
    float radius;
  };
  std::vector<ShadowCaster> m_shadowCasters;
  bool m_shadowCastersGathered = false; // reset every frame; gathered by the first shadow pass that needs them

  // What each point shadow slot's cube map shows (see pointShadowSignature); 0 when it shows nothing yet.
  // A light renders its cube map again only when this changes, so still lights in a still scene cost nothing.
  uint64_t m_pointShadowSignatures[EngineConfig::MAX_POINT_SHADOWS] = {};
  int m_pointShadowCount = 0;
  int m_pointShadowUpdates = 0;
  uint64_t pointShadowSignature(const PointShadowCaster &light) const;
  void gatherShadowCasters(ComponentManager &componentManager, ResourceSystem &resourceSystem,
                           TransformSystem &transformSystem);
  // Draws the gathered casters inside `viewProjection` into the bound depth target with the shadow shader
  void drawShadowCasters(Shader &depthShader, const glm::mat4 &viewProjection, ResourceSystem &resourceSystem);

  // Sorts m_instanceItems into runs of identical keys and calls drawRun(firstItem, instanceCount) for each,
  // with the run's instances already uploaded.
  template <typename SameRun, typename DrawRun> void drawInstanceRuns(SameRun sameRun, DrawRun drawRun);
};
