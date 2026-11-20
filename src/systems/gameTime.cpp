// The game API for time: see gameTime.h.

#include "systems/gameTime.h"

#include "foundation/core/engine.h"
#include "systems/timeSystem.h"
#include <algorithm>

float GameTime::scale() const { return m_engine.getSystemManager().getSystem<TimeSystem>().getTimeScale(); }

void GameTime::setScale(float scale) { m_engine.getSystemManager().getSystem<TimeSystem>().setTimeScale(scale); }

bool GameTime::isPaused() const { return m_engine.getState(Toggle::Paused); }

void GameTime::setPaused(bool paused) { m_engine.setState(Toggle::Paused, paused); }

int GameTime::after(float seconds, std::function<void()> action) {
  m_timers.push_back({m_nextId, std::max(seconds, 0.0f), 0.0f, std::move(action)});
  return m_nextId++;
}

int GameTime::every(float seconds, std::function<void()> action) {
  // Not 0: that would run it every frame, and more than once if the clock allowed
  const float interval = std::max(seconds, 0.001f);
  m_timers.push_back({m_nextId, interval, interval, std::move(action)});
  return m_nextId++;
}

void GameTime::cancel(int timer) {
  m_timers.erase(std::remove_if(m_timers.begin(), m_timers.end(), [&](const Timer &t) { return t.id == timer; }),
                 m_timers.end());
}

void GameTime::tick(float deltaTime) {
  m_delta = deltaTime;
  m_elapsed += deltaTime;
  if (m_timers.empty())
    return;
  std::vector<int> due;
  for (Timer &timer : m_timers) {
    timer.remaining -= deltaTime;
    if (timer.remaining <= 0.0f)
      due.push_back(timer.id);
  }
  // By handle, since an action may add or cancel timers (any of these included)
  for (int id : due) {
    auto it = std::find_if(m_timers.begin(), m_timers.end(), [&](const Timer &t) { return t.id == id; });
    if (it == m_timers.end())
      continue;
    std::function<void()> action = it->action;
    if (it->interval > 0.0f) {
      it->remaining += it->interval; // a long frame catches up one run per frame, not all at once
      if (it->remaining <= 0.0f)
        it->remaining = it->interval;
    }
    else
      m_timers.erase(it);
    action();
  }
}

void GameTime::clear() {
  m_timers.clear();
  m_elapsed = 0.0f;
}
