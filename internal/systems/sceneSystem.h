#pragma once
// High-level scene operations: create/destroy camera, model, and light entities.

#include "components/lightComponent.h"
#include "foundation/ecs/componentManager.h"
#include "foundation/ecs/entityManager.h"
#include "foundation/ecs/systemManager.h"
#include "glm/ext/vector_float3.hpp"
#include <string>

class SceneSystem : public BaseSystem {
private:
  EntityManager &entityManager;
  ComponentManager &componentManager;
  SystemManager &systemManager;

public:
  SceneSystem(EntityManager &em, ComponentManager &cm, SystemManager &sm)
      : entityManager(em), componentManager(cm), systemManager(sm) {}

  void destroyEntity(Entity entity);
  void createCameraEntity(glm::vec3 position, float yaw, float pitch, float fov);
  Entity createModelEntity(const std::string name, const std::string &modelPath, glm::vec3 position, glm::vec3 rotation,
                           glm::vec3 scale);
  void createLightEntity(const std::string &name, glm::vec3 position, glm::vec3 direction, glm::vec3 color,
                         LightType type, float intensity, float cutOff, float outerCutOff);
};