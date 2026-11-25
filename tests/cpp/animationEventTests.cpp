// Animation events: moments and windows of a state's clip that the game hears of (Engine::onAnimationEvent). That each
// fires once per crossing of the playback clock (loops, several laps in a frame, backwards, blends), that every window
// is closed whatever ends it, what is silent (fading clips, layers that show nothing, manual plays, a pause), what a
// listener may do, and files that are not valid. The Lua side is in tests/lua/animator_events.lua.

#include "components/animationStateMachine.h"
#include "components/animator.h"
#include "describe.h"
#include "foundation/core/engine.h"
#include "systems/animationGraph.h"
#include "systems/animationSystem.h"
#include "systems/sceneSerializer.h"
#include "testing.h"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <nlohmann/json.hpp>

namespace {
const std::string kModel = "models/tentacle/tentacle.glb"; // clips of 2 seconds: Sway and Twist
const std::string kEvents = "../tests/fixtures/animators/events.json";

// What the listener heard, as "name:phase@layer" lines, and what inAnimationWindow said as each came
struct Log {
  std::vector<std::string> lines;
  std::string inside; // one character per line: 1 when the entity was in a window of that name
  size_t count(const std::string &line) const { return std::count(lines.begin(), lines.end(), line); }
  std::string all() const {
    std::string text;
    for (const std::string &line : lines)
      text += (text.empty() ? "" : " ") + line;
    return text;
  }
};

std::shared_ptr<Log> listen(Engine &engine) {
  auto log = std::make_shared<Log>();
  engine.onAnimationEvent([log](const GameEntity &entity, const AnimationEvent &event) {
    static const char *const phases[] = {"fire", "begin", "end"};
    log->lines.push_back(event.name + ":" + phases[static_cast<int>(event.phase)] + "@" + event.layer);
    log->inside += entity.inAnimationWindow(event.name) ? '1' : '0';
  });
  return log;
}

GameEntity tentacle(Engine &engine, const std::string &file = kEvents) {
  GameEntity entity = engine.create("Tentacle", Desc(kModel));
  entity.setAnimator(file);
  return entity;
}

// Writes a machine file; each write gets a modification time an hour later than the last, so the engine sees it as
// changed however quickly it is rewritten
void writeFile(const std::string &file, const std::string &text, int version) {
  std::ofstream(file) << text;
  std::filesystem::last_write_time(file, std::filesystem::file_time_type::clock::now() + std::chrono::hours(version));
}

// An entity in the windowed state, with a window open at 0.6 s (0.4 to 0.8 s of each lap): `act` ends it, and the
// window is then closed exactly once, announced as closed, and nothing more is heard
void windowClosesWhen(Engine &engine, testing::Steps &steps, std::function<void(Engine &, GameEntity)> act) {
  auto log = listen(engine);
  GameEntity entity = tentacle(engine);
  entity.trigger("windowed");
  steps.seconds(0.6f, [log, entity, &engine, act]() mutable {
    CHECK_EQ(log->all(), std::string("w:begin@base"));
    CHECK(entity.inAnimationWindow("w"));
    act(engine, entity);
  });
  steps.step(3, [log, entity]() {
    CHECK_EQ(log->all(), std::string("w:begin@base w:end@base"));
    CHECK(!entity.inAnimationWindow("w"));
    CHECK_EQ(log->inside.back(), '0');
  });
  steps.seconds(3.0f, [log]() { CHECK_EQ(log->lines.size(), size_t(2)); });
}
} // namespace

// --- Moments ------------------------------------------------------------------------------------------------------

TEST_CASE(events_a_clip_that_plays_once_fires_its_moments_and_windows_in_the_order_they_are_crossed) {
  auto log = listen(engine);
  GameEntity entity = tentacle(engine);
  entity.trigger("once");
  steps.seconds(2.6f, [log]() {
    // zero is heard in the first frame the clock moves, one at the end, where a window that ends at 1 closes
    CHECK_EQ(log->all(), std::string("zero:fire@base win:begin@base half:fire@base win:end@base tail:begin@base "
                                     "one:fire@base tail:end@base"));
    // inAnimationWindow says true for a begin and false for an end
    CHECK_EQ(log->inside, std::string("0100100"));
  });
  steps.seconds(2.0f, [log]() { CHECK_EQ(log->lines.size(), size_t(7)); }); // the clip is over: nothing fires again
}

