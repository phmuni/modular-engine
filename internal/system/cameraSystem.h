#pragma once

#include "component/cameraComponent.h"
#include "ecs/componentManager.h"
#include "system/inputSystem.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

class CameraSystem {
private:
  ComponentManager &componentManager;
  InputSystem &input;

public:
  CameraSystem(ComponentManager &cm, InputSystem &in) : componentManager(cm), input(in) {};

  void update(Entity entity, float deltaTime);
  void updateFront(CameraComponent &cam);
  void rotateCamera(CameraComponent &cam);
  void moveCamera(CameraComponent &cam, float deltaTime);
  glm::mat4 getViewMatrix(const CameraComponent &cam) const;
  glm::mat4 getProjMatrix(const CameraComponent &cam) const;
};
