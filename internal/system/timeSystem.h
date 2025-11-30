#pragma once

#include "ecs/systemManager.h"
#include <SDL3/SDL.h>

class TimeSystem : public BaseSystem {
private:
  float deltaTime = 0.0f;
  float lastFrameTime = 0.0f;
  float startTime = 0.0f;

public:
  TimeSystem() = default;
  ~TimeSystem() = default;

  void start();
  void update();

  float getDeltaTime() const;
  float getLastFrameTime() const;
  float getTimeSinceStart() const;

  void setDeltaTime(float dt);
  void setLastFrameTime(float time);
};