TEST_CASE(events_a_loop_fires_every_lap) {
  auto log = listen(engine);
  GameEntity entity = tentacle(engine);
  entity.trigger("points");
  // 5.5 laps: zero at the start and at each lap's turn (one is the end of the lap, at the same place)
  steps.seconds(11.0f, [log]() {
    CHECK_EQ(log->count("zero:fire@base"), size_t(6));
    CHECK_EQ(log->count("quarter:fire@base"), size_t(6));
    CHECK_EQ(log->count("one:fire@base"), size_t(5));
    CHECK_EQ(log->lines.size(), size_t(17));
    // At the turn of a lap the end of it is heard before the start of the next
    CHECK_EQ(log->lines[0], std::string("zero:fire@base"));
    CHECK_EQ(log->lines[1], std::string("quarter:fire@base"));
    CHECK_EQ(log->lines[2], std::string("zero:fire@base")); // at:0 and at:1 of the same lap: both fire at the turn
    CHECK_EQ(log->lines[3], std::string("one:fire@base"));
  });
}

TEST_CASE(events_a_frame_that_goes_round_more_than_once_fires_for_every_lap) {
  auto log = listen(engine);
  GameEntity entity = tentacle(engine);
  entity.trigger("fast"); // 2.37 laps in each frame
  auto sizes = std::make_shared<std::vector<size_t>>();
  steps.step(3, [log, sizes]() { sizes->push_back(log->lines.size()); });
  for (int i = 0; i < 8; ++i)
    steps.step(1, [log, sizes]() { sizes->push_back(log->lines.size()); });
  steps.step(1, [sizes]() {
    CHECK_EQ(sizes->size(), size_t(9));
    bool several = false;
    for (size_t i = 1; i < sizes->size(); ++i) {
      const size_t heard = (*sizes)[i] - (*sizes)[i - 1];
      CHECK(heard == 2 || heard == 3); // two or three turns of the halfway point in a frame
      several = several || heard >= 2;
    }
    CHECK(several);
    const size_t total = sizes->back() - sizes->front();
    CHECK(total == 18 || total == 19); // 8 frames of 2.37 laps
  });
}

TEST_CASE(events_a_clip_that_plays_backwards_fires_what_it_crosses) {
  auto log = listen(engine);
  GameEntity entity = tentacle(engine);
  entity.trigger("back");
  // A lap takes 2 s, half of it at 1 s; the start counts as reached by the first move, and the turn of a lap is
  // crossed going backwards too
  steps.seconds(5.5f, [log]() {
    CHECK_EQ(log->count("zero:fire@base"), size_t(3));
    CHECK_EQ(log->count("mid:fire@base"), size_t(3));
    CHECK_EQ(log->lines.size(), size_t(6));
  });
}

TEST_CASE(events_a_blend_state_fires_once_per_cycle_not_once_per_clip) {
  auto log = listen(engine);
  GameEntity entity = tentacle(engine);
  entity.setParam("speed", 2.0f); // halfway between the two clips
  entity.trigger("blend");
  steps.seconds(5.5f, [log]() {
    CHECK_EQ(log->count("half:fire@base"), size_t(3));
    CHECK_EQ(log->lines.size(), size_t(3));
  });
}

// --- Windows ------------------------------------------------------------------------------------------------------

