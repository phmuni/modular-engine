
// Camera system implementation for handling camera movement, orientation, and view/projection matrix calculations.

#include "systems/cameraSystem.h"
#include "foundation/core/config.h"
#include "components/name.h"
#include "components/transform.h"
#include "systems/windowSystem.h"
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

void CameraSystem::update(float deltaTime, SystemManager &systemManager) {

  m_navigating = false;
  Entity entity = getActiveCamera();
  if (entity == -1 || !m_componentManager.has<Camera>(entity)) {
    return;
  }

  auto &cam = m_componentManager.get<Camera>(entity);
  auto &state = systemManager.getSystem<StateSystem>();
  auto &windowSystem = systemManager.getSystem<WindowSystem>();

  const bool cursorLocked = state.isToggled(Toggle::CursorLock);
  const bool flying = m_input.isEditorFlying(); // the right button: the mouse is the camera's while it is down
  windowSystem.setCursor(cursorLocked || flying);

  m_navigating = canNavigate(state) && (cursorLocked || flying);
  if (m_navigating) {
    updateOrientation(cam);
    if (flying)
      updateEditorFlight(cam, deltaTime);
    else
      updatePositionFromInput(cam, deltaTime, state);
    m_editorOverridesRig = m_editorOverridesRig || (m_editing && m_editorShown);
  }

  updateFrontVector(cam);
}

void CameraSystem::setEditorNavigation(bool editing, bool editorShown) {
  m_editing = editing;
  m_editorShown = editorShown;
  if (!editing)
    m_editorOverridesRig = false;
}

bool CameraSystem::canNavigate(const StateSystem &state) const {
  return (m_editing && m_editorShown) || state.isToggled(Toggle::CameraMovement);
}

bool CameraSystem::resolveRigTarget(CameraRig &rig, float deltaTime) {
  if (rig.targetEntity != -1 && rig.resolvedName == rig.target && m_componentManager.has<Transform>(rig.targetEntity))
    return true;
  if (rig.target.empty()) {
    rig.targetEntity = -1;
    return false;
  }
  // Gone, or renamed: look again, not every frame while it is missing (a game time of 0 is the editor looking: always)
  if (rig.resolvedName == rig.target && deltaTime > 0.0f && (rig.retryTimer -= deltaTime) > 0.0f)
    return false;
  rig.retryTimer = 0.25f;
  Entity found = -1;
  m_componentManager.forEachComponent<Name>([&](Entity entity, Name &name) {
    if (name.name == rig.target && (found == -1 || entity < found) && m_componentManager.has<Transform>(entity))
      found = entity;
  });
  rig.targetEntity = found;
  rig.resolvedName = rig.target;
  rig.primed = false;
  return found != -1;
}

