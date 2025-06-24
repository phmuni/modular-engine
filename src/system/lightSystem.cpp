#include "system/lightSystem.h"

void LightSystem::addLight(Entity entity) { m_lights.emplace_back(entity); }

const std::vector<Entity> &LightSystem::getLights() const { return m_lights; }

void LightSystem::uploadLightsToShader(Shader &shader, ComponentManager &componentManager) {
  int index = 0;

  for (const Entity &lightEntity : m_lights) {
    const auto &light = componentManager.get<LightComponent>(lightEntity);

    std::string prefix = "lights[" + std::to_string(index) + "]";

    shader.setInt((prefix + ".type").c_str(), static_cast<int>(light.type));
    shader.setVec3((prefix + ".position").c_str(), light.position);
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