TEST_CASE(events_a_window_opens_and_closes_every_lap_and_the_query_follows) {
  auto log = listen(engine);
  GameEntity entity = tentacle(engine);
  entity.trigger("windowed");
  auto inside = std::make_shared<std::string>();
  steps.seconds(0.6f, [entity, inside]() { *inside += entity.inAnimationWindow("w") ? '1' : '0'; });  // 0.4 to 0.8 s
  steps.seconds(0.4f, [entity, inside]() { *inside += entity.inAnimationWindow("w") ? '1' : '0'; });  // 1.0 s
  steps.seconds(1.5f, [entity, inside]() { *inside += entity.inAnimationWindow("w") ? '1' : '0'; });  // 2.5 s
  steps.seconds(3.0f, [log, entity, inside]() {
    CHECK_EQ(*inside, std::string("101"));
    CHECK_EQ(log->count("w:begin@base"), size_t(3)); // 0.4, 2.4 and 4.4 s of 5.5
    CHECK_EQ(log->count("w:end@base"), size_t(3));
    for (size_t i = 0; i < log->lines.size(); ++i) // every begin sees true, every end false
      CHECK_EQ(log->inside[i] == '1', log->lines[i] == "w:begin@base");
    CHECK(!entity.inAnimationWindow("nothing"));
  });
}

TEST_CASE(events_a_frame_that_jumps_over_a_window_gives_its_begin_and_its_end) {
  auto log = listen(engine);
  GameEntity entity = tentacle(engine);
  entity.trigger("jump"); // a quarter of the clip in a frame; the window is 0.3 to 0.4
  steps.seconds(1.0f, [log]() {
    CHECK_EQ(log->all(), std::string("j:begin@base p:fire@base j:end@base"));
    CHECK_EQ(log->inside, std::string("100"));
  });
}

TEST_CASE(events_a_window_is_closed_when_its_state_is_left) {
  windowClosesWhen(engine, steps, [](Engine &, GameEntity entity) mutable { entity.trigger("plain"); });
}

TEST_CASE(events_a_window_is_closed_when_the_machine_is_restarted) {
  windowClosesWhen(engine, steps, [](Engine &, GameEntity entity) mutable { entity.setAnimator(kEvents); });
}

TEST_CASE(events_a_window_is_closed_when_the_machine_is_removed) {
  windowClosesWhen(engine, steps, [](Engine &, GameEntity entity) mutable { entity.setAnimator(""); });
}

TEST_CASE(events_a_window_is_closed_when_the_component_is_removed_by_other_means) {
  windowClosesWhen(engine, steps,
                   [](Engine &engine, GameEntity entity) { engine.removeComponent<AnimationStateMachine>(entity.id()); });
}

TEST_CASE(events_a_window_is_closed_when_a_manual_play_takes_the_base_over) {
  windowClosesWhen(engine, steps, [](Engine &, GameEntity entity) mutable { entity.play("Twist", 0.0f); });
}

TEST_CASE(events_a_window_is_closed_when_the_definition_is_reloaded_without_it) {
  const std::string file = testing::tempDir("events_reload") + "/machine.json";
  const std::string with = R"({"states": [{"name": "a", "clip": "Sway", "events": [
      {"name": "w", "from": 0.2, "to": 0.4}, {"name": "t", "at": 0.5}]}]})";
  writeFile(file, with, 1);
  auto log = listen(engine);
  GameEntity entity = tentacle(engine, file);
  steps.seconds(0.6f, [log, entity, file]() {
    CHECK(entity.inAnimationWindow("w"));
    // The same state without its events
    writeFile(file, R"({"states": [{"name": "a", "clip": "Sway"}]})", 2);
  });
  steps.seconds(1.5f, [log, entity]() {
    CHECK_EQ(log->all(), std::string("w:begin@base w:end@base"));
    CHECK(!entity.inAnimationWindow("w"));
  });
  steps.seconds(3.0f, [log]() { CHECK_EQ(log->lines.size(), size_t(2)); });
}

