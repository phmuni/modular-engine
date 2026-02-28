#pragma once
#include "components/lightComponent.h"
#include "foundation/ecs/componentManager.h"
#include "foundation/ecs/entityManager.h"
#include "foundation/ecs/systemManager.h"
#include <glm/glm.hpp>
#include <string>

class Engine {
private:
  float m_screenWidth;
  float m_screenHeight;

  EntityManager entityManager;
  ComponentManager componentManager;
  SystemManager systemManager;

  void registerSystems();
  bool loadResources();

  void loop(bool &running);
  void update(bool &running);
  void render();

public:
  Engine();
  ~Engine();

  bool init();
  void run();

  void createCameraEntity(glm::vec3 position, float yaw = 0.0f, float pitch = 0.0f, float fov = 90.0f);
  void createModelEntity(const std::string &name, const std::string &modelPath, glm::vec3 position, glm::vec3 rotation,
                         glm::vec3 scale);
  void createLightEntity(const std::string &name, glm::vec3 position, glm::vec3 direction, glm::vec3 color,
                         LightType type, float intensity, float cutOff, float outerCutOff);
};
