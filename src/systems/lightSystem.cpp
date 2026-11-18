
// Light system implementation for managing light entities and uploading their data to shaders.

#include "systems/lightSystem.h"
#include "components/light.h"
#include "components/transform.h"
#include "foundation/core/logger.h"
#include <array>
#include <string>

namespace {
constexpr int kMaxLights = 10;

struct LightUniformNames {
  std::string type, position, direction, color, intensity, ambient, constant, linear, quadratic, cutOff, outerCutOff;
};

// Built once: formatting "lights[i].field" every frame allocated 11 strings per light per shader.
const std::array<LightUniformNames, kMaxLights> &lightUniformNames() {
  static const auto names = [] {
    std::array<LightUniformNames, kMaxLights> result;
    for (int i = 0; i < kMaxLights; ++i) {
      std::string prefix = "lights[" + std::to_string(i) + "].";
      result[i] = {prefix + "type",      prefix + "position",  prefix + "direction", prefix + "color",
                   prefix + "intensity", prefix + "ambient",   prefix + "constant",  prefix + "linear",
                   prefix + "quadratic", prefix + "cutOff",    prefix + "outerCutOff"};
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

void LightSystem::uploadLightsToShader(Shader &shader, ComponentManager &componentManager) {
  int index = 0;
  const auto &allNames = lightUniformNames();

  for (const Entity &lightEntity : m_lights) {
    if (index >= kMaxLights) {
      LOG_W("[LightSystem] Too many lights; max supported is %d - skipping remaining.", kMaxLights);
      break;
    }

    const auto &light = componentManager.getOrThrow<Light>(lightEntity);

    glm::vec3 worldPos = light.position;
    auto *transform = componentManager.getOrNil<Transform>(lightEntity);
    if (transform)
      worldPos += transform->position;

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

    index++;
  }

  shader.setInt("numLights", index);
}