TEST_CASE(events_a_window_is_closed_when_its_layer_disappears) {
  const std::string file = testing::tempDir("events_layer") + "/machine.json";
  const std::string layer = R"({"name": "up", "states": [{"name": "none"}, {"name": "x", "clip": "Twist",
      "events": [{"name": "w", "from": 0.1, "to": 0.9}]}], "transitions": [{"from": "none", "to": "x",
      "when": [{"param": "go"}]}]})";
  writeFile(file, R"({"params": {"go": "trigger"}, "states": [{"name": "a", "clip": "Sway"}], "layers": [)" + layer + "]}",
            1);
  auto log = listen(engine);
  GameEntity entity = tentacle(engine, file);
  steps.step(2, [entity]() mutable { entity.trigger("go"); });
  steps.seconds(0.5f, [log, entity, file]() {
    CHECK_EQ(log->all(), std::string("w:begin@up"));
    writeFile(file, R"({"params": {"go": "trigger"}, "states": [{"name": "a", "clip": "Sway"}]})", 2);
  });
  steps.seconds(1.5f, [log, entity]() {
    CHECK_EQ(log->all(), std::string("w:begin@up w:end@up"));
    CHECK(!entity.inAnimationWindow("w"));
  });
}

// --- Layers -------------------------------------------------------------------------------------------------------

TEST_CASE(events_come_after_the_state_changes_of_the_frame_the_base_first_then_the_layers) {
  auto log = std::make_shared<Log>();
  engine.onAnimationStateChanged([log](const GameEntity &, const std::string &, const std::string &to,
                                       const std::string &layer) { log->lines.push_back("change:" + to + "@" + layer); });
  engine.onAnimationEvent([log](const GameEntity &, const AnimationEvent &event) {
    static const char *const phases[] = {"fire", "begin", "end"};
    log->lines.push_back(event.name + ":" + phases[static_cast<int>(event.phase)] + "@" + event.layer);
  });
  GameEntity entity = tentacle(engine);
  entity.trigger("once");
  entity.trigger("swing");
  steps.seconds(0.3f, [log]() {
    CHECK_EQ(log->all(), std::string("change:once@base change:swing@upper zero:fire@base lead:fire@upper"));
  });
}

TEST_CASE(events_a_layer_that_shows_nothing_is_silent_and_what_it_had_open_is_closed) {
  auto log = listen(engine);
  GameEntity silent = engine.create("Silent", Desc(kModel));
  silent.setAnimator(kEvents);
  silent.setParam("w", 0.0f);
  silent.trigger("swing");
  steps.seconds(1.0f, [log, silent]() mutable {
    CHECK_EQ(log->lines.size(), size_t(0)); // not the lead, not the window that came and went
    CHECK(!silent.inAnimationWindow("hurt"));
    silent.setParam("w", 1.0f);
  });
  // The clock went on while it was silent: the moment still ahead (0.75 of the clip, at 1.5 s) is heard
  steps.seconds(1.5f, [log]() { CHECK_EQ(log->all(), std::string("tick:fire@upper")); });
}

TEST_CASE(events_a_layer_whose_weight_falls_to_zero_closes_its_window) {
  auto log = listen(engine);
  GameEntity entity = tentacle(engine);
  entity.trigger("swing");
  steps.seconds(0.7f, [log, entity]() mutable { // the hurt window is 0.5 to 1.0 s
    CHECK_EQ(log->all(), std::string("lead:fire@upper hurt:begin@upper"));
    CHECK(entity.inAnimationWindow("hurt"));
    entity.setParam("w", 0.0f);
  });
  steps.step(3, [log, entity]() {
    CHECK_EQ(log->all(), std::string("lead:fire@upper hurt:begin@upper hurt:end@upper"));
    CHECK(!entity.inAnimationWindow("hurt"));
  });
  steps.seconds(0.2f, [log]() { CHECK_EQ(log->lines.size(), size_t(3)); });
}

