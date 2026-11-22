#pragma once
// Render system for managing renderable entities and issuing draw calls.

#include "foundation/ecs/componentManager.h"
#include "foundation/ecs/entityManager.h"
#include "foundation/ecs/systemManager.h"
#include "rendering/clusteredLights.h"
#include "rendering/renderer.h"
#include "rendering/ibl.h"
#include "rendering/reflectionProbes.h"
#include "rendering/ssao.h"
#include <glm/glm.hpp>
#include <optional>
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
  // Image-based lighting (see ibl.h): 0 none, 1 one color from every direction, 2 the cube maps of the sky's image
  int environmentMode = 0;
  glm::vec3 environmentColor{0.0f}; // mode 1: the color; mode 2: the factor the maps are multiplied by
  float environmentRotation = 0.0f; // radians around +Y, the panorama's rotation
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
  // The antialiasing and upscaling filters (assets/shaders/fxaa.frag, fsrEasu.frag, fsrRcas.frag)
  void setAntialiasingShaders(uint32_t fxaa, uint32_t easu, uint32_t rcas) {
    m_fxaaShader = fxaa;
    m_easuShader = easu;
    m_rcasShader = rcas;
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
  // Ambient occlusion (see ssao.h). On by default. The scene's Skybox, when it has one, decides radius and intensity
  // (the level's look); the settings given here are what a scene without a Skybox gets, and setSsaoSettings also
  // writes radius and intensity into the scene's Skybox when it has one.
  void setSsaoEnabled(bool enabled) { m_ssaoEnabled = enabled; }
  bool ssaoEnabled() const { return m_ssaoEnabled; }
  void setSsaoSettings(const SsaoSettings &settings) { m_ssaoSettings = settings.clamped(); }
  const SsaoSettings &ssaoSettings() const { return m_ssaoSettings; }
  // Shows the AO alone instead of the image (an editor tuning aid)
  void setSsaoDebugView(bool enabled) { m_ssaoDebugView = enabled; }
  bool ssaoDebugView() const { return m_ssaoDebugView; }
  void shutdownSsao() { m_ssao.shutdown(); }
  // Clustered lighting (see clusteredLights.h): the light buffers and what the last view put in them. The heat map
  // colors the scene by how many lights each pixel's cluster has (an editor aid).
  void setLightHeatMap(bool enabled) { m_lightHeatMap = enabled; }
  bool lightHeatMap() const { return m_lightHeatMap; }
  const ClusteredLights::Stats &lightStats() const { return m_clusters.stats(); }
  void shutdownLights() { m_clusters.shutdown(); }
  void shutdownIbl() { m_ibl.shutdown(); }
  const Ibl &ibl() const { return m_ibl; }
  // Reflection probes (see reflectionProbes.h): their data, array and per-draw choice
  ReflectionProbes &probes() { return m_probes; }
  const ReflectionProbes &probes() const { return m_probes; }
  void shutdownProbes() { m_probes.shutdown(); }
  // Draws the scene from the probe entity's position into a cube (a face at a time, with the sun's and the lights'
  // shadows, the sky, transparent surfaces; no ambient occlusion, post-processing, particles or other probes) and turns
  // it into the probe's reflections and diffuse light. Does not store the result (ReflectionProbes::setData does).
  // Needs the GL context and may be called between frames; it restores the framebuffer and viewport it found. Empty
  // when the entity is not a probe with a Transform.
  std::optional<ProbeData> captureProbe(SystemManager &systemManager, ComponentManager &componentManager, Entity entity);
  // How long the last capture took, in milliseconds (the faces, the pre-filtering and reading it back)
  double lastCaptureMs() const { return m_lastCaptureMs; }
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
    uint32_t probes; // the reflection probes that light it (ReflectionProbes::select): draws with others cannot share a call
    InstanceData instance;
  };

  // Ambient occlusion state: the depth prepass and AO textures, and what drives them
  Ssao m_ssao;
  SsaoSettings m_ssaoSettings;
  bool m_ssaoEnabled = true;
  bool m_ssaoDebugView = false;

  ClusteredLights m_clusters;
  bool m_lightHeatMap = false;

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
  uint32_t m_fxaaShader = 0;
  uint32_t m_easuShader = 0;
  uint32_t m_rcasShader = 0;
  Renderer m_renderer;
  // The first Skybox component's panorama, behind the opaque geometry
  SceneEnvironment m_environment;
  Ibl m_ibl;
  ReflectionProbes m_probes;
  bool m_capturing = false; // a probe's capture is being drawn
  double m_lastCaptureMs = 0.0;
  uint32_t m_skyboxShader = 0;
  std::string m_skyboxPath; // what m_skyboxTexture shows
  GLuint m_skyboxTexture = 0;
  void drawSkybox(ComponentManager &componentManager, ResourceSystem &resourceSystem, const glm::mat4 &view,
                  const glm::mat4 &projection, bool gammaCorrect);

  void rebuildRenderBatches(ComponentManager &componentManager, ResourceSystem &resourceSystem);

  // A probe's capture in progress (see captureProbe): where from, how big, and what it made
  struct ProbeCapture {
    glm::vec3 position{0.0f};
    int resolution = 128;
    ProbeData result;
    bool ok = false;
  };
  // The whole frame, or with `capture` the six faces of a probe's cube instead of the camera's view
  void drawPipeline(SystemManager &systemManager, ComponentManager &componentManager, ProbeCapture *capture);

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
                                            const Camera &camera, const glm::mat4 &view, float aspect);
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
  // Models with parts whose custom shader reshapes or cuts them (see CustomShader::shadowHandle). Those are drawn by
  // index range: `plain` with the depth shader like any caster, `custom` with the shadow variant of the part's
  // shader and the part's values. Models without such parts stay in m_shadowCasters, drawn as they always were.
  struct ShadowRange {
    uint32_t indexStart;
    uint32_t indexCount;
    uint32_t material;
    const struct CustomShader *shader; // null in `plain`
  };
  struct CustomShadowCaster {
    uint32_t meshHandle;
    glm::mat4 model;
    glm::vec3 center;
    float radius;
    const std::vector<glm::mat4> *joints = nullptr;
    std::vector<ShadowRange> plain;
    std::vector<ShadowRange> custom;
  };
  std::vector<CustomShadowCaster> m_customShadowCasters;
  ResourceSystem *m_resourceSystemForSignature = nullptr; // for the materials' values in pointShadowSignature
  glm::vec3 m_shadowViewPos{0.0f}; // the camera, for `viewDir` in a custom shader's surface() during the shadow passes
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
  void drawCustomShadowCasters(Shader &depthShader, const glm::mat4 &viewProjection, ResourceSystem &resourceSystem);

  // Sorts m_instanceItems into runs of identical keys and calls drawRun(firstItem, instanceCount) for each,
  // with the run's instances already uploaded.
  template <typename SameRun, typename DrawRun> void drawInstanceRuns(SameRun sameRun, DrawRun drawRun);
};
