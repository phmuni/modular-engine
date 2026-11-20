#pragma once
// The game API for time: the time scale, pausing, the game clock and timers. Lua's `time` table is this class (see
// systems/scriptSystem.cpp), reached from C++ as engine.time().
//
// Game time is what frames advance by: the real frame time times the scale, and nothing while paused or while the
// editor has the game frozen. Timers run on it, so a paused game keeps its timers where they were.

#include <functional>
#include <vector>

class Engine;

class GameTime {
public:
  explicit GameTime(Engine &engine) : m_engine(engine) {}

  // 1 is normal, 0.5 slow motion, 0 stands still (sounds follow the pitch); never negative
  float scale() const;
  void setScale(float scale);
  // The same pause as F7 and the pause menu
  bool isPaused() const;
  void setPaused(bool paused);
  // Seconds of game time since the game started, and the length of this frame in it
  float elapsed() const { return m_elapsed; }
  float delta() const { return m_delta; }

  // Calls `action` once after `seconds` of game time, or every `seconds`; returns a handle for cancel. Called
  // between frames, so an action can create and destroy entities. Timers belong to the game, not to a scene: they
  // outlive loadScene, and a stopped play session (the editor's Stop) cancels them.
  int after(float seconds, std::function<void()> action);
  int every(float seconds, std::function<void()> action);
  // A timer that has already run (or never existed) is not an error
  void cancel(int timer);

  // Cancels every timer and restarts the clock, for a new play session
  void clear();
  // Once per frame from the engine, with the frame's game time
  void tick(float deltaTime);

private:
  struct Timer {
    int id;
    float remaining;
    float interval; // 0: once
    std::function<void()> action;
  };

  Engine &m_engine;
  std::vector<Timer> m_timers;
  int m_nextId = 1;
  float m_elapsed = 0.0f;
  float m_delta = 0.0f;
};
