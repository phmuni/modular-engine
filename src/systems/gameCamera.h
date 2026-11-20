#pragma once
// The game API for the active camera: where it looks and how wide. Lua's `camera` table is this class (see
// systems/scriptSystem.cpp), reached from C++ as engine.camera(). The full component is engine.getActiveCamera().

#include <glm/glm.hpp>

class Engine;

class GameCamera {
public:
  explicit GameCamera(Engine &engine) : m_engine(engine) {}

  // Places the camera at `from`, facing `target`. Does nothing while the free camera (Settings) is on, so the
  // engine's own examples and the editor can fly around a game that drives the camera.
  void lookAt(const glm::vec3 &from, const glm::vec3 &target);
  glm::vec3 position() const;
  // The way it looks, a unit vector (-Z with no camera)
  glm::vec3 forward() const;
  // Vertical field of view in degrees (90 with no camera); the setter ignores values outside 1..179
  float fov() const;
  void setFov(float degrees);

private:
  Engine &m_engine;
};
