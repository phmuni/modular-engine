
// Light system implementation for managing light entities and uploading their data to shaders.

#include "systems/lightSystem.h"
#include "components/light.h"
#include "components/transform.h"
#include "foundation/core/logger.h"
#include <algorithm>
#include <array>
#include <string>

namespace {
constexpr int kMaxLights = 10;

struct LightUniformNames {
  std::string type, position, direction, color, intensity, ambient, constant, linear, quadratic, cutOff, outerCutOff,
      shadowSlot, shadowFar;
};

// Built once: formatting "lights[i].field" every frame allocated a string per field, light and shader.
const std::array<LightUniformNames, kMaxLights> &lightUniformNames() {
  static const auto names = [] {
    std::array<LightUniformNames, kMaxLights> result;
    for (int i = 0; i < kMaxLights; ++i) {
      std::string prefix = "lights[" + std::to_string(i) + "].";
      result[i] = {prefix + "type",      prefix + "position",  prefix + "direction", prefix + "color",
                   prefix + "intensity", prefix + "ambient",   prefix + "constant",  prefix + "linear",
                   prefix + "quadratic", prefix + "cutOff",    prefix + "outerCutOff", prefix + "shadowSlot",
                   prefix + "shadowFar"};
    }
    return result;
  }();
  return names;
}
} // namespace

void LightSystem::createLight(Entity entity) { m_lights.push_back(entity); }

void LightSystem::destroyLight(Entity entity) {
  auto it = std::remove(m_lights.begin(), m_lights.end(), entity);
  m_lights.erase(it, m_lights.end());
}

const std::vector<Entity> &LightSystem::getLights() const { return m_lights; }

glm::vec3 LightSystem::worldPosition(const Light &light, const Transform *transform) {
  return transform ? light.position + transform->position : light.position;
}

const std::vector<PointShadowCaster> &LightSystem::updatePointShadows(ComponentManager &componentManager, bool enabled,
                                                                     int maxCasters) {
  m_pointShadows.clear();
  if (!enabled)
    return m_pointShadows;

  // Only lights the shader sees (the first kMaxLights) can use a shadow
  const size_t visible = std::min(m_lights.size(), static_cast<size_t>(kMaxLights));
  static bool warned = false; // once per run, not every frame
  for (size_t i = 0; i < visible; ++i) {
    const Light *light = componentManager.tryGet<Light>(m_lights[i]);
    if (!light || !light->castShadows || light->type == LightType::Directional || light->shadowRange <= 0.0f)
      continue;
    if (static_cast<int>(m_pointShadows.size()) >= maxCasters) {
      if (!warned)
        LOG_W("[LightSystem] More than %d lights cast shadows; the others light without them", maxCasters);
      warned = true;
      continue;
    }
    m_pointShadows.push_back({m_lights[i], static_cast<int>(m_pointShadows.size()),
                              worldPosition(*light, componentManager.tryGet<Transform>(m_lights[i])),
                              light->shadowRange});
  }
  return m_pointShadows;
}

void LightSystem::uploadLightsToShader(Shader &shader, ComponentManager &componentManager) {
  int index = 0;
  const auto &allNames = lightUniformNames();

  for (const Entity &lightEntity : m_lights) {
    if (index >= kMaxLights) {
      LOG_W("[LightSystem] Too many lights; max supported is %d - skipping remaining.", kMaxLights);
      break;
    }

    const auto &light = componentManager.get<Light>(lightEntity);

    const glm::vec3 worldPos = worldPosition(light, componentManager.tryGet<Transform>(lightEntity));
    int shadowSlot = -1;
    for (const PointShadowCaster &caster : m_pointShadows)
      if (caster.entity == lightEntity)
        shadowSlot = caster.slot;

    const auto &names = allNames[index];
    shader.setInt(names.type, static_cast<int>(light.type));
    shader.setVec3(names.position, worldPos);
    shader.setVec3(names.direction, light.direction);
    shader.setVec3(names.color, light.color);
    shader.setFloat(names.intensity, light.intensity);
    shader.setFloat(names.ambient, light.ambient);
    shader.setFloat(names.constant, light.constant);
    shader.setFloat(names.linear, light.linear);
    shader.setFloat(names.quadratic, light.quadratic);
    shader.setFloat(names.cutOff, light.cutOff);
    shader.setFloat(names.outerCutOff, light.outerCutOff);
    shader.setInt(names.shadowSlot, shadowSlot);
    shader.setFloat(names.shadowFar, light.shadowRange);

    index++;
  }

  shader.setInt("numLights", index);
}