void CameraSystem::updateRigs(float deltaTime, SystemManager &systemManager) {
  const bool placedByGame = m_manualPlacement;
  m_manualPlacement = false;
  const Entity entity = getActiveCamera();
  if (entity == -1)
    return;
  CameraRig *rig = m_componentManager.tryGet<CameraRig>(entity);
  Camera *cam = m_componentManager.tryGet<Camera>(entity);
  if (!rig || !cam)
    return;
  const auto &state = systemManager.getSystem<StateSystem>();
  if (state.isToggled(Toggle::CameraMovement) || placedByGame || m_editorOverridesRig) {
    rig->primed = false; // whoever moved the camera, the rig starts from where it should be when it takes over again
    return;
  }
  if (!resolveRigTarget(*rig, deltaTime))
    return;
  const Transform &target = m_componentManager.get<Transform>(rig->targetEntity);

  const bool running = deltaTime > 0.0f;
  if (rig->orbit && running) {
    glm::vec2 turn(0.0f); // degrees: x around the target, y up and down
    if (state.isToggled(Toggle::CursorLock) || m_input.isMouseButtonDown(MouseButton::Right))
      turn += m_input.getMouseDelta() * rig->orbitSensitivity;
    turn += glm::vec2(m_input.getGamepadAxis(SDL_GAMEPAD_AXIS_RIGHTX), m_input.getGamepadAxis(SDL_GAMEPAD_AXIS_RIGHTY)) *
            rig->orbitSpeed * deltaTime;
    rig->orbitYaw = std::fmod(rig->orbitYaw - turn.x, 360.0f); // moving right looks right: the camera goes left
    rig->orbitPitch = glm::clamp(rig->orbitPitch + turn.y, -20.0f, 80.0f);
  }

  // How much of the way to where it should be it goes this frame: independent of the frame rate
  auto blend = [&](float seconds) {
    return !running || !rig->primed || seconds <= 0.0f ? 1.0f : 1.0f - std::exp(-deltaTime / seconds);
  };
  const float targetYaw = rig->followRotation ? target.rotation.y : 0.0f;
  float yawStep = std::fmod(targetYaw - rig->yaw, 360.0f); // the short way round
  if (yawStep > 180.0f)
    yawStep -= 360.0f;
  else if (yawStep < -180.0f)
    yawStep += 360.0f;
  rig->yaw = rig->primed ? rig->yaw + yawStep * blend(rig->rotationSmoothing) : targetYaw;

  // The offset in the rig's frame (x right, y up, z behind), tilted up by the orbit pitch, then turned by the yaw
  const float yaw = glm::radians(rig->yaw + rig->orbitYaw), pitch = glm::radians(rig->orbitPitch);
  const glm::vec3 tilted(rig->offset.x, rig->offset.y * std::cos(pitch) + rig->offset.z * std::sin(pitch),
                         rig->offset.z * std::cos(pitch) - rig->offset.y * std::sin(pitch));
  const glm::vec3 forward(-std::sin(yaw), 0.0f, -std::cos(yaw)), right(std::cos(yaw), 0.0f, -std::sin(yaw));
  const glm::vec3 wantedPosition =
      target.position + right * tilted.x + glm::vec3(0.0f, tilted.y, 0.0f) - forward * tilted.z;
  const glm::vec3 wantedFocus = target.position + forward * rig->lookAhead;

  rig->position = rig->primed ? glm::mix(rig->position, wantedPosition, blend(rig->positionSmoothing)) : wantedPosition;
  rig->focus = rig->primed ? glm::mix(rig->focus, wantedFocus, blend(rig->rotationSmoothing)) : wantedFocus;
  rig->primed = true;

  if (glm::distance(rig->position, rig->focus) > 1e-4f) { // a camera on its target has no direction
    glm::vec3 offset(0.0f); // a camera relative to its own Transform keeps the place it is given relative to that
    if (cam->isRelative)
      if (const Transform *own = m_componentManager.tryGet<Transform>(entity))
        offset = own->position;
    cam->lookAt(rig->position - offset, rig->focus - offset);
  }
}

glm::mat4 CameraSystem::getViewMatrix(const Camera &cam) const {
  if (m_componentManager.has<Transform>(getActiveCamera()) && cam.isRelative) {
    auto &transform = m_componentManager.get<Transform>(getActiveCamera());
    auto finalPosition = transform.position + cam.position;
    return glm::lookAt(finalPosition, finalPosition + cam.front, cam.up);
  }

  return glm::lookAt(cam.position, cam.position + cam.front, cam.up);
}

glm::mat4 CameraSystem::getProjectionMatrix(const Camera &cam) const {
  return glm::perspective(glm::radians(cam.fov), cam.aspectRatio, EngineConfig::CAMERA_NEAR, EngineConfig::CAMERA_FAR);
}

void CameraSystem::updateFrontVector(Camera &cam) { cam.updateFront(); }

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

void CameraSystem::updateEditorFlight(Camera &cam, float deltaTime) {
  const float wheel = m_input.getFlyWheel();
  if (wheel != 0.0f)
    m_flySpeedScale = glm::clamp(m_flySpeedScale * std::pow(1.25f, wheel), 0.05f, 50.0f);

  cam.updateFront(); // the mouse turned it this frame
  const glm::vec3 right = glm::normalize(glm::cross(cam.front, cam.worldUp));
  auto down = [&](SDL_Scancode key) { return m_input.isRawKeyDown(key) ? 1.0f : 0.0f; };
  glm::vec3 move = cam.front * (down(SDL_SCANCODE_W) - down(SDL_SCANCODE_S)) +
                   right * (down(SDL_SCANCODE_D) - down(SDL_SCANCODE_A)) +
                   cam.worldUp * (std::max(down(SDL_SCANCODE_E), down(SDL_SCANCODE_SPACE)) -
                                  std::max(down(SDL_SCANCODE_Q), down(SDL_SCANCODE_LCTRL)));
  if (glm::dot(move, move) < 1e-6f)
    return;
  const bool fast = m_input.isRawKeyDown(SDL_SCANCODE_LSHIFT) || m_input.isRawKeyDown(SDL_SCANCODE_RSHIFT);
  cam.position += glm::normalize(move) * (cam.moveSpeed * m_flySpeedScale * (fast ? 4.0f : 1.0f) * deltaTime);
}

Entity CameraSystem::getActiveCamera() const { return m_activeCamera; }
void CameraSystem::removeActiveCamera() { m_activeCamera = -1; }
void CameraSystem::setActiveCamera(Entity newCamera) { m_activeCamera = newCamera; }
