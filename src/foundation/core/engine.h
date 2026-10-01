#pragma once
// Main engine class: initialization, game loop, and entity creation interface.

#include "components/light.h"
#include "components/transform.h"
#include "foundation/core/app.h"
#include "foundation/ecs/componentManager.h"
#include "foundation/ecs/entityManager.h"
#include "foundation/ecs/systemManager.h"
#include "systems/resourceSystem.h"
#include "systems/stateSystem.h"
#include <functional>
#include <glm/glm.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

class Engine {
public:
  class EntityBuilder {
  public:
    EntityBuilder(Engine &engine, std::string_view name);

    EntityBuilder &withModel(std::string_view modelPath, glm::vec3 position, glm::vec3 rotation, glm::vec3 scale,
                             uint32_t shaderHandle = 0);
    EntityBuilder &withCollision(const glm::vec3 &scale, bool isStatic = false);

    template <typename T, typename... Args> EntityBuilder &withComponent(Args &&...args) {
      auto values = std::make_tuple(std::forward<Args>(args)...);
      m_actions.emplace_back([&engine = m_engine, values = std::move(values)](Entity entity) mutable {
        std::apply(
            [&](auto &&...unpacked) { engine.addComponent<T>(entity, std::forward<decltype(unpacked)>(unpacked)...); },
            values);
      });
      return *this;
    }

    EntityBuilder &withTransform(glm::vec3 position, glm::vec3 rotation, glm::vec3 scale) {
      return withComponent<Transform>(position, rotation, scale);
    }

    EntityBuilder &withEmission(const glm::vec3 &color, float strength);
    EntityBuilder &withSolidColor(const glm::vec3 &color);
    Entity build();

  private:
    struct ModelConfig {
      std::string path;
      glm::vec3 position;
      glm::vec3 rotation;
      glm::vec3 scale;
      uint32_t shaderHandle = 0;
    };

    Engine &m_engine;
    std::string m_name;
    std::optional<ModelConfig> m_model;
    std::vector<std::function<void(Entity)>> m_actions;
    std::optional<Entity> m_entity;
  };

  class EntityFactory {
  public:
    explicit EntityFactory(Engine &engine);
    EntityBuilder create(std::string_view name);

  private:
    Engine &m_engine;
  };

  Engine();
  ~Engine();

  void shutdown();

  bool initialize();
  void run(App &app);

  SystemManager &getSystemManager();
  ComponentManager &getComponentManager();
  EntityManager &getEntityManager();

  Entity createEntity();
  EntityFactory entities();

  // Component access
  template <typename T> T &getOrThrow(Entity entity) { return m_componentManager.getOrThrow<T>(entity); }

  template <typename T> T *getOrNil(Entity entity) { return m_componentManager.getOrNil<T>(entity); }

  template <typename T> bool containsComponent(Entity entity) {
    return m_componentManager.containsComponent<T>(entity);
  }

  template <typename T> void removeComponent(Entity entity) { m_componentManager.removeComponent<T>(entity); }

  // allow entity builder to add components without exposing this publicly
  friend class EntityBuilder;

  // Entity creation utilities
  void createCameraEntity(std::string_view name, glm::vec3 position, float yaw = 0.0f, float pitch = 0.0f,
                          float fov = 90.0f, bool isActive = true);

  Entity createModelEntity(std::string_view name, std::string_view modelPath, glm::vec3 position, glm::vec3 rotation,
                           glm::vec3 scale, uint32_t shaderHandle = 0);

  Entity createLightEntity(std::string_view name, glm::vec3 position, glm::vec3 direction, glm::vec3 color,
                           LightType type, float intensity, float cutOff, float outerCutOff);

  void setState(Toggle toggle, bool value);

  // Material utilities
  void setEmission(Entity entity, int submesh, glm::vec3 color, float strength);
  void setEmission(Entity entity, glm::vec3 color, float strength);
  void setTexture(Entity entity, int submesh, TextureSlot slot, std::string_view path);
  void setTexture(Entity entity, TextureSlot slot, std::string_view path);
  void setShininess(Entity entity, int submesh, float shininess);
  void setShininess(Entity entity, float shininess);
  void setSolidColor(Entity entity, int submesh, glm::vec3 color);
  void setSolidColor(Entity entity, glm::vec3 color);

private:
  float m_screenWidth;
  float m_screenHeight;
  uint32_t m_baseShaderHandle = 0;

  // Component access (private): builders and internals should use this.
  template <typename T, typename... Args> T &addComponent(Entity entity, Args &&...args) {
    return m_componentManager.addInPlace<T>(entity, std::forward<Args>(args)...);
  }

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
};
