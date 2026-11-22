#pragma once
// Light component and system for managing scene lights and choosing, each frame, the ones the renderer uses.

#include "foundation/ecs/componentManager.h"
#include "foundation/ecs/entityManager.h"
#include "foundation/ecs/systemManager.h"
#include "components/light.h"
#include "foundation/core/config.h"
#include "rendering/clusteredLights.h"
#include <glm/glm.hpp>
#include <vector>

struct Transform;

// A point or spot light rendering a cube shadow map this frame
struct PointShadowCaster {
  Entity entity;
  int slot; // cube map slot in the renderer and the shader
  glm::vec3 position;
  float range; // Light::shadowRange
};

class LightSystem : public BaseSystem {
private:
  std::vector<Entity> m_lights;
  std::vector<PointShadowCaster> m_pointShadows;
  Entity m_slotOwners[EngineConfig::MAX_POINT_SHADOWS]; // the light each shadow slot was given to, -1 for none
  std::vector<GpuLight> m_directional;
  std::vector<GpuLight> m_clustered;
  int m_candidates = 0; // point and spot lights that reach the view, before the cap

public:
  LightSystem() {
    for (Entity &owner : m_slotOwners)
      owner = -1;
  }
  ~LightSystem() = default;

  void createLight(Entity entity);
  void destroyLight(Entity entity);
  // The lights are the entities with a Light again, after the entities were replaced wholesale
  void rebuild(ComponentManager &componentManager);
  const std::vector<Entity> &getLights() const;

  // Chooses what one view uses, for the lit passes (clusteredLights.h) and the shadow passes:
  //  - directional lights: the first EngineConfig::MAX_DIRECTIONAL_LIGHTS, in creation order
  //  - point and spot lights: those whose sphere of influence (lightRange) reaches the view's frustum (`frustumPlanes`,
  //    6 planes with inside >= 0; null for a view from everywhere, like a probe's capture); when there are more than
  //    lightingLimits().maxLights, the most important: brightness / distance to the camera squared (distance at least 1)
  //  - shadows: of those, the lights with castShadows (up to `maxCasters`, when `shadowsEnabled`), ranked by the same
  //    importance; a light that already has a slot gets 1.5 times its score, so two lights of about the same rank do not
  //    swap slots (and redraw their cubes) with every move of the camera. A shadowed light keeps its slot while chosen.
  void collectLights(ComponentManager &componentManager, bool shadowsEnabled, int maxCasters, const glm::vec3 &viewPos,
                     const glm::vec4 *frustumPlanes);
  // What collectLights chose
  const std::vector<PointShadowCaster> &pointShadows() const { return m_pointShadows; }
  const std::vector<GpuLight> &directionalLights() const { return m_directional; }
  // Point and spot lights for the clusters, most important first
  const std::vector<GpuLight> &clusterLights() const { return m_clustered; }
  // Point and spot lights that reach the view (before the cap) and how many of them the clusters took
  int candidateLightCount() const { return m_candidates; }
  int visibleLightCount() const { return static_cast<int>(m_clustered.size()); }

  // A light's position in the world: its offset moved by the entity's Transform, if it has one
  static glm::vec3 worldPosition(const Light &light, const Transform *transform);
};
