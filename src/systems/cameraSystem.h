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

  // Where the editor has the camera: the game is frozen for editing (Edit) and the editor is shown. There the camera
  // flies by the right mouse button held in the view (see InputSystem::isEditorFlying) or the cursor lock, and keeps
  // where it was flown instead of going back to its CameraRig, until the game runs again (Play, Resume).
  void setEditorNavigation(bool editing, bool editorShown);
  // Whether the camera can be flown by hand now: in the editor as above, or with the free camera on (Settings)
  bool canNavigate(const StateSystem &state) const;
  // The camera is being flown by hand this frame (locked cursor or right button): the editor keeps its shortcuts quiet
  bool isNavigating() const { return m_navigating; }
  // Scroll while flying changes this: a multiplier on the camera's move speed
  float getFlySpeedScale() const { return m_flySpeedScale; }

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
  bool m_editing = false;
  bool m_editorShown = false;
  bool m_editorOverridesRig = false; // flown by hand in the editor: the rig stays off until the game runs again
  bool m_navigating = false;
  float m_flySpeedScale = 1.0f;

  // Finds the entity a rig follows by its name; false when there is none (yet)
  bool resolveRigTarget(CameraRig &rig, float deltaTime);
  void updateOrientation(Camera &cam);
  void updatePositionFromInput(Camera &cam, float deltaTime, const StateSystem &state);
  // Right button flight: along where the camera looks, W A S D, E/Space up, Q/Ctrl down, Shift faster, wheel for speed
  void updateEditorFlight(Camera &cam, float deltaTime, const StateSystem &state);
};
