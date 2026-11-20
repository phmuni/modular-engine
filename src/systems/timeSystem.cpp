
// Time system implementation for managing time-related functionality, including delta time calculation and time
// tracking.

#include "systems/timeSystem.h"
#include <SDL3/SDL_timer.h>
#include <algorithm>

namespace {
constexpr float kMaxDeltaTime = 0.25f;
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
  // A long stall (loading, a debugger, the window held still) is not game time: capped, so nothing jumps afterwards
  m_unscaledDeltaTime = m_fixedStep > 0.0f ? m_fixedStep : std::min(currentTime - m_lastFrameTime, kMaxDeltaTime);
  m_deltaTime = m_paused ? 0.0f : m_unscaledDeltaTime * m_timeScale;
  m_lastFrameTime = currentTime;
}

float TimeSystem::getDeltaTime() const { return m_deltaTime; }

float TimeSystem::getUnscaledDeltaTime() const { return m_unscaledDeltaTime; }

float TimeSystem::getLastFrameTime() const { return m_lastFrameTime; }

float TimeSystem::getTimeSinceStart() const {
  float now = nowSeconds();
  return now - m_startTime;
}

void TimeSystem::setDeltaTime(float dt) { m_deltaTime = dt; }

void TimeSystem::setLastFrameTime(float time) { m_lastFrameTime = time; }
