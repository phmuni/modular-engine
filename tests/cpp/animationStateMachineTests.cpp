// Animation state machines: the cross-fade a transition starts, a manual play, the blend of a state, a state change
// listener, and the machine kept by scenes and prefabs. The Lua side is in tests/lua/animator.lua.

#include "components/animationStateMachine.h"
#include "components/animator.h"
#include "describe.h"
#include "foundation/core/engine.h"
#include "systems/sceneSerializer.h"
#include "testing.h"
#include <filesystem>
#include <fstream>
#include <memory>

namespace {
const std::string kMachine = "../tests/fixtures/animators/machine.json";
const std::string kModel = "models/tentacle/tentacle.glb";
} // namespace

TEST_CASE(animator_transition_cross_fades) {
  GameEntity entity = engine.create("Tentacle", Desc(kModel));
  entity.setAnimator(kMachine);
  const Entity id = entity.id();
  CHECK_EQ(entity.animationState(), std::string("idle"));
  CHECK_EQ(engine.get<Animator>(id).clip, std::string("Sway"));
  CHECK(engine.get<Animator>(id).fadeFrom.empty());

  entity.setParam("speed", 1.0f);
  steps.step(2, [&engine, entity, id]() {
    // The state changed on the first frame; the clips fade over 0.5 s
    CHECK_EQ(entity.animationState(), std::string("walk"));
    const Animator &animator = engine.get<Animator>(id);
    CHECK_EQ(animator.clip, std::string("Twist"));
    CHECK_EQ(animator.fadeFrom, std::string("Sway"));
    CHECK_NEAR(animator.fadeDuration, 0.5f, 1e-5f);
    CHECK_NEAR(animator.speed, 2.0f, 1e-5f);
    CHECK(animator.fadeElapsed < 0.2f);
  });
  steps.seconds(0.6f, [&engine, id]() {
    const Animator &animator = engine.get<Animator>(id);
    CHECK(animator.fadeFrom.empty()); // the fade is over
    CHECK_EQ(animator.clip, std::string("Twist"));
  });
}

TEST_CASE(animator_manual_play_overrides_until_the_next_state_change) {
  GameEntity entity = engine.create("Tentacle", Desc(kModel));
  entity.setAnimator(kMachine);
  const Entity id = entity.id();
  entity.play("Twist", 0.0f);
  CHECK(engine.get<AnimationStateMachine>(id).manual);
  steps.step(5, [&engine, entity, id]() {
    // Still idle for the machine, and the clip played by hand stays
    CHECK_EQ(entity.animationState(), std::string("idle"));
    CHECK_EQ(engine.get<Animator>(id).clip, std::string("Twist"));
    engine.setAnimationParam(id, "speed", 1.0f);
  });
  steps.step(3, [&engine, entity, id]() {
    CHECK_EQ(entity.animationState(), std::string("walk"));
    CHECK(!engine.get<AnimationStateMachine>(id).manual);
    CHECK_EQ(engine.get<Animator>(id).clip, std::string("Twist"));
    engine.setAnimationParam(id, "speed", 0.0f);
  });
  steps.step(3, [&engine, entity, id]() {
    CHECK_EQ(entity.animationState(), std::string("idle"));
    CHECK_EQ(engine.get<Animator>(id).clip, std::string("Sway"));
  });
}

TEST_CASE(animator_blend_state_mixes_two_clips_by_a_parameter) {
  GameEntity entity = engine.create("Tentacle", Desc(kModel));
  entity.setAnimator("../tests/fixtures/animators/blend.json");
  const Entity id = entity.id();
  entity.setParam("speed", 1.0f); // a quarter of the way from Sway (0) to Twist (4)
  steps.step(3, [&engine, id]() {
    const Animator &animator = engine.get<Animator>(id);
    CHECK_EQ(animator.clip, std::string("Sway"));
    CHECK_EQ(animator.blendClip, std::string("Twist"));
    CHECK_NEAR(animator.blendWeight, 0.25f, 1e-5f);
    CHECK(animator.time >= 0.0f && animator.time <= 1.0f); // a phase while blending
  });
}

