// Animation layers: what the poses are (joint matrices of the knight, read back) while a layer with a mask, a weight or
// an additive clip plays over the base, that nothing jumps when a fade starts, ends or is interrupted, triggers shared
// by two layers, files that are not valid, and what a manual play, a restart and a scene save do to the layers.

#include "components/animationStateMachine.h"
#include "components/animator.h"
#include "components/model.h"
#include "describe.h"
#include "foundation/core/engine.h"
#include "rendering/resources/skeletonPose.h"
#include "systems/animationGraph.h"
#include "systems/animationSystem.h"
#include "systems/resourceSystem.h"
#include "systems/sceneSerializer.h"
#include "testing.h"
#include "uiHelpers.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <nlohmann/json.hpp>
#include <set>

namespace {
const std::string kKnight = "models/knight/knight.glb";
const std::string kDir = "../tests/fixtures/animators/";
const std::string kBase = kDir + "knight_base.json";
const std::string kUpper = kDir + "knight_upper.json";

const MeshAnimation &animationOf(Engine &engine, Entity entity) {
  const Model &model = engine.get<Model>(entity);
  return *engine.getSystemManager().getSystem<ResourceSystem>().getMesh(model.meshHandle).getAnimation();
}

// Where each joint is in the model's space (a skin matrix is that times the inverse bind matrix)
std::vector<glm::mat4> globalsOf(const MeshAnimation &animation, const std::vector<glm::mat4> &joints) {
  std::vector<glm::mat4> globals(joints.size());
  for (size_t j = 0; j < joints.size(); ++j)
    globals[j] = joints[j] * glm::inverse(animation.skeleton.inverseBind[j]);
  return globals;
}

std::vector<glm::mat4> globalsOf(Engine &engine, Entity entity) {
  const auto &system = engine.getSystemManager().getSystem<AnimationSystem>();
  const std::vector<glm::mat4> *joints = system.getJointMatrices(entity);
  return joints ? globalsOf(animationOf(engine, entity), *joints) : std::vector<glm::mat4>();
}

int jointNamed(const MeshAnimation &animation, const std::string &name) {
  const Skeleton &skeleton = animation.skeleton;
  for (size_t j = 0; j < skeleton.jointNodes.size(); ++j)
    if (skeleton.nodes[skeleton.jointNodes[j]].name == name)
      return static_cast<int>(j);
  return -1;
}

// A joint's matrix in its parent's space, from the model-space ones (a pose change of an ancestor does not show in it)
glm::mat4 localOf(const MeshAnimation &animation, const std::vector<glm::mat4> &globals, const std::string &name) {
  const Skeleton &skeleton = animation.skeleton;
  const int joint = jointNamed(animation, name);
  const int parent = skeleton.nodes[skeleton.jointNodes[joint]].parent;
  for (size_t j = 0; j < skeleton.jointNodes.size(); ++j)
    if (skeleton.jointNodes[j] == parent)
      return glm::inverse(globals[j]) * globals[joint];
  return globals[joint];
}

float matrixDifference(const glm::mat4 &a, const glm::mat4 &b) {
  float most = 0.0f;
  for (int c = 0; c < 4; ++c)
    for (int r = 0; r < 4; ++r)
      most = std::max(most, std::fabs(a[c][r] - b[c][r]));
  return most;
}

// How far apart two entities' joints are, as the largest distance between a point 0.2 along each named bone
float jointDistance(Engine &engine, Entity a, Entity b, const std::vector<std::string> &names) {
  const MeshAnimation &animation = animationOf(engine, a);
  const std::vector<glm::mat4> first = globalsOf(engine, a), second = globalsOf(engine, b);
  float most = 0.0f;
  for (const std::string &name : names) {
    const int joint = jointNamed(animation, name);
    const glm::vec4 probe(0.0f, 0.2f, 0.0f, 1.0f);
    most = std::max(most, glm::length(glm::vec3(first[joint] * probe) - glm::vec3(second[joint] * probe)));
  }
  return most;
}

const std::vector<std::string> kLegs = {"hips", "upperleg.l", "lowerleg.l", "foot.l", "toes.l",
                                        "upperleg.r", "lowerleg.r", "foot.r", "toes.r"};
const std::vector<std::string> kArms = {"upperarm.l", "lowerarm.l", "hand.l", "upperarm.r", "lowerarm.r", "hand.r"};
std::vector<std::string> allJoints(const MeshAnimation &animation) {
  std::vector<std::string> names;
  for (int node : animation.skeleton.jointNodes)
    names.push_back(animation.skeleton.nodes[node].name);
  return names;
}

// The model-space point of every joint, 0.2 along its bone: a pose in meters, so a difference of two is a movement
std::vector<glm::vec3> probesOf(const std::vector<glm::mat4> &globals) {
  std::vector<glm::vec3> points;
  for (const glm::mat4 &matrix : globals)
    points.push_back(glm::vec3(matrix * glm::vec4(0.0f, 0.2f, 0.0f, 1.0f)));
  return points;
}

float mostMoved(const std::vector<glm::vec3> &before, const std::vector<glm::vec3> &after) {
  float most = 0.0f;
  for (size_t i = 0; i < before.size() && i < after.size(); ++i)
    most = std::max(most, glm::length(after[i] - before[i]));
  return most;
}

// The most any joint moves in one frame (1/60 s) while a clip just plays: what a transition must not exceed by much
float clipFrameStep(const MeshAnimation &animation, const std::string &name) {
  const AnimationClip &clip = animation.clips[animation.findClip(name)];
  std::vector<NodePose> pose;
  std::vector<glm::mat4> globals, joints;
  AnimationSystem::sample(animation, clip, 0.0f, pose);
  SkeletonPose::jointMatrices(animation.skeleton, pose, globals, joints);
  std::vector<glm::vec3> before = probesOf(globalsOf(animation, joints));
  float most = 0.0f;
  for (float time = 1.0f / 60.0f; time <= clip.duration; time += 1.0f / 60.0f) {
    AnimationSystem::sample(animation, clip, time, pose);
    SkeletonPose::jointMatrices(animation.skeleton, pose, globals, joints);
    const std::vector<glm::vec3> after = probesOf(globalsOf(animation, joints));
    most = std::max(most, mostMoved(before, after));
    before = after;
  }
  return most;
}

// Records how far the joints of an entity move each frame, for the steps that follow
struct Motion {
  std::vector<glm::vec3> last;
  float most = 0.0f;
  std::vector<float> history;
  void sample(Engine &engine, Entity entity) {
    const std::vector<glm::vec3> now = probesOf(globalsOf(engine, entity));
    if (!last.empty()) {
      most = std::max(most, mostMoved(last, now));
      history.push_back(mostMoved(last, now));
    }
    last = now;
  }
};
} // namespace

