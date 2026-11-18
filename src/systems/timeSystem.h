#pragma once
// Frame timing: delta time and elapsed time tracking.

#include "foundation/ecs/systemManager.h"

class TimeSystem : public BaseSystem {
private:
  float m_deltaTime = 0.0f;         // scaled by the time scale, 0 while paused
  float m_unscaledDeltaTime = 0.0f; // real time between frames
  float m_timeScale = 1.0f;
  bool m_paused = false;
  float m_lastFrameTime = 0.0f;
  float m_startTime = 0.0f;

public:
  TimeSystem() = default;
  ~TimeSystem() = default;

  void begin();
  void update();

  // Simulation time (particles, game logic)
  float getDeltaTime() const;
  // Real time (camera, UI, timers that should keep running while paused)
  float getUnscaledDeltaTime() const;

  float getTimeScale() const { return m_timeScale; }
  void setTimeScale(float scale) { m_timeScale = scale < 0.0f ? 0.0f : scale; }
  bool isPaused() const { return m_paused; }
  void setPaused(bool paused) { m_paused = paused; }
  float getLastFrameTime() const;
  float getTimeSinceStart() const;

  void setDeltaTime(float dt);
  void setLastFrameTime(float time);
};
