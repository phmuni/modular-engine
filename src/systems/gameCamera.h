#pragma once
// The game API for the active camera: where it looks and how wide. Lua's `camera` table is this class (see
// systems/scriptSystem.cpp), reached from C++ as engine.camera(). The full component is engine.getActiveCamera().

#include "components/cameraRig.h"
#include <glm/glm.hpp>
#include <optional>

class Engine;
class GameEntity;

class GameCamera {
public:
  explicit GameCamera(Engine &engine) : m_engine(engine) {}

  // Places the camera at `from`, facing `target`. Does nothing while the free camera (Settings) is on, so the
  // engine's own examples and the editor can fly around a game that drives the camera.
  void lookAt(const glm::vec3 &from, const glm::vec3 &target);
  glm::vec3 position() const;
  // Trails `target` from behind, as the CameraRig component says (see components/cameraRig.h; `options.target` and
  // the runtime fields are ignored). It adds a rig to the active camera's entity, replacing one that is there, and
  // CameraSystem moves the camera each frame after physics and the game's update, so there is no jitter.
  // Who wins: the free camera (Settings) switches it off, like it does lookAt, and a lookAt in the same frame wins
  // over the rig for that frame; the rig takes over again, from where it should be, when lookAt stops.
  // A scene load replaces the camera entity, so the request (target name and options) is remembered and put on the
  // new scene's active camera after loadScene, before onSceneLoaded. A scene whose camera has a CameraRig of its own
  // keeps it: the code request only applies to a camera without one (the file is the more specific, and it was
  // authored for that level). The request stays remembered, so a later scene without a rig is followed again. The
  // target is found by name, so it follows once the new scene has an entity of that name. stopFollow forgets it.
  void follow(const GameEntity &target, const CameraRig &options = CameraRig());
  void stopFollow();
  bool isFollowing() const;
  // The way it looks, a unit vector (-Z with no camera)
  glm::vec3 forward() const;
  // Vertical field of view in degrees (90 with no camera); the setter ignores values outside 1..179
  float fov() const;
  void setFov(float degrees);

private:
  friend class Engine; // applyRememberedFollow, after a scene load
  // Puts the remembered follow request on the active camera when it has no rig
  void applyRememberedFollow();

  Engine &m_engine;
  std::optional<CameraRig> m_followRequest;
};