TEST_CASE(layers_a_masked_override_leaves_the_legs_to_the_base) {
  GameEntity plain = engine.create("Plain", Desc(kKnight));
  plain.setAnimator(kBase);
  GameEntity layered = engine.create("Layered", Desc(kKnight));
  layered.setAnimator(kUpper);
  CHECK_EQ(layered.animationState(), std::string("walk"));
  CHECK_EQ(layered.animationLayerState("upper"), std::string("none"));
  CHECK_EQ(layered.animationLayerState("ghost"), std::string());
  CHECK_EQ(layered.animationLayerState("base"), std::string("walk"));

  steps.step(10, [layered]() mutable { layered.trigger("attack"); });
  steps.seconds(0.6f, [&engine, plain, layered]() {
    CHECK_EQ(layered.animationLayerState("upper"), std::string("attack"));
    CHECK_NEAR(jointDistance(engine, plain.id(), layered.id(), kLegs), 0.0f, 1e-4f);
    CHECK(jointDistance(engine, plain.id(), layered.id(), kArms) > 0.02f);
    CHECK(jointDistance(engine, plain.id(), layered.id(), {"spine", "chest"}) > 0.005f);
  });
  // The swing is over (a second long) and the layer has faded out: nothing of it is left
  steps.seconds(1.6f, [&engine, plain, layered]() {
    CHECK_EQ(layered.animationLayerState("upper"), std::string("none"));
    CHECK_NEAR(jointDistance(engine, plain.id(), layered.id(), allJoints(animationOf(engine, plain.id()))), 0.0f,
               1e-4f);
  });
}

TEST_CASE(layers_the_mask_takes_the_nearest_listed_ancestor_and_zero_leaves_a_node_out) {
  GameEntity plain = engine.create("Plain", Desc(kKnight));
  plain.setAnimator(kBase);
  GameEntity headless = engine.create("Headless", Desc(kKnight));
  headless.setAnimator(kDir + "knight_mask_head.json");
  GameEntity full = engine.create("Full", Desc(kKnight));
  full.setAnimator(kUpper);
  GameEntity half = engine.create("Half", Desc(kKnight));
  half.setAnimator(kDir + "knight_mask_half.json");
  for (GameEntity e : {headless, full, half})
    steps.step(0, [e]() mutable { e.trigger("attack"); });

  steps.seconds(0.6f, [&engine, plain, headless, full, half]() {
    const MeshAnimation &animation = animationOf(engine, plain.id());
    const auto base = globalsOf(engine, plain.id());
    const auto noHead = globalsOf(engine, headless.id());
    const auto whole = globalsOf(engine, full.id());
    const auto halved = globalsOf(engine, half.id());
    // head: 0 holds its own pose even though its parent moved with the layer...
    CHECK(matrixDifference(localOf(animation, whole, "chest"), localOf(animation, base, "chest")) > 0.01f);
    CHECK(matrixDifference(localOf(animation, noHead, "chest"), localOf(animation, base, "chest")) > 0.01f);
    CHECK_NEAR(matrixDifference(localOf(animation, noHead, "head"), localOf(animation, base, "head")), 0.0f, 1e-4f);
    // ...where the head of the plain spine mask does follow the clip (it takes the spine's weight)
    CHECK(matrixDifference(localOf(animation, whole, "head"), localOf(animation, base, "head")) > 0.005f);
    // The arms are under the chest, which takes the spine's weight
    CHECK(matrixDifference(localOf(animation, noHead, "upperarm.l"), localOf(animation, base, "upperarm.l")) > 0.01f);
    CHECK_NEAR(matrixDifference(localOf(animation, noHead, "upperleg.l"), localOf(animation, base, "upperleg.l")), 0.0f,
               1e-4f);

    // spine 0.5: the spine turns half as far from the base as with weight 1 (the spine's parent is the hips, which
    // no layer touches, so its local matrix tells)
    const float toFull = matrixDifference(localOf(animation, whole, "spine"), localOf(animation, base, "spine"));
    const float toHalf = matrixDifference(localOf(animation, halved, "spine"), localOf(animation, base, "spine"));
    CHECK(toFull > 0.02f);
    CHECK_NEAR(toHalf, toFull * 0.5f, toFull * 0.1f);
  });
}