TEST_CASE(events_an_interrupted_swing_closes_its_window_and_never_fires_what_was_left) {
  auto log = listen(engine);
  GameEntity entity = tentacle(engine);
  entity.trigger("swing");
  steps.seconds(0.7f, [log, entity]() mutable {
    CHECK(entity.inAnimationWindow("hurt"));
    entity.trigger("interrupt"); // into a state with no events, the swing fading out over 0.1 s and beyond
  });
  steps.step(3, [log, entity]() {
    CHECK_EQ(log->all(), std::string("lead:fire@upper hurt:begin@upper hurt:end@upper"));
    CHECK(!entity.inAnimationWindow("hurt"));
  });
  // The tick of the swing (1.5 s) is not heard: the layer is in another state, whatever the fading clip does
  steps.seconds(2.0f, [log]() { CHECK_EQ(log->lines.size(), size_t(3)); });
}

// --- What is silent -----------------------------------------------------------------------------------------------

TEST_CASE(events_a_clip_that_fades_out_is_silent) {
  auto log = listen(engine);
  GameEntity entity = tentacle(engine);
  entity.trigger("points");
  auto heard = std::make_shared<size_t>(0);
  steps.seconds(0.7f, [log, entity, heard]() mutable {
    *heard = log->lines.size();
    CHECK_EQ(*heard, size_t(2)); // zero and quarter
    entity.trigger("plain");     // fades out over a second; its loop would turn at 2 s and cross 0.25 again at 2.5 s
  });
  steps.seconds(2.0f, [log, heard]() { CHECK_EQ(log->lines.size(), *heard); });
}

TEST_CASE(events_a_clip_played_by_hand_is_silent) {
  auto log = listen(engine);
  GameEntity entity = tentacle(engine);
  entity.trigger("points");
  steps.seconds(0.3f, [log, entity]() mutable {
    CHECK_EQ(log->all(), std::string("zero:fire@base"));
    entity.play("Sway", 0.0f);
  });
  steps.seconds(4.5f, [log]() { CHECK_EQ(log->lines.size(), size_t(1)); });
}

TEST_CASE(events_nothing_fires_while_the_clock_stands_still) {
  auto log = listen(engine);
  GameEntity entity = tentacle(engine);
  entity.trigger("points");
  const Entity id = entity.id();
  steps.seconds(0.3f, [&engine, log]() {
    CHECK_EQ(log->all(), std::string("zero:fire@base"));
    engine.time().setPaused(true);
  });
  steps.seconds(1.0f, [&engine, log, id]() { // the quarter, at 0.5 s of the clip, would have come by now
    CHECK_EQ(log->lines.size(), size_t(1));
    engine.time().setPaused(false);
    engine.get<Animator>(id).playing = false;
  });
  steps.seconds(1.0f, [&engine, log, id]() {
    CHECK_EQ(log->lines.size(), size_t(1));
    engine.get<Animator>(id).playing = true;
  });
  steps.seconds(0.4f, [log]() { CHECK_EQ(log->count("quarter:fire@base"), size_t(1)); }); // it goes on from there
}

TEST_CASE(events_a_state_that_does_not_move_its_clock_fires_nothing) {
  auto log = listen(engine);
  GameEntity entity = tentacle(engine);
  entity.trigger("frozen"); // speed 0, with moments at 0 and 1
  steps.seconds(1.0f, [log]() { CHECK_EQ(log->lines.size(), size_t(0)); });
}

TEST_CASE(events_a_clip_the_model_lacks_and_a_model_without_animations_have_none) {
  auto log = listen(engine);
  GameEntity ghost = tentacle(engine);
  ghost.trigger("ghost"); // names a clip the model does not have
  GameEntity box = engine.create("Box", Desc("box"));
  box.setAnimator(kEvents);
  box.trigger("windowed");
  steps.seconds(2.5f, [log, ghost, box]() {
    CHECK_EQ(log->lines.size(), size_t(0));
    CHECK(!ghost.inAnimationWindow("w"));
    CHECK(!box.inAnimationWindow("w"));
  });
}

// --- What a listener may do ---------------------------------------------------------------------------------------

