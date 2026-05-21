#pragma once
// Main engine class: initialization, game loop, and entity creation interface.

#include "components/light.h"
#include "foundation/core/app.h"
#include "foundation/ecs/componentManager.h"
#include "foundation/ecs/entityManager.h"
#include "foundation/ecs/systemManager.h"
#include "systems/resourceSystem.h"
#include "systems/stateSystem.h"
#include <glm/glm.hpp>

class Engine {
private:
  float m_screenWidth;
  float m_screenHeight;
  uint32_t m_baseShaderHandle = 0;

  EntityManager m_entityManager;
  ComponentManager m_componentManager;
  SystemManager m_systemManager;

  App *m_app = nullptr;
  bool m_shutdown = false;

  void registerSystems();
  bool loadResources();

  void loop(bool &isRunning);
  void update(bool &isRunning);
  void render();

public:
  Engine();
  ~Engine();

  void shutdown();

  bool initialize();
  void run(App &app);

  SystemManager &getSystemManager();
  ComponentManager &getComponentManager();
  EntityManager &getEntityManager();

  Entity createEntity();

  // Component access
  template <typename T, typename... Args> T &addComponent(Entity entity, Args &&...args) {
    return m_componentManager.addInPlace<T>(entity, std::forward<Args>(args)...);
  }

  template <typename T> T &getOrThrow(Entity entity) { return m_componentManager.getOrThrow<T>(entity); }

  template <typename T> T *getOrNil(Entity entity) { return m_componentManager.getOrNil<T>(entity); }

  template <typename T> bool containsComponent(Entity entity) {
    return m_componentManager.containsComponent<T>(entity);
  }

  template <typename T> void removeComponent(Entity entity) { m_componentManager.removeComponent<T>(entity); }

  // Entity creation utilities
  void createCameraEntity(std::string_view name, glm::vec3 position, float yaw = 0.0f, float pitch = 0.0f,
                          float fov = 90.0f, bool isActive = true);

  Entity createModelEntity(std::string_view name, std::string_view modelPath, glm::vec3 position, glm::vec3 rotation,
                           glm::vec3 scale);

  void createLightEntity(std::string_view name, glm::vec3 position, glm::vec3 direction, glm::vec3 color,
                         LightType type, float intensity, float cutOff, float outerCutOff);

  void setState(Toggle toggle, bool value);

  // Material utilities
  void setEmission(Entity entity, int submesh, glm::vec3 color, float strength);
  void setTexture(Entity entity, int submesh, TextureSlot slot, std::string_view path);
  void setShininess(Entity entity, int submesh, float shininess);
  void setSolidColor(Entity entity, int submesh, glm::vec3 color);
};