TEST_CASE(layers_the_weight_can_come_from_a_parameter) {
  GameEntity plain = engine.create("Plain", Desc(kKnight));
  plain.setAnimator(kBase);
  std::vector<GameEntity> weighted;
  for (float weight : {0.0f, 0.5f, 1.0f}) {
    GameEntity e = engine.create("Weighted", Desc(kKnight));
    e.setAnimator(kDir + "knight_weight.json");
    e.setParam("w", weight);
    weighted.push_back(e);
  }
  steps.step(5, [weighted]() mutable {
    for (GameEntity &e : weighted)
      e.trigger("attack");
  });
  steps.seconds(0.6f, [&engine, plain, weighted]() {
    const MeshAnimation &animation = animationOf(engine, plain.id());
    const auto base = globalsOf(engine, plain.id());
    const float none = matrixDifference(localOf(animation, globalsOf(engine, weighted[0].id()), "spine"),
                                        localOf(animation, base, "spine"));
    const float half = matrixDifference(localOf(animation, globalsOf(engine, weighted[1].id()), "spine"),
                                        localOf(animation, base, "spine"));
    const float whole = matrixDifference(localOf(animation, globalsOf(engine, weighted[2].id()), "spine"),
                                         localOf(animation, base, "spine"));
    CHECK_NEAR(jointDistance(engine, plain.id(), weighted[0].id(), allJoints(animation)), 0.0f, 1e-4f);
    CHECK_NEAR(none, 0.0f, 1e-4f);
    CHECK(whole > 0.02f);
    CHECK_NEAR(half, whole * 0.5f, whole * 0.1f);
  });
}

TEST_CASE(layers_an_additive_layer_adds_what_the_clip_does_beyond_its_first_frame) {
  GameEntity plain = engine.create("Plain", Desc(kKnight));
  plain.setAnimator(kBase);
  GameEntity still = engine.create("Still", Desc(kKnight));   // the clip held on its first frame
  still.setAnimator(kDir + "knight_additive.json");
  GameEntity playing = engine.create("Playing", Desc(kKnight));
  playing.setAnimator(kDir + "knight_additive.json");
  GameEntity off = engine.create("Off", Desc(kKnight));       // weight 0
  off.setAnimator(kDir + "knight_additive.json");
  off.setParam("w", 0.0f);
  GameEntity against = engine.create("Against", Desc(kKnight)); // a reference clip of its own
  against.setAnimator(kDir + "knight_additive.json");
  for (GameEntity e : {still, playing, off})
    e.setParam("still", true);
  against.setParam("against", true);

  steps.step(5, [&engine, plain, still, playing, off, against]() mutable {
    const auto names = allJoints(animationOf(engine, plain.id()));
    CHECK_EQ(still.animationLayerState("add"), std::string("still"));
    // The first frame of the clip is the reference: it adds nothing
    CHECK_NEAR(jointDistance(engine, plain.id(), still.id(), names), 0.0f, 1e-4f);
    // Against another clip's first frame it does add: the difference of the two
    CHECK_EQ(against.animationLayerState("add"), std::string("against"));
    CHECK(jointDistance(engine, plain.id(), against.id(), kArms) > 0.02f);
    playing.setParam("play", true);
    off.setParam("play", true);
  });
  steps.seconds(0.5f, [&engine, plain, playing, off]() {
    const auto names = allJoints(animationOf(engine, plain.id()));
    CHECK_EQ(playing.animationLayerState("add"), std::string("play"));
    CHECK(jointDistance(engine, plain.id(), playing.id(), kArms) > 0.02f);
    // The legs are outside the mask
    CHECK_NEAR(jointDistance(engine, plain.id(), playing.id(), kLegs), 0.0f, 1e-4f);
    // Weight 0 adds nothing, whatever the clip does
    CHECK_EQ(off.animationLayerState("add"), std::string("play"));
    CHECK_NEAR(jointDistance(engine, plain.id(), off.id(), names), 0.0f, 1e-4f);
  });
}

// --- Nothing jumps ------------------------------------------------------------------------------------------------

namespace {
const std::string kMotion = kDir + "knight_motion.json";

// An entity of knight_motion.json whose joint movement per frame is recorded from frame `from` (its machine runs from
// the start); `events` are given as (frame, action)
struct Scenario {
  GameEntity entity;
  std::shared_ptr<Motion> motion = std::make_shared<Motion>();
};
} // namespace