TEST_CASE(events_a_listener_may_destroy_the_entity_it_is_told_of) {
  auto log = std::make_shared<Log>();
  engine.onAnimationEvent([log, &engine](const GameEntity &entity, const AnimationEvent &event) {
    log->lines.push_back(event.name + ":" + std::to_string(static_cast<int>(event.phase)));
    if (event.name == "w")
      engine.destroyEntity(entity.id());
  });
  GameEntity entity = tentacle(engine);
  const Entity id = entity.id();
  entity.trigger("windowed");
  steps.seconds(0.7f, [&engine, log, id]() {
    CHECK(!engine.isAlive(id));
    CHECK_EQ(log->lines.size(), size_t(1)); // only the begin: nobody is told of an entity that is gone
    CHECK(!engine.inAnimationWindow(id, "w"));
  });
  steps.seconds(2.0f, [&engine, log, id]() {
    CHECK_EQ(log->lines.size(), size_t(1));
    CHECK(!engine.inAnimationWindow(id, "w"));
    const auto &system = engine.getSystemManager().getSystem<AnimationSystem>();
    CHECK(system.openWindows(id, "base").empty());
  });
}

TEST_CASE(events_a_listener_may_remove_the_machine_and_hears_the_window_close_in_the_same_frame) {
  auto log = std::make_shared<Log>();
  engine.onAnimationEvent([log, &engine](const GameEntity &entity, const AnimationEvent &event) {
    static const char *const phases[] = {"fire", "begin", "end"};
    log->lines.push_back(event.name + ":" + phases[static_cast<int>(event.phase)]);
    log->inside += entity.inAnimationWindow(event.name) ? '1' : '0';
    if (event.phase == AnimationEvent::Phase::Begin)
      engine.setAnimator(entity.id(), "");
  });
  GameEntity entity = tentacle(engine);
  entity.trigger("windowed");
  steps.seconds(0.7f, [&engine, log, entity]() {
    CHECK_EQ(log->all(), std::string("w:begin w:end"));
    CHECK_EQ(log->inside, std::string("10"));
    CHECK(!entity.inAnimationWindow("w"));
    CHECK(!engine.has<AnimationStateMachine>(entity.id()));
  });
  steps.seconds(2.0f, [log]() { CHECK_EQ(log->lines.size(), size_t(2)); });
}

TEST_CASE(events_a_listener_may_trigger_and_play) {
  auto log = std::make_shared<Log>();
  engine.onAnimationEvent([log](const GameEntity &entity, const AnimationEvent &event) {
    log->lines.push_back(event.name);
    GameEntity changed = entity;
    if (event.name == "zero")
      changed.trigger("windowed");
    if (event.name == "w")
      changed.play("Twist", 0.0f);
  });
  GameEntity entity = tentacle(engine);
  entity.trigger("points");
  steps.seconds(1.0f, [log, entity]() {
    CHECK_EQ(entity.animationState(), std::string("windowed"));
    CHECK(log->lines.size() >= 3);
  });
}

// --- Saved --------------------------------------------------------------------------------------------------------

TEST_CASE(events_scenes_keep_the_machine_and_nothing_of_the_windows_that_are_open) {
  const std::string file = testing::tempDir("events_scene") + "/scene.json";
  GameEntity entity = tentacle(engine);
  entity.trigger("windowed");
  steps.seconds(0.6f, [&engine, entity, file]() {
    CHECK(entity.inAnimationWindow("w"));
    CHECK(testing::saveWholeScene(engine, file));
    std::ifstream in(file);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    CHECK(text.find("clockMoved") == std::string::npos && text.find("announced") == std::string::npos &&
          text.find("closing") == std::string::npos);
    const nlohmann::json scene = nlohmann::json::parse(text);
    bool found = false;
    for (const auto &saved : scene["entities"])
      if (saved["name"] == "Tentacle") {
        found = true;
        CHECK_EQ(saved["components"]["AnimationStateMachine"].size(), size_t(2)); // the file and the parameters
      }
    CHECK(found);
  });
}

// --- Files --------------------------------------------------------------------------------------------------------

