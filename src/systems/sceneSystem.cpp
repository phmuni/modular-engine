
// Scene system implementation for managing entities, components, and their interactions with other systems like
// rendering and lighting.

#include "systems/sceneSystem.h"
#include "components/name.h"
#include "components/transform.h"
#include "systems/cameraSystem.h"
#include "systems/lightSystem.h"
#include "systems/renderSystem.h"
#include "systems/resourceSystem.h"

void SceneSystem::destroyEntity(Entity entity) {
  auto &renderSystem = m_systemManager.getSystem<RenderSystem>();
  renderSystem.removeRenderable(entity);

  if (m_componentManager.containsComponent<Light>(entity)) {
    auto &lightSystem = m_systemManager.getSystem<LightSystem>();
    lightSystem.destroyLight(entity);

  } else if (m_componentManager.containsComponent<Camera>(entity)) {
    auto &cameraSystem = m_systemManager.getSystem<CameraSystem>();
    if (cameraSystem.getActiveCamera() == entity) {
      cameraSystem.removeActiveCamera();
    }
  }

  m_componentManager.removeAllComponents(entity);
  m_entityManager.destroyEntity(entity);
};

void SceneSystem::createCameraEntity(std::string name, glm::vec3 position, float yaw, float pitch, float fov,
                                     bool isActive, bool isRelative) {
  auto &cameraSystem = m_systemManager.getSystem<CameraSystem>();
  Entity newCamera = m_entityManager.createEntity();

  auto camera = std::make_unique<Camera>();
  camera->position = position;
  camera->yaw = yaw;
  camera->pitch = pitch;
  camera->fov = fov;
  camera->isRelative = isRelative;
  cameraSystem.updateFrontVector(*camera);

  m_componentManager.insert<Name>(newCamera, std::make_unique<Name>(std::move(name)));
  m_componentManager.insert<Transform>(newCamera,
                                       std::make_unique<Transform>(position, glm::vec3(0.0f), glm::vec3(1.0f)));
  m_componentManager.insert<Camera>(newCamera, std::move(camera));

  if (isActive)
    cameraSystem.setActiveCamera(newCamera);
}

Entity SceneSystem::createModelEntity(std::string name, std::string modelPath, glm::vec3 position, glm::vec3 rotation,
                                      glm::vec3 scale) {
  auto &resourceSystem = m_systemManager.getSystem<ResourceSystem>();
  Entity entity = m_entityManager.createEntity();
  auto modelData = resourceSystem.loadModel(modelPath);
  auto materialHandles = std::move(modelData.materialHandles);

  m_componentManager.insert<Name>(entity, std::make_unique<Name>(std::move(name)));
  m_componentManager.insert<Transform>(entity, std::make_unique<Transform>(position, rotation, scale));
  m_componentManager.insert<Model>(
      entity, std::make_unique<Model>(modelData.meshHandle, std::move(materialHandles), modelData.transparent));

  m_systemManager.getSystem<RenderSystem>().insertRenderable(entity);
  return entity;
}

void SceneSystem::createLightEntity(std::string name, glm::vec3 position, glm::vec3 direction, glm::vec3 color,
                                    LightType type, float intensity, float cutOff, float outerCutOff) {
  Entity entity = m_entityManager.createEntity();

  m_componentManager.insert<Name>(entity, std::make_unique<Name>(std::move(name)));
  m_componentManager.insert<Transform>(entity, std::make_unique<Transform>(position, glm::vec3(0.0f), glm::vec3(1.0f)));
  m_componentManager.insert<Light>(entity, std::make_unique<Light>(type, glm::vec3(0.0f), direction, color));

  m_systemManager.getSystem<LightSystem>().createLight(entity);
}