TEST_CASE(layers_fades_do_not_make_the_pose_jump) {
  const MeshAnimation &animation = animationOf(engine, engine.create("Probe", Desc(kKnight)).id());
  // What clips do on their own in a frame: the worst of those a scenario plays is the room it has before a frame counts
  // as a jump. A fade adds a little, the distance between two poses spread over its length.
  auto room = [&animation](std::initializer_list<const char *> clips) {
    float natural = 0.0f;
    for (const char *clip : clips)
      natural = std::max(natural, clipFrameStep(animation, clip));
    return natural * 1.1f;
  };
  const float baseRoom = room({"Idle", "Walking_A", "Running_A", "Dodge_Forward", "Jump_Idle"});
  const float layerRoom = room({"Walking_A", "1H_Melee_Attack_Chop", "Blocking"});

  std::vector<Scenario> scenarios;
  for (int i = 0; i < 5; ++i) { // the last one is the control: nothing happens to it
    Scenario scenario{engine.create("Moving", Desc(kKnight))};
    scenario.entity.setAnimator(kMotion);
    scenarios.push_back(scenario);
  }
  auto firstFrame = std::make_shared<float>(0.0f);
  for (int frame = 0; frame < 260; ++frame)
    steps.step(1, [&engine, scenarios, frame, firstFrame]() mutable {
      for (const Scenario &s : scenarios)
        s.motion->sample(engine, s.entity.id());
      GameEntity a = scenarios[0].entity, b = scenarios[1].entity, c = scenarios[2].entity, d = scenarios[3].entity;
      // a: a layer comes in (empty -> clip) and goes out (clip -> empty)
      if (frame == 30)
        a.trigger("attack");
      // b: the fade in is interrupted by another state, and that one is left too
      if (frame == 30)
        b.trigger("attack");
      if (frame == 36)
        b.setParam("block", true);
      if (frame == 120)
        b.setParam("block", false);
      // c: the base leaves a blend (between walking and running) for a state
      if (frame == 30)
        c.trigger("dodge");
      if (frame == 31)
        *firstFrame = jointDistance(engine, c.id(), scenarios[4].entity.id(),
                                    allJoints(animationOf(engine, c.id())));
      // d: ...and that crossfade is interrupted by another
      if (frame == 30)
        d.trigger("dodge");
      if (frame == 36)
        d.setParam("grounded", false);
    });
  steps.step(1, [scenarios, baseRoom, layerRoom, firstFrame]() {
    const char *names[] = {"a layer in and out", "an interrupted layer fade", "a blend left for a state",
                           "an interrupted crossfade"};
    const float rooms[] = {layerRoom, layerRoom, baseRoom, baseRoom};
    for (size_t i = 0; i < 4; ++i)
      if (scenarios[i].motion->most > rooms[i])
        testing::fail(__FILE__, __LINE__,
                      std::string(names[i]) + ": a joint moved " + std::to_string(scenarios[i].motion->most) +
                          " in a frame, more than " + std::to_string(rooms[i]));
    // The first frame of a fade is still where it was: of a whole blend, not of a clip it leans to
    if (*firstFrame > 0.06f)
      testing::fail(__FILE__, __LINE__,
                    "the first frame of the fade is " + std::to_string(*firstFrame) + " away from the blend it left");
  });
}

TEST_CASE(layers_a_crossfade_fades_out_a_blend_whole_with_its_own_speed) {
  GameEntity entity = engine.create("Moving", Desc(kKnight));
  entity.setAnimator(kMotion);
  entity.setParam("speed", 1.7f);
  const Entity id = entity.id();
  steps.step(20, [entity]() mutable { entity.trigger("dodge"); });
  steps.step(2, [&engine, id]() {
    const Animator &animator = engine.get<Animator>(id);
    CHECK_EQ(animator.clip, std::string("Dodge_Forward"));
    // What it fades out is the whole blend (both clips and their weight), at the speed and loop it had
    CHECK_EQ(animator.fadeFrom, std::string("Walking_A"));
    CHECK_EQ(animator.fadeFromBlend, std::string("Running_A"));
    CHECK(animator.fadeFromBlendWeight > 0.1f && animator.fadeFromBlendWeight < 0.9f);
    CHECK(animator.fadeFromLoop); // the dodge does not loop, the blend did
    CHECK_NEAR(animator.fadeFromSpeed, 1.0f, 1e-5f);
  });
}

// --- Triggers -----------------------------------------------------------------------------------------------------

TEST_CASE(layers_a_trigger_two_layers_wait_for_reaches_both_and_is_used_up_once) {
  auto changes = std::make_shared<std::vector<std::string>>();
  engine.onAnimationStateChanged(
      [changes](const GameEntity &, const std::string &, const std::string &to, const std::string &layer) {
        changes->push_back(layer + ">" + to);
      });
  GameEntity entity = engine.create("Knight", Desc(kKnight));
  entity.setAnimator(kDir + "knight_shared_trigger.json");
  steps.step(3, [entity]() mutable { entity.trigger("go"); });
  steps.step(3, [entity, changes]() {
    CHECK_EQ(entity.animationLayerState("left"), std::string("swing"));
    CHECK_EQ(entity.animationLayerState("right"), std::string("raised"));
    CHECK_NEAR(entity.param("go"), 0.0f, 1e-6f); // used up once both had their turn
    CHECK_EQ(changes->size(), size_t(2));
    if (changes->size() == 2) {
      CHECK_EQ((*changes)[0], std::string("left>swing"));
      CHECK_EQ((*changes)[1], std::string("right>raised"));
    }
  });
}

