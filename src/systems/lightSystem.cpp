// Light system implementation: the scene's lights, and the choice of the ones a view lights with and casts shadows from.

#include "systems/lightSystem.h"
#include "components/light.h"
#include "components/transform.h"
#include "systems/transformSystem.h"
#include "foundation/core/logger.h"
#include <algorithm>

namespace {
struct Candidate {
  Entity entity;
  const Light *light;
  glm::vec3 position;
  glm::vec3 direction; // world
  float range;
  float importance;
  int slot = -1;
};

float brightnessOf(const Light &light) {
  return std::max(light.color.x, std::max(light.color.y, light.color.z)) * light.intensity;
}

GpuLight pack(const Light &light, const glm::vec3 &position, const glm::vec3 &direction, float range, int slot) {
  GpuLight gpu;
  gpu.data[0] = glm::vec4(position, range);
  gpu.data[1] = glm::vec4(light.color, light.intensity);
  gpu.data[2] = glm::vec4(direction, 0.0f);
  gpu.data[3] = glm::vec4(0.0f, 0.0f, 0.0f, static_cast<float>(light.type));
  gpu.data[4] = glm::vec4(light.cutOff, light.outerCutOff, light.shadowRange, static_cast<float>(slot));
  return gpu;
}

bool intersectsSphere(const glm::vec4 *planes, const glm::vec3 &center, float radius) {
  if (!planes)
    return true;
  for (int i = 0; i < 6; ++i)
    if (glm::dot(glm::vec3(planes[i]), center) + planes[i].w < -radius)
      return false;
  return true;
}
} // namespace

void LightSystem::createLight(Entity entity) { m_lights.push_back(entity); }

void LightSystem::rebuild(ComponentManager &componentManager) {
  m_lights.clear();
  componentManager.forEachComponent<Light>([&](Entity entity, Light &) { m_lights.push_back(entity); });
  std::sort(m_lights.begin(), m_lights.end()); // creation order, as they were added
}

void LightSystem::destroyLight(Entity entity) {
  auto it = std::remove(m_lights.begin(), m_lights.end(), entity);
  m_lights.erase(it, m_lights.end());
}

const std::vector<Entity> &LightSystem::getLights() const { return m_lights; }

namespace {
// The Transform's rotation alone (no scale), built as the model matrix builds it
glm::mat3 rotationOf(const Transform &transform) {
  Transform rotation;
  rotation.rotation = transform.rotation;
  return glm::mat3(TransformSystem::calculateModelMatrix(rotation));
}
} // namespace

glm::vec3 LightSystem::worldPosition(const Light &light, const Transform *transform) {
  return transform ? transform->position + rotationOf(*transform) * light.position : light.position;
}

glm::vec3 LightSystem::worldDirection(const Light &light, const Transform *transform) {
  return transform ? rotationOf(*transform) * light.direction : light.direction;
}

