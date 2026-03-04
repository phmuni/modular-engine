#pragma once
// Frame timing: delta time and elapsed time tracking.

#include "foundation/ecs/systemManager.h"
#include <SDL3/SDL.h>

class TimeSystem : public BaseSystem {
private:
  float m_deltaTime = 0.0f;
  float m_lastFrameTime = 0.0f;
  float m_startTime = 0.0f;

public:
  TimeSystem() = default;
  ~TimeSystem() = default;

  void begin();
  void update();

  float getDeltaTime() const;
  float getLastFrameTime() const;
  float getTimeSinceStart() const;

  void setDeltaTime(float dt);
  void setLastFrameTime(float time);
};