// --- Files --------------------------------------------------------------------------------------------------------

namespace {
bool parses(const std::string &text, std::string &error) {
  AnimationGraph graph;
  return AnimationGraph::parse(nlohmann::json::parse(text), graph, error);
}
bool mentions(const std::string &error, const std::string &part) { return error.find(part) != std::string::npos; }
} // namespace

TEST_CASE(layers_files_with_a_mistake_in_a_layer_are_refused_with_the_reason) {
  const std::string base = R"("params": {"w": "float", "flag": "bool", "t": "trigger"}, "states": [{"name": "a", "clip": "Idle"}],)";
  auto layer = [&](const std::string &layerBody) { return "{" + base + R"("layers": [)" + layerBody + "]}"; };
  std::string error;

  // A file that is right, with every part a layer has
  CHECK(parses(layer(R"({"name": "up", "mask": {"spine": 0.5}, "weight": "w", "additive": true, "default": "none",
      "states": [{"name": "none"}, {"name": "x", "clip": "Idle", "reference": "Walking_A"}],
      "transitions": [{"from": "none", "to": "x", "when": [{"param": "t"}]}]})"), error));
  CHECK(parses("{" + base + R"("states2": 1})", error)); // no layers at all is as it always was

  CHECK(!parses(layer(R"({"name": "up", "weight": "nope", "states": [{"name": "n"}]})"), error));
  CHECK(mentions(error, "nope"));
  CHECK(!parses(layer(R"({"name": "up", "weight": "flag", "states": [{"name": "n"}]})"), error));
  CHECK(mentions(error, "float parameter"));
  CHECK(!parses(layer(R"({"name": "up", "states": [{"name": "n"}]}, {"name": "up", "states": [{"name": "n"}]})"), error));
  CHECK(mentions(error, "two layers"));
  CHECK(!parses(layer(R"({"name": "base", "states": [{"name": "n"}]})"), error));
  CHECK(mentions(error, "base"));
  CHECK(!parses(layer(R"({"name": "", "states": [{"name": "n"}]})"), error));
  CHECK(mentions(error, "no \"name\""));
  CHECK(!parses(layer(R"({"states": [{"name": "n"}]})"), error));
  CHECK(!parses(layer(R"({"name": "up", "mask": [], "states": [{"name": "n"}]})"), error));
  CHECK(mentions(error, "mask"));
  // A state that plays nothing is for layers only
  CHECK(!parses(R"({"states": [{"name": "empty"}]})", error));
  CHECK(mentions(error, "neither"));
  // A transition stays within its layer: the state of another is no state there
  CHECK(!parses(layer(R"({"name": "one", "states": [{"name": "n"}, {"name": "m", "clip": "Idle"}]},
                         {"name": "two", "states": [{"name": "k"}], "transitions": [{"from": "k", "to": "m"}]})"), error));
  CHECK(mentions(error, "\"m\""));
  CHECK(mentions(error, "layer \"two\""));
  // ...and so is one of the base's
  CHECK(!parses(layer(R"({"name": "one", "states": [{"name": "n"}], "transitions": [{"to": "a"}]})"), error));
}

TEST_CASE(layers_a_file_that_does_not_load_leaves_the_entity_without_a_state) {
  const std::string dir = testing::tempDir("layers_bad");
  std::ofstream(dir + "/bad.json")
      << R"({"states": [{"name": "a", "clip": "Idle"}], "layers": [{"name": "up", "weight": "ghost", "states": [{"name": "n"}]}]})";
  GameEntity entity = engine.create("Knight", Desc(kKnight));
  entity.setAnimator(dir + "/bad.json");
  CHECK_EQ(entity.animationState(), std::string());
  CHECK_EQ(entity.animationLayerState("up"), std::string());
  steps.step(3, [&engine]() { CHECK(engine.find("Knight").valid()); });
}

namespace {
// Writes a machine with these layers; each write gets a modification time an hour later than the last, so the engine
// sees the file as changed however quickly it is rewritten
void writeMachine(const std::string &file, const std::string &layers, int version) {
  std::ofstream(file) << R"({"params": {"go": "trigger"}, "states": [{"name": "a", "clip": "Walking_A"}], "layers": [)"
                      << layers << "]}";
  std::filesystem::last_write_time(file, std::filesystem::file_time_type::clock::now() + std::chrono::hours(version));
}
const std::string kUpperLayer =
    R"({"name": "upper", "states": [{"name": "none"}, {"name": "up", "clip": "Idle", "loop": false}],
        "transitions": [{"from": "none", "to": "up", "when": [{"param": "go"}]}]})";
} // namespace

TEST_CASE(layers_follow_the_file_when_it_changes_on_disk) {
  const std::string file = testing::tempDir("layers_reload") + "/machine.json";
  writeMachine(file, kUpperLayer, 1);
  GameEntity entity = engine.create("Knight", Desc(kKnight));
  entity.setAnimator(file);
  steps.step(2, [entity]() mutable { entity.trigger("go"); });
  steps.step(3, [entity, file]() mutable {
    CHECK_EQ(entity.animationLayerState("upper"), std::string("up"));
    // A layer added in front, the first one kept as it is: layers are matched by name
    writeMachine(file, R"({"name": "lower", "states": [{"name": "rest", "clip": "Idle"}]},)" + kUpperLayer, 2);
  });
  steps.step(80, [entity, file]() mutable {
    CHECK_EQ(entity.animationLayerState("lower"), std::string("rest")); // new: in its default state
    CHECK_EQ(entity.animationLayerState("upper"), std::string("up"));   // old: where it was
    // The first layer renamed, the other dropped
    writeMachine(file, R"({"name": "top", "states": [{"name": "none"}, {"name": "up", "clip": "Idle"}]})", 3);
  });
  steps.step(80, [entity]() {
    CHECK_EQ(entity.animationLayerState("top"), std::string("none"));
    CHECK_EQ(entity.animationLayerState("upper"), std::string());
    CHECK_EQ(entity.animationLayerState("lower"), std::string());
  });
}

// --- Played by hand, restarted, saved ------------------------------------------------------------------------------

TEST_CASE(layers_a_manual_play_moves_only_the_base_and_a_restart_resets_the_layers) {
  GameEntity plain = engine.create("Plain", Desc(kKnight));
  plain.setAnimator(kBase);
  GameEntity layered = engine.create("Layered", Desc(kKnight));
  layered.setAnimator(kUpper);
  steps.step(5, [layered]() mutable { layered.trigger("attack"); });
  steps.seconds(0.5f, [&engine, plain, layered]() mutable {
    CHECK_EQ(layered.animationLayerState("upper"), std::string("attack"));
    plain.play("Idle", 0.0f);
    layered.play("Idle", 0.0f);
    CHECK(engine.get<AnimationStateMachine>(layered.id()).manual);
  });
  steps.seconds(0.3f, [&engine, plain, layered]() mutable {
    // The base is the clip played by hand, with the layer still going over it
    CHECK_EQ(engine.get<Animator>(layered.id()).clip, std::string("Idle"));
    CHECK_EQ(layered.animationLayerState("upper"), std::string("attack"));
    CHECK_NEAR(jointDistance(engine, plain.id(), layered.id(), kLegs), 0.0f, 1e-4f);
    CHECK(jointDistance(engine, plain.id(), layered.id(), kArms) > 0.02f);

    layered.setAnimator(kUpper); // start over, as the plain one does
    plain.setAnimator(kBase);
    CHECK_EQ(layered.animationLayerState("upper"), std::string("none"));
    CHECK(!engine.get<AnimationStateMachine>(layered.id()).manual);
    CHECK_NEAR(engine.get<AnimationStateMachine>(layered.id()).layers[0].presence, 0.0f, 1e-6f);
  });
  steps.step(3, [&engine, plain, layered]() {
    CHECK_EQ(layered.animationState(), std::string("walk"));
    const auto names = allJoints(animationOf(engine, plain.id()));
    CHECK_NEAR(jointDistance(engine, plain.id(), layered.id(), names), 0.0f, 1e-4f);
  });
}

TEST_CASE(layers_play_over_the_rest_pose_when_the_base_clip_is_missing_and_without_animations) {
  const std::string dir = testing::tempDir("layers_missing");
  std::ofstream(dir + "/machine.json")
      << R"({"params": {"go": "trigger"}, "states": [{"name": "a", "clip": "NoSuchClip"}],
             "layers": [{"name": "up", "mask": ["spine"], "states": [{"name": "none"}, {"name": "x", "clip": "Block"}],
                         "transitions": [{"from": "none", "to": "x", "fade": 0, "when": [{"param": "go"}]}]}]})";
  GameEntity rest = engine.create("Rest", Desc(kKnight));
  rest.setAnimator(dir + "/machine.json");
  GameEntity layered = engine.create("Layered", Desc(kKnight));
  layered.setAnimator(dir + "/machine.json");
  // The model without animations still has its machine run
  GameEntity box = engine.create("Box", Desc("box"));
  box.setAnimator(dir + "/machine.json");
  steps.step(3, [layered, box]() mutable {
    layered.trigger("go");
    box.trigger("go");
  });
  steps.step(8, [&engine, rest, layered, box]() {
    CHECK_EQ(layered.animationLayerState("up"), std::string("x"));
    CHECK_EQ(box.animationLayerState("up"), std::string("x"));
    CHECK_EQ(rest.animationLayerState("up"), std::string("none"));
    CHECK(jointDistance(engine, rest.id(), layered.id(), kArms) > 0.01f);
    CHECK_NEAR(jointDistance(engine, rest.id(), layered.id(), kLegs), 0.0f, 1e-4f);
  });
}