namespace {
bool parses(const std::string &events, std::string &error, bool onEmptyLayerState = false) {
  const std::string state = onEmptyLayerState ? R"({"name": "n", "events": )" + events + "}"
                                              : R"({"name": "s", "clip": "Sway", "events": )" + events + "}";
  const std::string text = onEmptyLayerState
                               ? R"({"states": [{"name": "a", "clip": "Sway"}], "layers": [{"name": "l", "states": [)" +
                                     state + "]}]}"
                               : R"({"states": [)" + state + "]}";
  AnimationGraph graph;
  return AnimationGraph::parse(nlohmann::json::parse(text), graph, error);
}
bool mentions(const std::string &error, const std::string &part) { return error.find(part) != std::string::npos; }
} // namespace

TEST_CASE(events_files_with_a_mistake_in_an_event_are_refused_with_the_reason) {
  std::string error;
  CHECK(parses(R"([{"name": "a", "at": 0}, {"name": "b", "at": 1}, {"name": "c", "from": 0, "to": 1}])", error));
  CHECK(parses("[]", error));

  CHECK(!parses(R"([{"at": 0.5}])", error));
  CHECK(mentions(error, "no \"name\""));
  CHECK(mentions(error, "\"s\""));
  CHECK(!parses(R"([{"name": "x"}])", error));
  CHECK(mentions(error, "\"x\"") && mentions(error, "\"at\""));
  CHECK(!parses(R"([{"name": "x", "from": 0.2}])", error));
  CHECK(mentions(error, "\"from\" and \"to\""));
  CHECK(!parses(R"([{"name": "x", "at": 0.5, "from": 0.2, "to": 0.4}])", error));
  CHECK(mentions(error, "both"));
  CHECK(!parses(R"([{"name": "x", "at": 1.5}])", error));
  CHECK(mentions(error, "0 to 1"));
  CHECK(!parses(R"([{"name": "x", "at": -0.1}])", error));
  CHECK(!parses(R"([{"name": "x", "from": 0.2, "to": 2}])", error));
  CHECK(mentions(error, "\"to\""));
  CHECK(!parses(R"([{"name": "x", "at": "half"}])", error));
  CHECK(!parses(R"([{"name": "x", "from": 0.5, "to": 0.5}])", error));
  CHECK(mentions(error, "less than"));
  CHECK(!parses(R"([{"name": "x", "from": 0.6, "to": 0.5}])", error));
  CHECK(!parses(R"({"name": "x", "at": 0.5})", error));
  CHECK(mentions(error, "list"));
  // A layer's state that plays nothing has no clip to put them on
  CHECK(!parses(R"([{"name": "x", "at": 0.5}])", error, true));
  CHECK(mentions(error, "plays nothing") && mentions(error, "layer \"l\""));
}

TEST_CASE(events_a_file_with_a_mistake_in_an_event_leaves_the_entity_without_a_state) {
  const std::string dir = testing::tempDir("events_bad");
  std::ofstream(dir + "/bad.json") << R"({"states": [{"name": "a", "clip": "Sway", "events": [{"name": "x"}]}]})";
  GameEntity entity = tentacle(engine, dir + "/bad.json");
  CHECK_EQ(entity.animationState(), std::string());
}

// A clip that does not loop, started while time stands still, plays once time goes on: it is not taken for finished
TEST_CASE(events_a_clip_started_during_a_pause_plays_after_it) {
  GameEntity entity = engine.create("Tentacle", Desc(kModel));
  engine.setState(Toggle::Paused, true);
  steps.step(2, [entity]() mutable { entity.play("Sway", 0.0f, false); });
  steps.step(5, [&engine, entity]() {
    CHECK(engine.get<Animator>(entity.id()).playing);
    CHECK_NEAR(engine.get<Animator>(entity.id()).time, 0.0f, 1e-6f);
    engine.setState(Toggle::Paused, false);
  });
  steps.step(30, [&engine, entity]() {
    CHECK(engine.get<Animator>(entity.id()).playing);
    CHECK(engine.get<Animator>(entity.id()).time > 0.4f);
  });
}
