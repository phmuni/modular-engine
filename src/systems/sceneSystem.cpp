// Scene system: entity factory methods for cameras, models, and lights.
#include "systems/sceneSystem.h"
#include "components/cameraComponent.h"
#include "components/lightComponent.h"
#include "components/modelComponent.h"
#include "components/nameComponent.h"
#include "components/transformComponent.h"
#include "rendering/resources/mesh.h"
#include "systems/cameraSystem.h"
#include "systems/lightSystem.h"
#include "systems/renderSystem.h"
#include "systems/resourceSystem.h"
#include <memory>
#include <utility>

void SceneSystem::destroyEntity(Entity entity) {
  auto &renderSystem = systemManager.getSystem<RenderSystem>();
  renderSystem.removeRenderable(entity);

  if (componentManager.has<LightComponent>(entity)) {
    auto &lightSystem = systemManager.getSystem<LightSystem>();
    lightSystem.destroyLight(entity);

  } else if (componentManager.has<CameraComponent>(entity)) {
    auto &cameraSystem = systemManager.getSystem<CameraSystem>();
    cameraSystem.removeActiveCamera();
  }

  componentManager.removeAll(entity);
  entityManager.destroyEntity(entity);
};

void SceneSystem::createCameraEntity(glm::vec3 position, float yaw, float pitch, float fov) {
  auto &cameraSystem = systemManager.getSystem<CameraSystem>();
  Entity newCamera = entityManager.createEntity();

  auto cameraComponent = std::make_unique<CameraComponent>();
  cameraComponent->position = position;
  cameraComponent->yaw = yaw;
  cameraComponent->pitch = pitch;
  cameraComponent->fov = fov;

  cameraSystem.updateFront(*cameraComponent);

  componentManager.insert<CameraComponent>(newCamera, std::move(cameraComponent));
  cameraSystem.setActiveCamera(newCamera);
}

Entity SceneSystem::createModelEntity(const std::string name, const std::string &modelPath, glm::vec3 position,
                                      glm::vec3 rotation, glm::vec3 scale) {
  Entity entity = entityManager.createEntity();

  auto &resourceSystem = systemManager.getSystem<ResourceSystem>();

  uint32_t meshHandle = resourceSystem.loadMesh(modelPath);
  Mesh &mesh = resourceSystem.getMesh(meshHandle);

  size_t submeshCount = mesh.getSubmeshes().size();
  std::vector<uint32_t> materialHandles(submeshCount, 0); // all start with default material

  componentManager.insert<NameComponent>(entity, std::make_unique<NameComponent>(name));
  componentManager.insert<TransformComponent>(entity, std::make_unique<TransformComponent>(position, rotation, scale));
  componentManager.insert<ModelComponent>(entity,
                                          std::make_unique<ModelComponent>(meshHandle, std::move(materialHandles)));

  systemManager.getSystem<RenderSystem>().insertRenderable(entity);

  return entity;
}

void SceneSystem::createLightEntity(const std::string &name, glm::vec3 position, glm::vec3 direction, glm::vec3 color,
                                    LightType type, float intensity, float cutOff, float outerCutOff) {
  Entity entity = entityManager.createEntity();

  auto light = std::make_unique<LightComponent>(type, position, direction, color, intensity, 0.2f, 1.0f, 0.09f, 0.032f,
                                                cutOff, outerCutOff);

  componentManager.insert<NameComponent>(entity, std::make_unique<NameComponent>(name));
  componentManager.insert<LightComponent>(entity, std::move(light));

  systemManager.getSystem<LightSystem>().createLight(entity);
}