TEST_CASE(layers_scenes_keep_the_machine_and_nothing_of_where_its_layers_are) {
  const std::string file = testing::tempDir("layers_scene") + "/scene.json";
  GameEntity entity = engine.create("Knight", Desc(kKnight));
  entity.setAnimator(kUpper);
  steps.step(5, [entity]() mutable { entity.trigger("attack"); });
  steps.seconds(0.4f, [&engine, entity, file]() mutable {
    CHECK_EQ(entity.animationLayerState("upper"), std::string("attack"));
    CHECK(testing::saveWholeScene(engine, file));
    std::ifstream in(file);
    const nlohmann::json scene = nlohmann::json::parse(in);
    bool found = false;
    for (const auto &saved : scene["entities"])
      if (saved["name"] == "Knight") {
        const auto &machine = saved["components"]["AnimationStateMachine"];
        found = true;
        CHECK_EQ(machine.size(), size_t(2)); // the file and the parameters
        CHECK(machine.contains("path") && machine.contains("params"));
        CHECK(!saved["components"]["Animator"].contains("presence"));
      }
    CHECK(found);
    entity.destroy();
    engine.loadScene(file);
  });
  steps.until([&engine]() { return !engine.isLoadingScene(); }, "the scene to load", 300);
  steps.step(3, [&engine]() {
    GameEntity loaded = engine.find("Knight");
    CHECK(loaded.valid());
    CHECK_EQ(loaded.animationState(), std::string("walk"));
    CHECK_EQ(loaded.animationLayerState("upper"), std::string("none")); // not where the layer was: it starts over
    CHECK_NEAR(engine.get<AnimationStateMachine>(loaded.id()).layers[0].presence, 0.0f, 1e-6f);
  });
}