void LightSystem::collectLights(ComponentManager &componentManager, bool shadowsEnabled, int maxCasters,
                                const glm::vec3 &viewPos, const glm::vec4 *frustumPlanes) {
  m_directional.clear();
  m_clustered.clear();
  m_pointShadows.clear();
  maxCasters = std::min(maxCasters, static_cast<int>(EngineConfig::MAX_POINT_SHADOWS));

  static bool warnedDirectional = false, warnedLights = false, warnedShadows = false; // once per run, not every frame
  std::vector<Candidate> candidates;
  candidates.reserve(m_lights.size());
  for (Entity entity : m_lights) {
    const Light *light = componentManager.tryGet<Light>(entity);
    if (!light)
      continue;
    if (light->type == LightType::Directional) {
      if (static_cast<int>(m_directional.size()) < EngineConfig::MAX_DIRECTIONAL_LIGHTS)
        m_directional.push_back(pack(*light, glm::vec3(0.0f),
                                     worldDirection(*light, componentManager.tryGet<Transform>(entity)), 0.0f, -1));
      else if (!warnedDirectional) {
        warnedDirectional = true;
        LOG_W("[LightSystem] More than %d directional lights; the others are ignored",
              EngineConfig::MAX_DIRECTIONAL_LIGHTS);
      }
      continue;
    }
    const float brightness = brightnessOf(*light);
    const float range = light->range;
    if (range <= 0.0f || brightness <= 0.0f)
      continue; // reaches nowhere, or gives no light
    const Transform *transform = componentManager.tryGet<Transform>(entity);
    const glm::vec3 position = worldPosition(*light, transform);
    if (!intersectsSphere(frustumPlanes, position, range))
      continue;
    const glm::vec3 toLight = position - viewPos;
    candidates.push_back({entity, light, position, worldDirection(*light, transform), range, brightness / std::max(glm::dot(toLight, toLight), 1.0f)});
  }
  m_candidates = static_cast<int>(candidates.size());

  // Most important first, so that the cap (and a full index list) drop the least important
  std::stable_sort(candidates.begin(), candidates.end(),
                   [](const Candidate &a, const Candidate &b) { return a.importance > b.importance; });
  const size_t cap = static_cast<size_t>(lightingLimits().maxLights);
  if (candidates.size() > cap) {
    if (!warnedLights) {
      warnedLights = true;
      LOG_W("[LightSystem] %zu point and spot lights reach the view; the %zu most important are used", candidates.size(),
            cap);
    }
    candidates.resize(cap);
  }

  // Shadows: the best of the shadowed lights among those used. A light that has a slot is favored (see the header).
  if (shadowsEnabled && maxCasters > 0) {
    std::vector<Candidate *> wanting;
    for (Candidate &candidate : candidates) {
      const Light &light = *candidate.light;
      if (!light.castShadows || light.shadowRange <= 0.0f)
        continue;
      if (!intersectsSphere(frustumPlanes, candidate.position, std::min(candidate.range, light.shadowRange)))
        continue;
      wanting.push_back(&candidate);
    }
    auto score = [&](const Candidate *candidate) {
      const bool has = std::find(std::begin(m_slotOwners), std::end(m_slotOwners), candidate->entity) !=
                       std::end(m_slotOwners);
      return candidate->importance * (has ? 1.5f : 1.0f);
    };
    std::stable_sort(wanting.begin(), wanting.end(),
                     [&](const Candidate *a, const Candidate *b) { return score(a) > score(b); });
    if (static_cast<int>(wanting.size()) > maxCasters) {
      if (!warnedShadows) {
        warnedShadows = true;
        LOG_W("[LightSystem] More than %d lights cast shadows; the most important ones at the time get them, the others "
              "light without",
              maxCasters);
      }
      wanting.resize(static_cast<size_t>(maxCasters));
    }
    // Slots: a chosen light keeps its own; the others take the free ones
    for (int slot = 0; slot < maxCasters; ++slot) {
      const bool kept = std::any_of(wanting.begin(), wanting.end(),
                                    [&](const Candidate *candidate) { return candidate->entity == m_slotOwners[slot]; });
      if (!kept)
        m_slotOwners[slot] = -1;
    }
    for (int slot = static_cast<int>(maxCasters); slot < EngineConfig::MAX_POINT_SHADOWS; ++slot)
      m_slotOwners[slot] = -1;
    for (Candidate *candidate : wanting) {
      int slot = -1;
      for (int i = 0; i < maxCasters; ++i)
        if (m_slotOwners[i] == candidate->entity)
          slot = i;
      if (slot < 0)
        for (int i = 0; i < maxCasters && slot < 0; ++i)
          if (m_slotOwners[i] == -1)
            slot = i;
      m_slotOwners[slot] = candidate->entity;
      candidate->slot = slot;
      m_pointShadows.push_back({candidate->entity, slot, candidate->position, candidate->light->shadowRange});
    }
  } else {
    for (Entity &owner : m_slotOwners)
      owner = -1;
  }

  m_clustered.reserve(candidates.size());
  for (const Candidate &candidate : candidates)
    m_clustered.push_back(pack(*candidate.light, candidate.position, candidate.direction, candidate.range, candidate.slot));
}
