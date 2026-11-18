
// Time system implementation for managing time-related functionality, including delta time calculation and time
// tracking.

#include "systems/timeSystem.h"
#include <SDL3/SDL_timer.h>

namespace {
// Nanosecond clock: millisecond ticks would report 0 for frames faster than 1 ms
float nowSeconds() { return static_cast<float>(static_cast<double>(SDL_GetTicksNS()) / 1e9); }
} // namespace

void TimeSystem::begin() {
  m_startTime = nowSeconds();
  m_lastFrameTime = m_startTime;
  m_deltaTime = 0.0f;
}

void TimeSystem::update() {
  float currentTime = nowSeconds();
  m_deltaTime = currentTime - m_lastFrameTime;
  m_lastFrameTime = currentTime;
}

float TimeSystem::getDeltaTime() const { return m_deltaTime; }

float TimeSystem::getLastFrameTime() const { return m_lastFrameTime; }

float TimeSystem::getTimeSinceStart() const {
  float now = nowSeconds();
  return now - m_startTime;
}

void TimeSystem::setDeltaTime(float dt) { m_deltaTime = dt; }

void TimeSystem::setLastFrameTime(float time) { m_lastFrameTime = time; }
