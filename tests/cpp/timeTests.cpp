// GameTime, GameTween and GameCamera through the C++ API (the Lua side of each is in tests/lua).

#include "describe.h"
#include "foundation/core/engine.h"
#include "testing.h"
#include <memory>

namespace {
constexpr float kFrame = 1.0f / 60.0f;
}

TEST_CASE(time_timers) {
  // Counters the callbacks write to, kept alive by the steps that read them
  struct State {
    int once = 0;
    int ticks = 0;
    int cancelled = 0;
    float firedAt = -1.0f;
  };
  auto state = std::make_shared<State>();
  GameTime &time = engine.time();
  const float start = time.elapsed();
  time.after(0.5f, [state, &time]() {
    ++state->once;
    state->firedAt = time.elapsed();
  });
  const int repeating = time.every(0.25f, [state]() { ++state->ticks; });
  const int doomed = time.after(0.3f, [state]() { ++state->cancelled; });
  time.cancel(doomed);
  time.cancel(9999); // an unknown handle is ignored

  steps.seconds(0.3f, [state]() {
    CHECK_EQ(state->once, 0);
    CHECK_EQ(state->ticks, 1);
  });
  steps.seconds(0.4f, [state, start]() {
    CHECK_EQ(state->once, 1);
    CHECK_NEAR(state->firedAt - start, 0.5f, 2 * kFrame);
    CHECK_EQ(state->ticks, 2);
  });
  steps.seconds(0.5f, [state, &time, repeating]() {
    CHECK_EQ(state->ticks, 4);
    CHECK_EQ(state->once, 1); // once is once
    CHECK_EQ(state->cancelled, 0);
    time.cancel(repeating);
  });
  steps.seconds(0.6f, [state]() { CHECK_EQ(state->ticks, 4); });
}

TEST_CASE(time_scale_and_pause) {
  GameTime &time = engine.time();
  CHECK_NEAR(time.scale(), 1.0f, 1e-6f);
  CHECK(!time.isPaused());
  auto elapsed = std::make_shared<float>(0.0f);

  time.setScale(0.5f);
  CHECK_NEAR(time.scale(), 0.5f, 1e-6f);
  steps.step(1, [&time, elapsed]() { *elapsed = time.elapsed(); });
  steps.step(60, [&time, elapsed]() {
    // 60 frames at half speed is half a second of game time
    CHECK_NEAR(time.elapsed() - *elapsed, 0.5f, 2 * kFrame);
    CHECK_NEAR(time.delta(), kFrame * 0.5f, 1e-5f);
    time.setScale(1.0f);
    time.setPaused(true);
    CHECK(time.isPaused());
    *elapsed = time.elapsed();
  });
  steps.step(30, [&time, elapsed]() {
    CHECK_NEAR(time.elapsed(), *elapsed, 1e-6f); // paused: the clock stands still
    CHECK_NEAR(time.delta(), 0.0f, 1e-6f);
    time.setPaused(false);
  });
  steps.step(30, [&time, elapsed]() { CHECK_NEAR(time.elapsed() - *elapsed, 0.5f, 2 * kFrame); });
}

TEST_CASE(tween_easings) {
  // The curves at a quarter, a half and three quarters
  struct Sample {
    Ease ease;
    float quarter, half, threeQuarters;
  };
  const Sample samples[] = {
      {Ease::Linear, 0.25f, 0.5f, 0.75f},
      {Ease::EaseInQuad, 0.0625f, 0.25f, 0.5625f},
      {Ease::EaseOutQuad, 0.4375f, 0.75f, 0.9375f},
      {Ease::EaseInOutQuad, 0.125f, 0.5f, 0.875f},
      {Ease::EaseInCubic, 0.015625f, 0.125f, 0.421875f},
      {Ease::EaseOutCubic, 0.578125f, 0.875f, 0.984375f},
      {Ease::EaseInOutCubic, 0.0625f, 0.5f, 0.9375f},
  };
  for (const Sample &sample : samples) {
    CHECK_NEAR(applyEase(sample.ease, 0.0f), 0.0f, 1e-6f);
    CHECK_NEAR(applyEase(sample.ease, 0.25f), sample.quarter, 1e-5f);
    CHECK_NEAR(applyEase(sample.ease, 0.5f), sample.half, 1e-5f);
    CHECK_NEAR(applyEase(sample.ease, 0.75f), sample.threeQuarters, 1e-5f);
    CHECK_NEAR(applyEase(sample.ease, 1.0f), 1.0f, 1e-6f);
  }
}