TEST_CASE(animator_listener_hears_state_changes) {
  auto changes = std::make_shared<std::vector<std::string>>();
  engine.onAnimationStateChanged([changes](const GameEntity &, const std::string &from, const std::string &to) {
    changes->push_back(from + ">" + to);
  });
  GameEntity entity = engine.create("Tentacle", Desc(kModel));
  entity.setAnimator(kMachine);
  entity.trigger("hit");
  steps.step(4, [changes]() {
    CHECK_EQ(changes->size(), size_t(1));
    if (!changes->empty())
      CHECK_EQ((*changes)[0], std::string("idle>hurt"));
  });
}

TEST_CASE(animator_bad_files_do_not_crash) {
  const std::string dir = testing::tempDir("animator");
  {
    std::ofstream(dir + "/garbage.json") << "{ not json at all";
    std::ofstream(dir + "/empty_states.json") << R"({"states": []})";
    std::ofstream(dir + "/bad_blend.json")
        << R"({"states": [{"name": "a", "blend": {"param": "nope", "clips": [{"clip": "x"}]}}]})";
    std::ofstream(dir + "/bad_condition.json")
        << R"({"states": [{"name": "a", "clip": "x"}], "transitions": [{"to": "a", "when": [{"param": "ghost"}]}]})";
  }
  for (const char *name : {"garbage", "empty_states", "bad_blend", "bad_condition", "missing"}) {
    GameEntity entity = engine.create(name, Desc(kModel));
    entity.setAnimator(dir + "/" + name + ".json");
    CHECK(entity.valid());
    CHECK(entity.has("AnimationStateMachine"));
    CHECK_EQ(entity.animationState(), std::string());
    entity.setParam("anything", 1.0f); // kept: with no usable file there is nothing to check the name against
    entity.trigger("anything");
  }
  steps.step(5, [&engine]() { CHECK(engine.find("garbage").valid()); }); // the game went on
}

TEST_CASE(animator_scene_round_trip_keeps_path_and_params) {
  const std::string file = testing::tempDir("animator_scene") + "/scene.json";
  GameEntity entity = engine.create("Tentacle", Desc(kModel));
  entity.setAnimator(kMachine);
  entity.setParam("speed", 0.25f);
  entity.setParam("grounded", false);
  CHECK(engine.saveScene(file));
  entity.destroy();
  engine.loadScene(file);
  steps.until([&engine]() { return !engine.isLoadingScene(); }, "the scene to load", 300);
  steps.step(3, [&engine]() {
    GameEntity loaded = engine.find("Tentacle");
    CHECK(loaded.valid());
    CHECK(loaded.has("AnimationStateMachine"));
    CHECK_EQ(engine.get<AnimationStateMachine>(loaded.id()).path, kMachine);
    CHECK_NEAR(loaded.param("speed"), 0.25f, 1e-5f);
    CHECK_NEAR(loaded.param("grounded"), 0.0f, 1e-5f);
    CHECK_EQ(loaded.animationState(), std::string("idle")); // started in the default state
  });
}

TEST_CASE(animator_prefab_round_trip_and_instance_overrides) {
  const std::string dir = testing::tempDir("animator_prefab");
  const std::string prefab = dir + "/tentacle.json";
  const std::string scene = dir + "/scene.json";
  GameEntity entity = engine.create("Tentacle", Desc(kModel));
  entity.setAnimator(kMachine);
  entity.setParam("speed", 0.25f);
  CHECK(engine.getSceneSerializer().applyInstance(entity.id(), prefab));
  entity.destroy();

  const Entity root = engine.getSceneSerializer().instantiate(prefab)[0];
  CHECK_EQ(engine.get<AnimationStateMachine>(root).path, kMachine);
  CHECK_NEAR(engine.get<AnimationStateMachine>(root).params["speed"], 0.25f, 1e-5f);
  // This instance's own value is an override of the prefab's, and survives in the scene
  engine.get<AnimationStateMachine>(root).params["speed"] = 0.4f;
  CHECK(engine.saveScene(scene));
  engine.loadScene(scene);
  steps.until([&engine]() { return !engine.isLoadingScene(); }, "the scene to load", 300);
  steps.step(2, [&engine]() {
    GameEntity loaded = engine.find("Tentacle");
    CHECK(loaded.valid());
    CHECK_EQ(engine.get<AnimationStateMachine>(loaded.id()).path, kMachine);
    CHECK_NEAR(loaded.param("speed"), 0.4f, 1e-5f);
  });
}
