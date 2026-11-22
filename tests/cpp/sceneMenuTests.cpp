// The editor's File menu: the names Save As accepts, the recent list, saving under a name, New Scene, and the menu
// bar taking the top of the window. The dialogs themselves are not driven (they are ImGui popups); what they call is.

#include "components/camera.h"
#include "components/light.h"
#include "describe.h"
#include "foundation/core/config.h"
#include "foundation/core/engine.h"
#include "systems/editor/sceneFiles.h"
#include "systems/editorUISystem.h"
#include "systems/stateSystem.h"
#include "testing.h"
#include <filesystem>
#include <memory>
#include <random>

namespace {
EditorUISystem &editorOf(Engine &engine) { return engine.getSystemManager().getSystem<EditorUISystem>(); }

// What the engine's tests write into assets/scenes is removed again, under a name no other run shares
struct SavedScene {
  std::string name = "zz_menu_test_" + std::to_string(std::random_device{}());
  std::string path() const { return EngineConfig::resolvePath(std::string(EngineConfig::SCENES_DIR) + "/" + name + ".json"); }
  ~SavedScene() {
    std::error_code ignored;
    std::filesystem::remove(path(), ignored);
  }
};

// An entity of the scene, as the editor makes them (the game's own are not saved, so changing one is not an unsaved
// change)
GameEntity sceneBox(Engine &engine) {
  engine.getEntityManager().setCreatingRuntime(false);
  GameEntity box = engine.create("Box", Desc("box"));
  engine.getEntityManager().setCreatingRuntime(true);
  return box;
}

// One edit the history sees, so the scene is dirty
void makeDirty(Engine &engine, GameEntity &entity) {
  engine.beginCheckpoint();
  entity.setPosition(glm::vec3(3.0f, 0.0f, 0.0f));
  engine.endCheckpoint();
}
} // namespace

TEST_CASE(scene_file_names) {
  std::string error;
  CHECK_EQ(sceneFiles::fileName("level", &error), std::string("level.json"));
  CHECK_EQ(sceneFiles::fileName("  level.json ", &error), std::string("level.json"));
  CHECK_EQ(sceneFiles::fileName("Level.JSON", &error), std::string("Level.JSON"));
  CHECK_EQ(sceneFiles::fileName("level.", &error), std::string("level.json"));
  CHECK_EQ(sceneFiles::fileName("my level 2", &error), std::string("my level 2.json"));
  for (const char *bad : {"", "   ", ".json", "../x", "..", "a/b", "a\\b", "/etc/x", "C:x", "x..y", ".hidden", "a?b", "a|b"}) {
    error.clear();
    CHECK(sceneFiles::fileName(bad, &error).empty());
    CHECK(!error.empty());
  }
}

TEST_CASE(scene_recent_list) {
  std::vector<std::string> recent;
  sceneFiles::pushRecent(recent, "a");
  sceneFiles::pushRecent(recent, "b");
  sceneFiles::pushRecent(recent, "c");
  CHECK_EQ(recent.size(), size_t(3));
  CHECK_EQ(recent[0], std::string("c")); // newest first
  sceneFiles::pushRecent(recent, "a"); // again: moves up, no duplicate
  CHECK_EQ(recent.size(), size_t(3));
  CHECK_EQ(recent[0], std::string("a"));
  CHECK_EQ(recent[2], std::string("b"));
  for (int i = 0; i < 20; ++i)
    sceneFiles::pushRecent(recent, "s" + std::to_string(i));
  CHECK_EQ(recent.size(), sceneFiles::kMaxRecent);
  CHECK_EQ(recent[0], std::string("s19"));
}

TEST_CASE(scene_save_as_writes_under_assets_scenes) {
  auto scene = std::make_shared<SavedScene>();
  auto box = std::make_shared<GameEntity>(sceneBox(engine));
  auto &editor = editorOf(engine);
  makeDirty(engine, *box);
  CHECK(engine.isSceneDirty());

  // Names that would leave assets/scenes write nothing and change nothing
  std::string error;
  CHECK(!editor.saveSceneAs(engine, "../" + scene->name, &error));
  CHECK(!error.empty());
  CHECK(!std::filesystem::exists(scene->path()));
  CHECK(engine.isSceneDirty());
  CHECK(editor.recentScenes().empty());

  CHECK(editor.saveSceneAs(engine, scene->name, &error));
  CHECK(std::filesystem::exists(scene->path()));
  CHECK(!engine.isSceneDirty()); // saved
  CHECK_EQ(EngineConfig::relativePath(EngineConfig::resolvePath(engine.getScenePath())),
           std::string(EngineConfig::SCENES_DIR) + "/" + scene->name + ".json");
  CHECK_EQ(editor.recentScenes().size(), size_t(1));

  // Save writes back to the same file, and the recent list does not repeat it
  std::filesystem::remove(scene->path());
  editor.requestSave(engine);
  CHECK(std::filesystem::exists(scene->path()));
  CHECK_EQ(editor.recentScenes().size(), size_t(1));
  steps.step(1, [scene, box]() {}); // keeps the file's owner alive until the test ends
}

TEST_CASE(scene_new_scene_has_a_camera_and_no_file) {
  auto scene = std::make_shared<SavedScene>();
  auto box = std::make_shared<GameEntity>(sceneBox(engine));
  auto &editor = editorOf(engine);
  CHECK(editor.saveSceneAs(engine, scene->name));
  CHECK(!engine.getScenePath().empty());
  makeDirty(engine, *box);

  // Unsaved changes: New asks first, and nothing happens until it is answered
  editor.requestScene(engine, EditorUISystem::SceneAction::Kind::New);
  CHECK(editor.hasPendingSceneAction());
  engine.newScene(); // as the dialog's "Start new without saving" does
  steps.step(2, [&engine, scene, box]() {
    CHECK(engine.getScenePath().empty());
    CHECK(!engine.isSceneDirty());
    CHECK(!engine.isLoadingScene());
    CHECK(engine.getActiveCamera() != nullptr);
    CHECK(!engine.isAlive(box->id()));
    size_t cameras = 0, directional = 0, all = 0;
    for (Entity entity : engine.getEntityManager().entities()) {
      ++all;
      cameras += engine.getComponentManager().has<Camera>(entity) ? 1 : 0;
      if (const Light *light = engine.getComponentManager().tryGet<Light>(entity))
        directional += light->type == LightType::Directional ? 1 : 0;
    }
    CHECK_EQ(cameras, size_t(1));
    CHECK_EQ(directional, size_t(1));
    CHECK_EQ(all, size_t(3)); // the camera, the sun and a Sky holding the scene ambient (lights have none of their own)
    // The scene's own entities, so saving it keeps them
    CHECK(!engine.isRuntime(*engine.getEntityManager().entities().begin()));
  });
}

TEST_CASE(scene_menu_bar_takes_the_top_of_the_window) {
  engine.getSystemManager().getSystem<StateSystem>().setToggle(Toggle::ShowUI, true);
  steps.step(6, [&engine]() {
    auto &editor = editorOf(engine);
    const glm::vec4 rect = editor.gameViewRect();
    CHECK(rect.y > 4.0f); // below the bar
    // Maximized: the bar goes with the panels and the game gets the whole window
    editor.setMaximized(true);
  });
  steps.step(4, [&engine]() {
    const glm::vec4 rect = editorOf(engine).gameViewRect();
    CHECK_NEAR(rect.x, 0.0f, 0.01f);
    CHECK_NEAR(rect.y, 0.0f, 0.01f);
  });
}
