#pragma once

#include "components/cameraComponent.h"
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

  void rotateCamera(CameraComponent &cam);
  void moveCamera(CameraComponent &cam, float deltaTime, const StateSystem &state);

public:
  CameraSystem(ComponentManager &cm, InputSystem &in) : m_componentManager(cm), m_input(in), m_activeCamera(-1) {}

  void update(float deltaTime, SystemManager &systemManager);
  void updateFront(CameraComponent &cam);

  glm::mat4 getViewMatrix(const CameraComponent &cam) const;
  glm::mat4 getProjMatrix(const CameraComponent &cam) const;

  Entity getActiveCamera() const;
  void removeActiveCamera();
  void setActiveCamera(Entity newCamera);
};
