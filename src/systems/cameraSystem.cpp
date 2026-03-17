// Camera system: FPS-style movement and mouse look.
#include "systems/cameraSystem.h"
#include "components/camera.h"
#include "systems/inputSystem.h"
#include "systems/stateSystem.h"
#include "systems/windowSystem.h"

void CameraSystem::update(float deltaTime, SystemManager &systemManager) {
  Entity entity = getActiveCamera();
  if (!m_componentManager.has<Camera>(entity))
    return;

  auto &cam = m_componentManager.get<Camera>(entity);
  auto &state = systemManager.getSystem<StateSystem>();
  auto &windowSystem = systemManager.getSystem<WindowSystem>();

  bool cursorLocked = state.isToggled(Toggle::CursorLock);
  windowSystem.setCursor(cursorLocked);

  if (state.isToggled(Toggle::CameraMovement) && cursorLocked) {
    updateOrientation(cam);
    updatePositionFromInput(cam, deltaTime, state);
  }

  updateFrontVector(cam);
}

glm::mat4 CameraSystem::getViewMatrix(const Camera &cam) const {
  return glm::lookAt(cam.position, cam.position + cam.front, cam.up);
}

glm::mat4 CameraSystem::getProjectionMatrix(const Camera &cam) const {
  return glm::perspective(glm::radians(cam.fov), cam.aspectRatio, 0.1f, 100.0f);
}

void CameraSystem::updateFrontVector(Camera &cam) {
  float pitchRad = glm::radians(cam.pitch);
  float yawRad = glm::radians(cam.yaw);

  cam.front = glm::normalize(glm::vec3(cos(pitchRad) * sin(yawRad), sin(pitchRad), -cos(pitchRad) * cos(yawRad)));
}

void CameraSystem::updateOrientation(Camera &cam) {
  float xoffset = m_input.getMouseXOffset() * cam.mouseSensitivity / cam.smoothFactor;
  float yoffset = m_input.getMouseYOffset() * cam.mouseSensitivity / cam.smoothFactor;

  cam.yaw += xoffset;
  cam.pitch -= yoffset;

  if (cam.yaw >= 360.0f)
    cam.yaw -= 360.0f;
  else if (cam.yaw < 0.0f)
    cam.yaw += 360.0f;

  cam.pitch = glm::clamp(cam.pitch, -89.9f, 89.9f);
}

void CameraSystem::updatePositionFromInput(Camera &cam, float deltaTime, const StateSystem &state) {
  float velocity = cam.moveSpeed * deltaTime;
  float angleRad = glm::radians(-cam.yaw);
  float cosA = cos(angleRad);
  float sinA = sin(angleRad);

  if (m_input.isActionPressed(Action::MoveForward)) {
    cam.position.z -= cosA * velocity;
    cam.position.x -= sinA * velocity;
  }
  if (m_input.isActionPressed(Action::MoveBackward)) {
    cam.position.z += cosA * velocity;
    cam.position.x += sinA * velocity;
  }
  if (m_input.isActionPressed(Action::MoveLeft)) {
    cam.position.z += sinA * velocity;
    cam.position.x -= cosA * velocity;
  }
  if (m_input.isActionPressed(Action::MoveRight)) {
    cam.position.z -= sinA * velocity;
    cam.position.x += cosA * velocity;
  }
  if (state.isToggled(Toggle::CameraFly)) {
    if (m_input.isActionPressed(Action::MoveUp))
      cam.position.y += velocity;
    if (m_input.isActionPressed(Action::MoveDown))
      cam.position.y -= velocity;
  }
}

Entity CameraSystem::getActiveCamera() const { return m_activeCamera; }
void CameraSystem::removeActiveCamera() { m_activeCamera = -1; }
void CameraSystem::setActiveCamera(Entity newCamera) { m_activeCamera = newCamera; }
