#pragma once
// Render system for managing renderable entities and issuing draw calls.

#include "foundation/ecs/componentManager.h"
#include "foundation/ecs/entityManager.h"
#include "foundation/ecs/systemManager.h"
#include "rendering/renderer.h"
#include <glm/glm.hpp>
#include <string>
#include <unordered_map>

class ResourceSystem;
class TransformSystem;
class LightSystem;
class Shader;
struct PointShadowCaster;
struct Camera;

// What the scene's Skybox says about fog and ambient light, as the shaders take it: colors already in the space the scene
// target is in (linear with gamma-correct lighting)
struct SceneEnvironment {
  int fogMode = 0; // 0 off, 1 linear, 2 exponential
  glm::vec3 fogColor{0.0f};
  float fogStart = 0.0f;
  float fogFinish = 1.0f;
  float fogDensity = 0.0f;
  glm::vec3 ambient{0.0f}; // added to every lit surface, times its color
};

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
  void setSkyboxShader(uint32_t handle) { m_skyboxShader = handle; }
  void markBatchesDirty();
  // The seconds custom surface shaders see as `time`: the game clock (the engine sets it every frame)
  void setShaderTime(float seconds) { m_shaderTime = seconds; }
  // Forgets what was drawn before (batches, point shadows), after the entities were replaced wholesale
  void resetCaches() {
    m_batchesDirty = true;
    for (uint64_t &signature : m_pointShadowSignatures)
      signature = 0;
  }
  // The fog and ambient light of the frame being drawn (the particle pass reads it after the lit pass)
  const SceneEnvironment &getEnvironment() const { return m_environment; }
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
  uint32_t m_seenShaderRevision = 0; // ResourceSystem::shaderRevision() when the batches were last built
  float m_shaderTime = 0.0f;
  int m_culledCount = 0;
  uint32_t m_shadowShaderHandle = 0;
  uint32_t m_tonemapShader = 0;
  uint32_t m_bloomDownsampleShader = 0;
  uint32_t m_bloomUpsampleShader = 0;
  uint32_t m_compositeShader = 0;
  Renderer m_renderer;
  // The first Skybox component's panorama, behind the opaque geometry
  SceneEnvironment m_environment;
  uint32_t m_skyboxShader = 0;
  std::string m_skyboxPath; // what m_skyboxTexture shows
  GLuint m_skyboxTexture = 0;
  void drawSkybox(ComponentManager &componentManager, ResourceSystem &resourceSystem, const glm::mat4 &view,
                  const glm::mat4 &projection, bool gammaCorrect);

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
    const std::vector<glm::mat4> *joints = nullptr; // skinned: its pose this frame, drawn on its own
  };
  // Skinned models: this frame's joint matrices, set on the shader (useSkinning); false for a static mesh
  class AnimationSystem *m_animation = nullptr;
  bool applySkinning(Shader &shader, Entity entity, const class Mesh &mesh) const;
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
