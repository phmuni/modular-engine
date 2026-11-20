#pragma once
// The game API for tweens: animating an entity's position, rotation, scale or color, or any number, over a time with an
// easing curve. Lua's `tween` table is this class (see systems/scriptSystem.cpp), reached from C++ as engine.tween().
//
// Tweens run on game time like timers (GameTime): a paused game, or one in slow motion, holds them. A tween of an
// entity that is destroyed meanwhile stops without calling its completion callback. They are cancelled when a play
// session ends, like timers. Lua has `wait` for sequencing inside a coroutine; C++ has no coroutines, so chain tweens
// and timers with the completion callback and GameTime::after instead.
//
// Tweens set the value each frame before the game's update, so tweening the Position of an entity that physics moves
// (a dynamic body, a character) fights the physics: tween kinematic bodies and plain entities.

#include "foundation/core/gameEntity.h"
#include <functional>
#include <glm/glm.hpp>
#include <memory>
#include <vector>

class Engine;

// The curve a tween follows from start (0) to end (1)
enum class Ease {
  Linear,
  EaseInQuad,
  EaseOutQuad,
  EaseInOutQuad,
  EaseInCubic,
  EaseOutCubic,
  EaseInOutCubic,
};

// Progress t (0 to 1) through the curve; 0 and 1 map to themselves
float applyEase(Ease ease, float t);

enum class TweenProperty { Position, Rotation, Scale, Color };

class GameTween {
public:
  explicit GameTween(Engine &engine) : m_engine(engine) {}

  // Moves a property of the entity from what it is now to `target` over `seconds`, ending exactly on it, then calls
  // `onComplete`. Rotation is in degrees (turned the way given, not the short way), Color needs a model. A tween
  // already running on the same property of the entity is replaced. Returns a handle for cancel (0: nothing started,
  // for a missing entity or a color on an entity without a model).
  int to(const GameEntity &entity, TweenProperty property, const glm::vec3 &target, float seconds,
         Ease ease = Ease::Linear, std::function<void()> onComplete = nullptr);
  // Calls `onValue` each frame with a number going from `from` to `to`, ending exactly on `to`
  int value(float from, float to, float seconds, Ease ease, std::function<void(float)> onValue,
            std::function<void()> onComplete = nullptr);
  // Stops it where it is, without calling onComplete; one that has finished (or never existed) is not an error
  void cancel(int tween);

  // Cancels every tween, for a new play session
  void clear();
  // Once per frame from the engine, with the frame's game time
  void tick(float deltaTime);

private:
  struct Item {
    int id;
    float elapsed;
    float duration;
    Ease ease;
    Entity owner; // -1 for a plain number
    int property; // a TweenProperty, -1 for a plain number
    std::function<void(float)> apply; // given the eased progress
    std::function<void()> onComplete;
    bool cancelled = false; // set while it may be running, so it does not report completing
  };

  void remove(int id);

  Engine &m_engine;
  // Shared pointers: a callback may cancel or start tweens while one of them is being run
  std::vector<std::shared_ptr<Item>> m_items;
  int m_nextId = 1;
};
