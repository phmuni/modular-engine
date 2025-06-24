#include "system/timeSystem.h"

void TimeSystem::start() {
  startTime = SDL_GetTicks() / 1000.0f;
  lastFrameTime = startTime;
  deltaTime = 0.0f;
}

void TimeSystem::update() {
  float currentTime = SDL_GetTicks() / 1000.0f;
  deltaTime = currentTime - lastFrameTime;
  lastFrameTime = currentTime;
}

float TimeSystem::getDeltaTime() const { return deltaTime; }

float TimeSystem::getLastFrameTime() const { return lastFrameTime; }

float TimeSystem::getTimeSinceStart() const {
  float now = SDL_GetTicks() / 1000.0f;
  return now - startTime;
}

void TimeSystem::setDeltaTime(float dt) { deltaTime = dt; }

void TimeSystem::setLastFrameTime(float time) { lastFrameTime = time; }
