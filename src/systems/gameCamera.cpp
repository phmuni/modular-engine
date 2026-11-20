// The game API for the active camera: see gameCamera.h.

#include "systems/gameCamera.h"

#include "components/camera.h"
#include "foundation/core/engine.h"

void GameCamera::lookAt(const glm::vec3 &from, const glm::vec3 &target) {
  if (m_engine.getState(Toggle::CameraMovement))
    return;
  if (Camera *active = m_engine.getActiveCamera())
    active->lookAt(from, target);
}

glm::vec3 GameCamera::position() const {
  const Camera *active = m_engine.getActiveCamera();
  return active ? active->position : glm::vec3(0.0f);
}

glm::vec3 GameCamera::forward() const {
  const Camera *active = m_engine.getActiveCamera();
  return active ? active->front : glm::vec3(0.0f, 0.0f, -1.0f);
}

float GameCamera::fov() const {
  const Camera *active = m_engine.getActiveCamera();
  return active ? active->fov : 90.0f;
}

void GameCamera::setFov(float degrees) {
  if (Camera *active = m_engine.getActiveCamera(); active && degrees > 1.0f && degrees < 179.0f)
    active->fov = degrees;
}
