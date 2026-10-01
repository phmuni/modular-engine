#pragma once
// Camera system for managing active camera, view/projection matrices, and input-based movement.

#include "components/camera.h"
#include "foundation/ecs/componentManager.h"
#include "foundation/ecs/systemManager.h"
#include "systems/inputSystem.h"

class CameraSystem : public BaseSystem {

public:
  CameraSystem(ComponentManager &cm, InputSystem &in) : m_componentManager(cm), m_input(in), m_activeCamera(-1) {}

  void update(float deltaTime, SystemManager &systemManager);
  void updateFrontVector(Camera &cam);

  glm::mat4 getViewMatrix(const Camera &cam) const;
  glm::mat4 getProjectionMatrix(const Camera &cam) const;

  Entity getActiveCamera() const;
  void removeActiveCamera();
  void setActiveCamera(Entity newCamera);

private:
  ComponentManager &m_componentManager;
  InputSystem &m_input;
  Entity m_activeCamera;

  void updateOrientation(Camera &cam);
  void updatePositionFromInput(Camera &cam, float deltaTime, const StateSystem &state);
};