// The example character (assets/scenes/character.json, scripts/knight.lua) played with keys and mouse buttons
TEST_CASE(knight_example_moves_attacks_and_blocks_input) {
  auto key = [](SDL_Scancode scancode, bool down) {
    SDL_Event event{};
    event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    event.key.windowID = testing::ui::windowId();
    event.key.scancode = scancode;
    event.key.down = down;
    SDL_PushEvent(&event);
  };
  auto button = [](int which, bool down) {
    SDL_Event event{};
    event.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
    event.button.windowID = testing::ui::windowId();
    event.button.button = static_cast<Uint8>(which);
    event.button.down = down;
    SDL_PushEvent(&event);
  };
  auto model = [&engine]() { return engine.find("Knight Model"); };
  auto start = std::make_shared<glm::vec3>(0.0f);

  engine.loadScene("../assets/scenes/character.json");
  steps.until([&engine, model]() { return !engine.isLoadingScene() && model().valid(); }, "the scene to load", 300);
  steps.step(40, [&engine, model, key, start]() {
    CHECK_EQ(model().animationState(), std::string("locomotion")); // it stands on the floor
    CHECK_EQ(model().animationLayerState("upper"), std::string("none"));
    *start = engine.find("Knight").position();
    key(SDL_SCANCODE_W, true);
  });
  steps.step(60, [&engine, model, button, start]() {
    const glm::vec3 moved = engine.find("Knight").position() - *start;
    CHECK(moved.z < -1.5f); // away from the camera, which is behind it
    CHECK_NEAR(moved.x, 0.0f, 0.05f);
    CHECK_NEAR(model().param("speed"), 2.84f, 0.01f);
    button(SDL_BUTTON_LEFT, true);
  });
  steps.step(3, [model, button]() {
    button(SDL_BUTTON_LEFT, false);
    CHECK_EQ(model().animationLayerState("upper"), std::string("attack"));
    CHECK_EQ(model().animationState(), std::string("locomotion")); // the legs run on
  });
  steps.step(20, [&engine, model, button, start]() {
    CHECK(engine.find("Knight").position().z < start->z - 2.2f);
    button(SDL_BUTTON_LEFT, true); // again during the swing: the second one
  });
  steps.step(2, [button]() { button(SDL_BUTTON_LEFT, false); });
  steps.until([model]() { return model().animationLayerState("upper") == "attack2"; }, "the second swing", 60);
  steps.until([model]() { return model().animationLayerState("upper") == "none"; }, "the swings to end", 120,
              [key, button]() {
                key(SDL_SCANCODE_W, false);
                button(SDL_BUTTON_RIGHT, true);
              });
  steps.step(5, [model, button]() {
    CHECK_EQ(model().animationLayerState("upper"), std::string("block"));
    button(SDL_BUTTON_LEFT, true); // not behind the shield
  });
  steps.step(3, [button]() {
    button(SDL_BUTTON_LEFT, false);
    button(SDL_BUTTON_RIGHT, false);
  });
  steps.step(5, [model, key]() {
    CHECK_EQ(model().animationLayerState("upper"), std::string("none"));
    key(SDL_SCANCODE_H, true);
  });
  steps.step(3, [model, key]() {
    key(SDL_SCANCODE_H, false);
    CHECK_EQ(model().animationLayerState("hit"), std::string("flinch"));
    key(SDL_SCANCODE_SPACE, true);
  });
  steps.until([model]() { return model().animationState() == "air"; }, "the jump", 20,
              [key]() { key(SDL_SCANCODE_SPACE, false); });
  steps.until([model]() { return model().animationState() == "locomotion"; }, "the landing to end", 180,
              [key]() { key(SDL_SCANCODE_Q, true); });
  steps.step(3, [model, key]() {
    key(SDL_SCANCODE_Q, false);
    CHECK_EQ(model().animationState(), std::string("dodge"));
  });
  steps.until([model]() { return model().animationState() == "locomotion"; }, "the dodge to end", 60,
              [key]() { key(SDL_SCANCODE_K, true); });
  steps.step(3, [model, key]() {
    key(SDL_SCANCODE_K, false);
    CHECK_EQ(model().animationState(), std::string("dead"));
  });
}

