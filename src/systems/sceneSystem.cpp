// Scene system: entity factory methods for cameras, models, and lights.
#include "systems/sceneSystem.h"
#include "components/camera.h"
#include "components/light.h"
#include "components/model.h"
#include "components/name.h"
#include "components/transform.h"
#include "systems/cameraSystem.h"
#include "systems/lightSystem.h"
#include "systems/renderSystem.h"
#include "systems/resourceSystem.h"
#include <memory>
#include <utility>

void SceneSystem::destroyEntity(Entity entity) {
  auto &renderSystem = systemManager.getSystem<RenderSystem>();
  renderSystem.removeRenderable(entity);

  if (componentManager.has<Light>(entity)) {
    auto &lightSystem = systemManager.getSystem<LightSystem>();
    lightSystem.destroyLight(entity);

  } else if (componentManager.has<Camera>(entity)) {
    auto &cameraSystem = systemManager.getSystem<CameraSystem>();
    cameraSystem.removeActiveCamera();
  }

  componentManager.removeAllComponents(entity);
  entityManager.destroyEntity(entity);
};

void SceneSystem::createCameraEntity(glm::vec3 position, float yaw, float pitch, float fov) {
  auto &cameraSystem = systemManager.getSystem<CameraSystem>();
  Entity newCamera = entityManager.createEntity();

  auto camera = std::make_unique<Camera>();
  camera->position = position;
  camera->yaw = yaw;
  camera->pitch = pitch;
  camera->fov = fov;

  cameraSystem.updateFrontVector(*camera);

  componentManager.insert<Camera>(newCamera, std::move(camera));
  cameraSystem.setActiveCamera(newCamera);
}

Entity SceneSystem::createModelEntity(const std::string name, const std::string &modelPath, glm::vec3 position,
                                      glm::vec3 rotation, glm::vec3 scale) {
  Entity entity = entityManager.createEntity();

  auto &resourceSystem = systemManager.getSystem<ResourceSystem>();

  auto modelData = resourceSystem.loadModel(modelPath);
  uint32_t meshHandle = modelData.meshHandle;
  std::vector<uint32_t> materialHandles = std::move(modelData.materialHandles);

  componentManager.insert<Name>(entity, std::make_unique<Name>(name));
  componentManager.insert<Transform>(entity, std::make_unique<Transform>(position, rotation, scale));
  componentManager.insert<Model>(entity, std::make_unique<Model>(meshHandle, std::move(materialHandles)));

  systemManager.getSystem<RenderSystem>().insertRenderable(entity);

  return entity;
}

void SceneSystem::createLightEntity(const std::string &name, glm::vec3 position, glm::vec3 direction, glm::vec3 color,
                                    LightType type, float intensity, float cutOff, float outerCutOff) {
  Entity entity = entityManager.createEntity();

  auto light = std::make_unique<Light>(type, glm::vec3(0.0f), direction, color, intensity, 0.2f, 1.0f, 0.09f, 0.032f,
                                       cutOff, outerCutOff);

  componentManager.insert<Name>(entity, std::make_unique<Name>(name));
  componentManager.insert<Transform>(entity, std::make_unique<Transform>(position));
  componentManager.insert<Light>(entity, std::move(light));

  systemManager.getSystem<LightSystem>().createLight(entity);
}