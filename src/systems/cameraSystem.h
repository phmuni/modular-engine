#pragma once
// Camera system for managing active camera, view/projection matrices, and input-based movement.

#include "components/camera.h"
#include "components/cameraRig.h"
#include "foundation/ecs/componentManager.h"
#include "foundation/ecs/systemManager.h"
#include "systems/inputSystem.h"

class CameraSystem : public BaseSystem {

public:
  CameraSystem(ComponentManager &cm, InputSystem &in) : m_componentManager(cm), m_input(in), m_activeCamera(-1) {}

  void update(float deltaTime, SystemManager &systemManager);
  // Moves the active camera along with its CameraRig, if it has one (see components/cameraRig.h). After physics and the
  // game's update, so the camera sees where things ended up this frame. `deltaTime` is game time: 0 while the game
  // stands still, and the rig then just places the camera (no smoothing, no orbiting), so edits show in the editor.
  void updateRigs(float deltaTime, SystemManager &systemManager);
  // A game placed the camera itself this frame (GameCamera::lookAt): the rig leaves it alone for the frame
  void noteManualPlacement() { m_manualPlacement = true; }
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
  bool m_manualPlacement = false;

  // Finds the entity a rig follows by its name; false when there is none (yet)
  bool resolveRigTarget(CameraRig &rig, float deltaTime);
  void updateOrientation(Camera &cam);
  void updatePositionFromInput(Camera &cam, float deltaTime, const StateSystem &state);
};