// The sword only hurts while the swing's damage window is open (animation events), and only what is within its reach: the
// training dummy of the example scene flinches once per swing that reaches it, falls after three and gets up again
TEST_CASE(knight_example_hits_the_training_dummy_once_per_swing_input) {
  auto key = [](SDL_Scancode scancode, bool down) {
    SDL_Event event{};
    event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    event.key.windowID = testing::ui::windowId();
    event.key.scancode = scancode;
    event.key.down = down;
    SDL_PushEvent(&event);
  };
  auto button = [](bool down) {
    SDL_Event event{};
    event.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
    event.button.windowID = testing::ui::windowId();
    event.button.button = SDL_BUTTON_LEFT;
    event.button.down = down;
    SDL_PushEvent(&event);
  };
  auto knight = [&engine]() { return engine.find("Knight Model"); };
  auto dummy = [&engine]() { return engine.find("Dummy Model"); };
  // What the dummy's hit layer did, and the windows the knight's sword opened and closed
  auto flinches = std::make_shared<int>(0);
  auto windows = std::make_shared<std::string>();
  engine.onAnimationStateChanged([flinches](const GameEntity &entity, const std::string &, const std::string &to,
                                            const std::string &layer) {
    if (entity.name() == "Dummy Model" && layer == "hit" && to == "flinch")
      ++*flinches;
  });
  engine.onAnimationEvent([windows](const GameEntity &entity, const AnimationEvent &event) {
    if (entity.name() == "Knight Model" && event.name == "damage")
      *windows += event.phase == AnimationEvent::Phase::Begin ? "b" : "e";
  });
  // One swing: press, release, and wait for the layer to be back to nothing
  auto swing = [&steps, button, knight]() {
    steps.step(1, [button]() { button(true); });
    steps.step(2, [button]() { button(false); });
    steps.until([knight]() { return knight().animationLayerState("upper") == "attack"; }, "the swing", 10);
    steps.until([knight]() { return knight().animationLayerState("upper") == "none"; }, "the swing to end", 120);
  };

  engine.loadScene("../assets/scenes/character.json");
  steps.until([&engine, knight, dummy]() { return !engine.isLoadingScene() && knight().valid() && dummy().valid(); },
              "the scene to load", 300);
  steps.step(40, [&engine, dummy]() {
    const glm::vec3 apart = engine.find("Dummy").position() - engine.find("Knight").position();
    CHECK(glm::length(apart) > 2.5f); // not touching: a short walk away, in front of the knight (-Z)
    CHECK(apart.z < -2.5f);
    CHECK_EQ(dummy().animationState(), std::string("locomotion"));
    CHECK(engine.find("Dummy").forward().z > 0.9f); // and facing the player, a little off to the side of its way
  });

  // Out of reach: the window opens and closes in the air, nobody is hit
  swing();
  steps.step(5, [flinches, windows]() {
    CHECK_EQ(*windows, std::string("be"));
    CHECK_EQ(*flinches, 0);
  });

  // Walk up to it
  steps.step(1, [key]() { key(SDL_SCANCODE_W, true); });
  steps.until([&engine]() { return engine.find("Knight").position().z < -2.0f; }, "the knight to get close", 240,
              [key]() { key(SDL_SCANCODE_W, false); });
  steps.step(40, [&engine]() {
    const float apart = engine.find("Knight").position().z - engine.find("Dummy").position().z;
    CHECK(apart > 0.8f && apart < 1.8f); // in reach of the sword, not touching
  });

  // One swing, one hit, though the window is open for several frames and the dummy is inside it all of them
  swing();
  steps.step(5, [flinches, windows, dummy]() {
    CHECK_EQ(*windows, std::string("bebe"));
    CHECK_EQ(*flinches, 1);
    CHECK_EQ(dummy().animationState(), std::string("locomotion"));
  });
  swing();
  steps.step(5, [flinches]() { CHECK_EQ(*flinches, 2); });
  // The third takes it down
  swing();
  steps.step(5, [flinches, dummy]() {
    CHECK_EQ(*flinches, 3);
    CHECK_EQ(dummy().animationState(), std::string("dead"));
  });
  // A dummy that is down takes no hits
  swing();
  steps.step(5, [flinches, dummy]() {
    CHECK_EQ(*flinches, 3);
    CHECK_EQ(dummy().animationState(), std::string("dead"));
  });
  // ...and gets up after its few seconds
  steps.until([dummy]() { return dummy().animationState() == "locomotion"; }, "the dummy to get up", 400);
  swing();
  steps.step(5, [flinches]() { CHECK_EQ(*flinches, 4); });
}
