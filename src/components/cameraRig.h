#pragma once
// Camera follow: put it on the active camera's entity to have the camera trail another entity. CameraSystem moves the
// camera after physics and the game's update, so it does not jitter behind a character. Engine::camera().follow
// adds one; scene files keep it and the Properties panel edits it.
//
// The free camera (Settings) switches the rig off, like it does for GameCamera::lookAt, and a lookAt called in a frame
// wins over the rig for that frame (the rig then picks up from where the camera was left).

#include "foundation/ecs/entityManager.h"
#include <glm/glm.hpp>
#include <string>

struct CameraRig {
  // What it follows, by Name (entity numbers change from run to run, names survive a scene file). Empty: nothing.
  std::string target;
  // Where the camera sits from the target: x to its right, y up, z behind it
  glm::vec3 offset{0.0f, 4.0f, 7.0f};
  float lookAhead = 0.0f;         // the camera looks this far in front of the target, along its facing
  float positionSmoothing = 0.0f; // seconds the camera takes to catch up with where it should be; 0: stays rigid
  float rotationSmoothing = 0.0f; // the same for the target's facing and the point looked at
  bool followRotation = true;     // the offset turns with the target (behind it); off: the offset is in world axes

  // Orbit: turn around the target with the mouse (while the cursor is captured, or the right button is held) and
  // the gamepad's right stick, on top of the target's own facing
  bool orbit = false;
  float orbitSensitivity = 0.15f; // degrees per pixel of mouse movement
  float orbitSpeed = 120.0f;      // degrees per second at full stick

  // Runtime state, not saved
  Entity targetEntity = -1;
  std::string resolvedName; // the `target` that targetEntity was found for
  bool primed = false;      // false: the next update puts the camera right where it should be
  float retryTimer = 0.0f;  // between searches for a target that is not there
  float yaw = 0.0f;         // the facing the camera follows (degrees), smoothed
  glm::vec3 focus{0.0f};    // the point looked at, smoothed
  glm::vec3 position{0.0f}; // where the camera is, smoothed
  float orbitYaw = 0.0f;
  float orbitPitch = 0.0f;
};
