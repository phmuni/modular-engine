#pragma once

#include "component/cameraComponent.h"
#include "ecs/componentManager.h"
#include "ecs/systemManager.h"
#include "system/inputSystem.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

// Manages camera transformations and matrix calculations.
// Supports free camera with mouse look and keyboard movement.
// Maintains separate view and projection matrices for flexibility.
class CameraSystem : public BaseSystem {
private:
  ComponentManager &componentManager;
  InputSystem &input;
  Entity activeCamera;

public:
  CameraSystem(ComponentManager &cm, InputSystem &in) : componentManager(cm), input(in), activeCamera(-1) {};

  void update(float deltaTime, SystemManager &systemManager);
  void updateFront(CameraComponent &cam);
  void rotateCamera(CameraComponent &cam);
  void moveCamera(CameraComponent &cam, float deltaTime);
  glm::mat4 getViewMatrix(const CameraComponent &cam) const;
  glm::mat4 getProjMatrix(const CameraComponent &cam) const;

  Entity getActiveCamera() const;
  void setActiveCamera(Entity newCamera);
};
