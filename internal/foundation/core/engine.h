#pragma once
#include "components/lightComponent.h"
#include "components/transformComponent.h"
#include "foundation/core/app.h"
#include "foundation/core/config.h"
#include "foundation/ecs/componentManager.h"
#include "foundation/ecs/entityManager.h"
#include "foundation/ecs/systemManager.h"
#include "systems/inputSystem.h"


class Engine {
private:
  float m_screenWidth;
  float m_screenHeight;

  EntityManager entityManager;
  ComponentManager componentManager;
  SystemManager systemManager;

  App *m_app = nullptr;

  void registerSystems();
  bool loadResources();

  void loop(bool &running);
  void update(bool &running);
  void render();

public:
  Engine();
  ~Engine();

  bool init();
  void run(App &app);

  SystemManager &getSystemManager();
  ComponentManager &getComponentManager();
  EntityManager &getEntityManager();

  // --- Entity creation ---
  Entity createEntity();

  // --- Generic component access (for user-defined components) ---
  template <typename T, typename... Args> T &addComponent(Entity entity, Args &&...args) {
    return componentManager.add<T>(entity, std::forward<Args>(args)...);
  }

  template <typename T> T &getComponent(Entity entity) { return componentManager.get<T>(entity); }

  template <typename T> T *tryGetComponent(Entity entity) { return componentManager.tryGet<T>(entity); }

  template <typename T> bool hasComponent(Entity entity) { return componentManager.has<T>(entity); }

  template <typename T> void removeComponent(Entity entity) { componentManager.remove<T>(entity); }

  // --- High-level helpers ---
  void createCameraEntity(glm::vec3 position, float yaw = 0.0f, float pitch = 0.0f, float fov = 90.0f);
  Entity createModelEntity(const std::string &name, const std::string &modelPath, glm::vec3 position,
                           glm::vec3 rotation, glm::vec3 scale);
  void createLightEntity(const std::string &name, glm::vec3 position, glm::vec3 direction, glm::vec3 color,
                         LightType type, float intensity, float cutOff, float outerCutOff);

  void setState(Toggle toggle, bool value);
};
