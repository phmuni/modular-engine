// Entities the game creates while it runs (Engine::isRuntime) against the scene's: Play and Stop, and saving.

#include "foundation/core/config.h"
#include "foundation/core/engine.h"
#include "systems/scriptSystem.h"
#include "testing.h"
#include <fstream>
#include <memory>
#include <sol/sol.hpp>
#include <sstream>

namespace {
int countNamed(Engine &engine, const char *name) { return static_cast<int>(engine.findAll(name).size()); }

std::string readFile(const std::string &path) {
  std::ifstream stream(path);
  std::stringstream text;
  text << stream.rdbuf();
  return text.str();
}

int starts(Engine &engine) { return engine.scripts().lua()["game"]["starts"].get_or(0); }
} // namespace

// The editor is opened over a running game (its onStart made the entities); Play and Stop must not leave them twice
TEST_CASE(runtime_entities_play_stop_save) {
  // What the editor creates is the scene's (the editor turns this on around its own work)
  engine.getEntityManager().setCreatingRuntime(false);
  engine.createCameraEntity("SceneThing", glm::vec3(0.0f));
  engine.getEntityManager().setCreatingRuntime(true);
  CHECK(engine.scripts().runGameScript(EngineConfig::resolvePath("../tests/fixtures/runtime_game.lua")));
  const std::string folder = testing::tempDir("runtime");
  Engine *game = &engine;

  steps.step(3, [=] {
    CHECK_EQ(starts(*game), 1);
    CHECK_EQ(countNamed(*game, "Spawned"), 2);
    CHECK(game->findAll("Spawned")[0].isRuntime());
    CHECK(!game->find("SceneThing").isRuntime());
    // Path 2: saving leaves out what the game built (the editor's Save Scene)
    CHECK(game->saveScene(folder + "/scene.json"));
    const std::string text = readFile(folder + "/scene.json");
    CHECK(text.find("SceneThing") != std::string::npos);
    CHECK(text.find("Spawned") == std::string::npos);
    game->play();
  });
  steps.step(2, [=] {
    CHECK_EQ(countNamed(*game, "Spawned"), 2); // Play keeps the game going as it is
    CHECK(game->getPlayMode() == Engine::PlayMode::Testing);
    game->stop(); // the test host resumes at once (TestHost::onPlayStopped)
  });
  // Path 1: Stop discards what the game made, and the game starts over (its onStart ran again): two again, not four
  steps.step(3, [=] {
    CHECK(game->getPlayMode() == Engine::PlayMode::Running);
    CHECK_EQ(countNamed(*game, "Spawned"), 2);
    CHECK_EQ(countNamed(*game, "SceneThing"), 1);
    CHECK(game->findAll("Spawned")[0].isRuntime());
    CHECK(game->saveScene(folder + "/again.json"));
    CHECK(readFile(folder + "/again.json").find("Spawned") == std::string::npos);
  });
}

// --save-scene is a picture of what the game built: it keeps the entities the game created
TEST_CASE(runtime_entities_save_scene_after) {
  CHECK(engine.scripts().runGameScript(EngineConfig::resolvePath("../tests/fixtures/runtime_game.lua")));
  const std::string file = testing::tempDir("runtime_after") + "/built.json";
  engine.saveSceneAfter(file, 0.0f, false);
  steps.step(4, [file] {
    const std::string text = readFile(file);
    CHECK(text.find("Spawned") != std::string::npos);
  });
}
