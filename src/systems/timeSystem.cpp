// Time system: delta time calculation using SDL ticks.
#include "systems/timeSystem.h"

void TimeSystem::begin() {
  m_startTime = SDL_GetTicks() / 1000.0f;
  m_lastFrameTime = m_startTime;
  m_deltaTime = 0.0f;
}

void TimeSystem::update() {
  float currentTime = SDL_GetTicks() / 1000.0f;
  m_deltaTime = currentTime - m_lastFrameTime;
  m_lastFrameTime = currentTime;
}

float TimeSystem::getDeltaTime() const { return m_deltaTime; }

float TimeSystem::getLastFrameTime() const { return m_lastFrameTime; }

float TimeSystem::getTimeSinceStart() const {
  float now = SDL_GetTicks() / 1000.0f;
  return now - m_startTime;
}

void TimeSystem::setDeltaTime(float dt) { m_deltaTime = dt; }

void TimeSystem::setLastFrameTime(float time) { m_lastFrameTime = time; }
