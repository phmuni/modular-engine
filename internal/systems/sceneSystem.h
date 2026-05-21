#pragma once
// Scene system for managing entities in the scene, including creation/destruction and utility functions.

#include "components/light.h"
#include "foundation/ecs/componentManager.h"
#include "foundation/ecs/entityManager.h"
#include "foundation/ecs/systemManager.h"

class SceneSystem : public BaseSystem {
private:
  EntityManager &m_entityManager;
  ComponentManager &m_componentManager;
  SystemManager &m_systemManager;

public:
  SceneSystem(EntityManager &em, ComponentManager &cm, SystemManager &sm)
      : m_entityManager(em), m_componentManager(cm), m_systemManager(sm) {}

  void destroyEntity(Entity entity);
  void createCameraEntity(const std::string name, glm::vec3 position, float yaw, float pitch, float fov,
                          bool isActive = false, bool isRelative = false);
  Entity createModelEntity(const std::string name, const std::string modelPath, uint32_t shaderHandle,
                           glm::vec3 position, glm::vec3 rotation, glm::vec3 scale);
  void createLightEntity(const std::string name, glm::vec3 position, glm::vec3 direction, glm::vec3 color,
                         LightType type, float intensity, float cutOff, float outerCutOff);
};