TEST_CASE(tween_entities_and_values) {
  GameEntity e = engine.create("Tweened", Desc("box").at(0, 0, 0));
  auto completed = std::make_shared<int>(0);
  auto last = std::make_shared<float>(-1.0f);

  engine.tween().to(e, TweenProperty::Position, glm::vec3(4, 2, 0), 1.0f, Ease::Linear, [completed]() { ++*completed; });
  engine.tween().to(e, TweenProperty::Scale, glm::vec3(3), 0.5f, Ease::EaseOutQuad);
  engine.tween().to(e, TweenProperty::Rotation, glm::vec3(0, 90, 0), 0.5f);
  engine.tween().to(e, TweenProperty::Color, glm::vec3(0, 1, 0), 0.5f);
  engine.tween().value(10.0f, 20.0f, 0.5f, Ease::Linear, [last](float value) { *last = value; });

  steps.seconds(0.5f, [e, completed, last]() {
    CHECK_NEAR(e.position(), glm::vec3(2, 1, 0), 0.1f); // halfway along a linear tween
    CHECK_EQ(*completed, 0);
    CHECK_NEAR(*last, 20.0f, 1e-4f);
  });
  steps.seconds(0.6f, [e, completed]() {
    CHECK_NEAR(e.position(), glm::vec3(4, 2, 0), 1e-4f);
    CHECK_NEAR(e.scale(), glm::vec3(3), 1e-4f);
    CHECK_NEAR(e.rotation(), glm::vec3(0, 90, 0), 1e-3f);
    CHECK_NEAR(e.color(), glm::vec3(0, 1, 0), 1e-4f);
    CHECK_EQ(*completed, 1);
  });
}

TEST_CASE(tween_cancel_and_destroyed) {
  GameEntity moving = engine.create("Moving", Desc("box"));
  GameEntity doomed = engine.create("Doomed", Desc("box"));
  auto reported = std::make_shared<int>(0);
  const int handle = engine.tween().to(moving, TweenProperty::Position, glm::vec3(10, 0, 0), 1.0f, Ease::Linear,
                                       [reported]() { ++*reported; });
  engine.tween().to(doomed, TweenProperty::Position, glm::vec3(5, 5, 5), 0.5f, Ease::Linear,
                    [reported]() { ++*reported; });
  auto stoppedAt = std::make_shared<float>(0.0f);
  steps.seconds(0.3f, [&engine, moving, doomed, handle, stoppedAt]() mutable {
    engine.tween().cancel(handle);
    *stoppedAt = moving.position().x;
    CHECK(*stoppedAt > 1.0f && *stoppedAt < 5.0f);
    doomed.destroy(); // mid-tween
  });
  steps.seconds(0.8f, [moving, doomed, reported, stoppedAt]() {
    CHECK_NEAR(moving.position().x, *stoppedAt, 1e-6f); // stays where it was cancelled
    CHECK(!doomed.valid()); // not revived by its tween
    CHECK_EQ(*reported, 0); // neither finished
  });
}

TEST_CASE(camera_rig) {
  GameEntity target = engine.create("Target", Desc("box").at(0, 0, 0));
  GameCamera &camera = engine.camera();

  camera.lookAt(glm::vec3(0, 5, 10), glm::vec3(0, 0, 0));
  CHECK_NEAR(camera.position(), glm::vec3(0, 5, 10), 1e-4f);
  CHECK_NEAR(camera.forward(), glm::normalize(glm::vec3(0, -5, -10)), 0.01f);
  camera.setFov(65.0f);
  CHECK_NEAR(camera.fov(), 65.0f, 1e-4f);
  CHECK(!camera.isFollowing());

  // A rigid rig first (the frame after a lookAt belongs to the lookAt), then one that trails
  steps.step(1, [&engine, target]() {
    CameraRig rig;
    rig.offset = glm::vec3(0, 4, 7);
    engine.camera().follow(target, rig);
    CHECK(engine.camera().isFollowing());
  });
  steps.step(2, [&engine]() { CHECK_NEAR(engine.camera().position(), glm::vec3(0, 4, 7), 0.05f); });
  steps.step(1, [&engine, target]() mutable {
    target.setPosition(glm::vec3(10, 0, 0));
    engine.camera().stopFollow();
    CHECK(!engine.camera().isFollowing());
    CameraRig rig;
    rig.offset = glm::vec3(0, 4, 7);
    rig.positionSmoothing = 0.3f;
    rig.rotationSmoothing = 0.3f;
    engine.camera().follow(target, rig);
  });
  // A new rig starts right on its target, so move the target to see it trail
  steps.seconds(1.0f, [&engine, target]() mutable {
    CHECK_NEAR(engine.camera().position(), glm::vec3(10, 4, 7), 0.05f);
    target.setPosition(glm::vec3(20, 0, 0));
  });
  steps.seconds(0.1f, [&engine]() {
    const float x = engine.camera().position().x;
    CHECK(x > 10.5f && x < 19.5f); // on its way
  });
  steps.seconds(3.0f, [&engine]() { CHECK_NEAR(engine.camera().position(), glm::vec3(20, 4, 7), 0.05f); });

  // A lookAt wins for its frame, and the rig takes over again afterwards
  steps.step(1, [&engine]() { engine.camera().lookAt(glm::vec3(50, 5, 5), glm::vec3(50, 0, 0)); });
  steps.step(1, [&engine]() {
    CHECK_NEAR(engine.camera().position(), glm::vec3(50, 5, 5), 0.01f);
    CHECK(engine.camera().isFollowing());
  });
  steps.seconds(3.0f, [&engine]() { CHECK_NEAR(engine.camera().position(), glm::vec3(20, 4, 7), 0.1f); });
}
