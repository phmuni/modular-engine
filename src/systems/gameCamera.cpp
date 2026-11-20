// The game API for the active camera: see gameCamera.h.

#include "systems/gameCamera.h"

#include "components/camera.h"
#include "foundation/core/engine.h"
#include "foundation/core/logger.h"
#include "systems/cameraSystem.h"

void GameCamera::lookAt(const glm::vec3 &from, const glm::vec3 &target) {
  if (m_engine.getState(Toggle::CameraMovement))
    return;
  if (Camera *active = m_engine.getActiveCamera()) {
    active->lookAt(from, target);
    m_engine.getSystemManager().getSystem<CameraSystem>().noteManualPlacement();
  }
}

void GameCamera::follow(const GameEntity &target, const CameraRig &options) {
  const Entity camera = m_engine.getSystemManager().getSystem<CameraSystem>().getActiveCamera();
  if (camera == -1 || !m_engine.has<Camera>(camera)) {
    LOG_W("[Camera] follow: there is no active camera (a scene load replaces it: follow again in onSceneLoaded)");
    return;
  }
  if (!target.valid()) {
    LOG_W("[Camera] follow: the target does not exist");
    return;
  }
  CameraRig rig = options;
  rig.target = target.name();
  rig.targetEntity = target.id();
  rig.resolvedName = rig.target;
  rig.primed = false;
  rig.orbitYaw = rig.orbitPitch = 0.0f;
  m_engine.getComponentManager().addInPlace<CameraRig>(camera) = rig;
}

void GameCamera::stopFollow() {
  const Entity camera = m_engine.getSystemManager().getSystem<CameraSystem>().getActiveCamera();
  if (camera != -1)
    m_engine.removeComponent<CameraRig>(camera);
}

bool GameCamera::isFollowing() const {
  const Entity camera = m_engine.getSystemManager().getSystem<CameraSystem>().getActiveCamera();
  return camera != -1 && m_engine.has<CameraRig>(camera);
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
