#pragma once

#include "component/lightComponent.h" // IWYU pragma: keep
#include "component/modelComponent.h" // IWYU pragma: keep
#include "core/renderer.h"
#include "ecs/componentManager.h"
#include "ecs/entityManager.h"
#include "ecs/systemManager.h"
#include "manager/cameraManager.h"
#include "manager/shaderManager.h"
#include "system/cameraSystem.h"    // IWYU pragma: keep
#include "system/lightSystem.h"     // IWYU pragma: keep
#include "system/transformSystem.h" // IWYU pragma: keep

#include <vector>

class RenderSystem {
public:
  struct RenderEntry {
    explicit RenderEntry(Entity entity) : entity(entity) {}
    Entity entity;
  };

  void addRenderable(Entity entity);

  void renderCall(ShaderManager &shaderManager, SystemManager &systemManager, EntityManager &entityManager,
                  ComponentManager &componentManager, ActiveCameraManager &cameraManager);

  Renderer &getRenderer();

private:
  std::vector<RenderEntry> m_entries;
  Renderer m_renderer;

  void setupLights();
};
