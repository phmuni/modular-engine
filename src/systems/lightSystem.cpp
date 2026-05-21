
// Light system implementation for managing light entities and uploading their data to shaders.

#include "systems/lightSystem.h"
#include "components/light.h"
#include "components/transform.h"
#include "foundation/core/logger.h"

void LightSystem::createLight(Entity entity) { m_lights.push_back(entity); }

void LightSystem::destroyLight(Entity entity) {
  auto it = std::remove(m_lights.begin(), m_lights.end(), entity);
  m_lights.erase(it, m_lights.end());
}

const std::vector<Entity> &LightSystem::getLights() const { return m_lights; }

void LightSystem::uploadLightsToShader(Shader &shader, ComponentManager &componentManager) {
  int index = 0;
  constexpr int kMaxLights = 10;

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

    std::string prefix = "lights[" + std::to_string(index) + "]";

    shader.setInt((prefix + ".type").c_str(), static_cast<int>(light.type));
    shader.setVec3((prefix + ".position").c_str(), worldPos);
    shader.setVec3((prefix + ".direction").c_str(), light.direction);
    shader.setVec3((prefix + ".color").c_str(), light.color);
    shader.setFloat((prefix + ".intensity").c_str(), light.intensity);
    shader.setFloat((prefix + ".ambient").c_str(), light.ambient);
    shader.setFloat((prefix + ".constant").c_str(), light.constant);
    shader.setFloat((prefix + ".linear").c_str(), light.linear);
    shader.setFloat((prefix + ".quadratic").c_str(), light.quadratic);
    shader.setFloat((prefix + ".cutOff").c_str(), light.cutOff);
    shader.setFloat((prefix + ".outerCutOff").c_str(), light.outerCutOff);

    index++;
  }

  shader.setInt("numLights", index);
}
