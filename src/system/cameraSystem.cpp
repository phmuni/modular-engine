#include "system/cameraSystem.h"

void CameraSystem::update(Entity entity, float deltaTime) {
  if (!componentManager.has<CameraComponent>(entity))
    return;

  auto &cam = componentManager.get<CameraComponent>(entity);

  rotateCamera(cam);
  moveCamera(cam, deltaTime);
  updateFront(cam);
}

glm::mat4 CameraSystem::getViewMatrix(const CameraComponent &cam) const {
  return glm::lookAt(cam.position, cam.position + cam.front, cam.up);
}

glm::mat4 CameraSystem::getProjMatrix(const CameraComponent &cam) const {
  return glm::perspective(glm::radians(cam.fov), static_cast<float>(1280.0f) / 720.0f, 0.1f, 100.0f);
}

void CameraSystem::updateFront(CameraComponent &cam) {
  cam.front =
      glm::normalize(glm::vec3(cos(glm::radians(cam.pitch)) * sin(glm::radians(cam.yaw)), sin(glm::radians(cam.pitch)),
                               -cos(glm::radians(cam.pitch)) * cos(glm::radians(cam.yaw))));
}

void CameraSystem::rotateCamera(CameraComponent &cam) {
  float xoffset = input.getMouseXOffset() * cam.mouseSensitivity / cam.smoothFactor;
  float yoffset = input.getMouseYOffset() * cam.mouseSensitivity / cam.smoothFactor;

  cam.yaw += xoffset;
  cam.pitch -= yoffset;

  if (cam.yaw >= 360.0f)
    cam.yaw -= 360.0f;
  else if (cam.yaw < 0.0f)
    cam.yaw += 360.0f;

  cam.pitch = glm::clamp(cam.pitch, -89.9f, 89.9f);
}

void CameraSystem::moveCamera(CameraComponent &cam, float deltaTime) {
  float velocity = cam.moveSpeed * deltaTime;
  float angleRad = glm::radians(-cam.yaw);
  float cosAngle = cos(angleRad);
  float sinAngle = sin(angleRad);

  if (input.isActionPressed(Action::MoveForward)) {
    cam.position.z -= cosAngle * velocity;
    cam.position.x -= sinAngle * velocity;
  }
  if (input.isActionPressed(Action::MoveBackward)) {
    cam.position.z += cosAngle * velocity;
    cam.position.x += sinAngle * velocity;
  }
  if (input.isActionPressed(Action::MoveLeft)) {
    cam.position.z += sinAngle * velocity;
    cam.position.x -= cosAngle * velocity;
  }
  if (input.isActionPressed(Action::MoveRight)) {
    cam.position.z -= sinAngle * velocity;
    cam.position.x += cosAngle * velocity;
  }
  if (input.isActionPressed(Action::MoveUp)) {
    cam.position.y += velocity;
  }
  if (input.isActionPressed(Action::MoveDown)) {
    cam.position.y -= velocity;
  }
}
