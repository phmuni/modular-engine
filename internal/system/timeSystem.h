#pragma once

#include <SDL3/SDL.h>

class TimeSystem {
private:
  float deltaTime = 0.0f;
  float lastFrameTime = 0.0f;
  float startTime = 0.0f;

public:
  TimeSystem() = default;
  ~TimeSystem() = default;

  void start();  // Start timer
  void update(); // Updates deltaTime

  // Getters
  float getDeltaTime() const;
  float getLastFrameTime() const;
  float getTimeSinceStart() const; // Time since start

  // Setters
  void setDeltaTime(float dt);
  void setLastFrameTime(float time);
};
