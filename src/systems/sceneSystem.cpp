#include "systems/sceneSystem.h"
#include "components/cameraComponent.h"
#include "components/lightComponent.h"
#include "components/modelComponent.h"
#include "components/nameComponent.h"
#include "components/transformComponent.h"
#include "rendering/resources/material.h"
#include "rendering/resources/mesh.h"
#include "systems/cameraSystem.h"
#include "systems/lightSystem.h"
#include "systems/renderSystem.h"
#include <memory>
#include <utility>

void SceneSystem::removeEntity(Entity entity) {
  auto &renderSystem = systemManager.getSystem<RenderSystem>();
  renderSystem.removeRenderable(entity);

  if (componentManager.has<LightComponent>(entity)) {
    auto &lightSystem = systemManager.getSystem<LightSystem>();
    lightSystem.removeLight(entity);

  } else if (componentManager.has<CameraComponent>(entity)) {
    auto &cameraSystem = systemManager.getSystem<CameraSystem>();
    cameraSystem.removeActiveCamera();
  }

  componentManager.removeAll(entity);
  entityManager.deleteEntity(entity);
};

void SceneSystem::createEntityCamera(glm::vec3 position, float yaw, float pitch, float fov) {
  auto &cameraSystem = systemManager.getSystem<CameraSystem>();
  Entity newCamera = entityManager.createEntity();

  auto cameraComponent = std::make_unique<CameraComponent>();
  cameraComponent->position = position;
  cameraComponent->yaw = yaw;
  cameraComponent->pitch = pitch;
  cameraComponent->fov = fov;

  cameraSystem.updateFront(*cameraComponent);

  componentManager.add<CameraComponent>(newCamera, std::move(cameraComponent));
  cameraSystem.setActiveCamera(newCamera);
}

void SceneSystem::createEntityModel(const std::string name, const std::string &modelPath,
                                    const std::string &texturePath, glm::vec3 position, glm::vec3 rotation,
                                    glm::vec3 scale) {
  Entity entity = entityManager.createEntity();

  auto material = std::make_unique<Material>(texturePath, 32.0f);
  if (!material) {
    SDL_Log("Failed to load material from: %s", texturePath.c_str());
    return;
  }

  auto mesh = std::make_unique<Mesh>(modelPath);
  if (!mesh) {
    SDL_Log("Failed to load mesh from: %s", modelPath.c_str());
    return;
  }

  componentManager.add<NameComponent>(entity, std::make_unique<NameComponent>(name));
  componentManager.add<TransformComponent>(entity, std::make_unique<TransformComponent>(position, rotation, scale));
  componentManager.add<ModelComponent>(entity, std::make_unique<ModelComponent>(std::move(mesh), std::move(material)));

  systemManager.getSystem<RenderSystem>().addRenderable(entity);
}

void SceneSystem::createEntityLight(const std::string &name, glm::vec3 position, glm::vec3 direction, glm::vec3 color,
                                    LightType type, float intensity, float cutOff, float outerCutOff) {
  Entity entity = entityManager.createEntity();

  auto light = std::make_unique<LightComponent>(type, position, direction, color, intensity, 0.2f, 1.0f, 0.09f, 0.032f,
                                                cutOff, outerCutOff);

  componentManager.add<NameComponent>(entity, std::make_unique<NameComponent>(name));
  componentManager.add<LightComponent>(entity, std::move(light));

  systemManager.getSystem<LightSystem>().addLight(entity);
}