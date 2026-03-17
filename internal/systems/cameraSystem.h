#pragma once
// Camera movement, rotation, and matrix computation.

#include "components/camera.h"
#include "foundation/ecs/componentManager.h"
#include "foundation/ecs/systemManager.h"
#include "systems/inputSystem.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

class StateSystem;

class CameraSystem : public BaseSystem {
private:
  ComponentManager &m_componentManager;
  InputSystem &m_input;
  Entity m_activeCamera;

  void updateOrientation(Camera &cam);
  void updatePositionFromInput(Camera &cam, float deltaTime, const StateSystem &state);

public:
  CameraSystem(ComponentManager &cm, InputSystem &in) : m_componentManager(cm), m_input(in), m_activeCamera(-1) {}

  void update(float deltaTime, SystemManager &systemManager);
  void updateFrontVector(Camera &cam);

  glm::mat4 getViewMatrix(const Camera &cam) const;
  glm::mat4 getProjectionMatrix(const Camera &cam) const;

  Entity getActiveCamera() const;
  void removeActiveCamera();
  void setActiveCamera(Entity newCamera);
};
