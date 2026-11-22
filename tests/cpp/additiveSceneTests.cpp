// Additive scenes: several scene files open at once, every entity in one of them, one of them the active scene.

#include "describe.h"
#include "foundation/core/config.h"
#include "foundation/core/engine.h"
#include "systems/cameraSystem.h"
#include "systems/sceneSerializer.h"
#include "testing.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>

namespace {
const char *kFixtureA = "../tests/fixtures/additive_a.json";
const char *kFixtureB = "../tests/fixtures/additive_b.json";

// Copies of the fixtures in a folder of their own, so saving never touches the originals
struct Files {
  std::string a, b, c;
};

Files copies(const char *name) {
  const std::string folder = testing::tempDir(name);
  Files files{folder + "/a.json", folder + "/b.json", folder + "/c.json"};
  std::filesystem::copy_file(EngineConfig::resolvePath(kFixtureA), files.a);
  std::filesystem::copy_file(EngineConfig::resolvePath(kFixtureB), files.b);
  // A third scene, with names of its own
  std::ifstream in(EngineConfig::resolvePath(kFixtureB));
  std::stringstream text;
  text << in.rdbuf();
  std::string content = text.str();
  for (size_t at = content.find("\"B "); at != std::string::npos; at = content.find("\"B ", at))
    content.replace(at, 3, "\"C ");
  std::ofstream(files.c) << content;
  return files;
}

std::string readFile(const std::string &path) {
  std::ifstream stream(path);
  std::stringstream text;
  text << stream.rdbuf();
  return text.str();
}

bool has(const std::string &text, const char *name) { return text.find(std::string("\"") + name + "\"") != std::string::npos; }

int sceneId(Engine &engine, const std::string &path) {
  return engine.getEntityManager().findSceneByKey(SceneSerializer::sceneKey(path));
}

int sceneOfName(Engine &engine, const char *name) {
  const Entity entity = engine.findEntity(name);
  return entity == -1 ? -2 : engine.getEntityManager().sceneOf(entity);
}

// What the editor does: the entities made are the scene's
GameEntity makeSceneEntity(Engine &engine, const char *name) {
  engine.getEntityManager().setCreatingRuntime(false);
  GameEntity entity = engine.create(name, Desc());
  engine.getEntityManager().setCreatingRuntime(true);
  return entity;
}

Entity activeCameraEntity(Engine &engine) { return engine.getSystemManager().getSystem<CameraSystem>().getActiveCamera(); }

// Load A, then add B (and wait for both)
void openBoth(Engine &engine, testing::Steps &steps, const Files &files) {
  engine.loadScene(files.a);
  steps.until([&engine] { return !engine.isLoadingScene(); }, "A to load", 300);
  steps.step(0, [&engine, files] { engine.loadSceneAdditive(files.b); });
  steps.until([&engine] { return !engine.isLoadingScene(); }, "B to load", 300);
}

// An edit the history sees
void reparent(Engine &engine, const char *child, const char *parent) {
  engine.beginCheckpoint();
  CHECK(engine.find(child).setParent(engine.find(parent)));
  engine.endCheckpoint();
}
} // namespace

TEST_CASE(additive_scenes_load_and_membership) {
  const Files files = copies("additive_load");
  engine.loadScene(files.a);
  steps.until([&engine] { return !engine.isLoadingScene(); }, "A to load", 300);
  steps.step(0, [&engine, files] {
    CHECK_EQ(engine.loadedScenes().size(), size_t(1));
    CHECK_EQ(engine.getScenePath(), files.a);
    CHECK(!engine.isSceneDirty());
    engine.loadSceneAdditive(files.b);
    engine.loadSceneAdditive(files.b); // asked twice while waiting: once
    CHECK(engine.isLoadingScene());
  });
  steps.until([&engine] { return !engine.isLoadingScene(); }, "B to load", 300);
  steps.step(0, [&engine, files] {
    const std::vector<std::string> open = engine.loadedScenes();
    CHECK_EQ(open.size(), size_t(2));
    CHECK_EQ(open[0], files.a); // in the order they were opened
    CHECK_EQ(open[1], files.b);
    CHECK_EQ(engine.getActiveScene(), files.a);
    CHECK_EQ(engine.getScenePath(), files.a); // keeps meaning the active scene
    const int a = sceneId(engine, files.a), b = sceneId(engine, files.b);
    CHECK(a != -1 && b != -1 && a != b);
    CHECK_EQ(sceneOfName(engine, "A Box"), a);
    CHECK_EQ(sceneOfName(engine, "A Child"), a);
    CHECK_EQ(sceneOfName(engine, "B Box"), b);
    CHECK_EQ(sceneOfName(engine, "B Wall"), b);
    CHECK(!engine.isSceneDirty());
    // Both cameras are marked active in their files; the first scene's keeps the view
    CHECK_EQ(activeCameraEntity(engine), engine.findEntity("A Camera"));
    // The second file's collision layers are in effect
    CHECK(engine.layers().indexOf("zz_additive_layer") != -1);

    // A file already open is not loaded again, not even spelled another way
    engine.loadSceneAdditive(std::filesystem::path(files.b).parent_path().string() + "/./b.json");
    engine.loadSceneAdditive(files.b);
  });
  steps.step(3, [&engine, files] {
    CHECK_EQ(engine.loadedScenes().size(), size_t(2));
    CHECK_EQ(engine.findAll("B Box").size(), size_t(1));

    // Made by the editor: the active scene's
    const int a = sceneId(engine, files.a), b = sceneId(engine, files.b);
    GameEntity first = makeSceneEntity(engine, "Made in A");
    CHECK_EQ(engine.getEntityManager().sceneOf(first.id()), a);
    CHECK(!first.isRuntime());
    CHECK(!engine.setActiveScene("nowhere.json"));
    CHECK(engine.setActiveScene(files.b));
    CHECK_EQ(engine.getScenePath(), files.b);
    GameEntity second = makeSceneEntity(engine, "Made in B");
    CHECK_EQ(engine.getEntityManager().sceneOf(second.id()), b);
    // What the game makes is in none
    GameEntity runtime = engine.create("Game made", Desc());
    CHECK(runtime.isRuntime());
    CHECK_EQ(engine.getEntityManager().sceneOf(runtime.id()), -1);
    // A copy pasted goes to the active scene, children with it
    const std::string text = engine.getSceneSerializer().copyToJson(std::vector<Entity>{engine.findEntity("A Parent")});
    engine.getEntityManager().setCreatingRuntime(false);
    const std::vector<Entity> pasted = engine.getSceneSerializer().pasteFromJson(text);
    engine.getEntityManager().setCreatingRuntime(true);
    CHECK_EQ(pasted.size(), size_t(2));
    for (Entity entity : pasted)
      CHECK_EQ(engine.getEntityManager().sceneOf(entity), b);
  });
}

TEST_CASE(additive_scenes_save_each_file_with_its_own) {
  const Files files = copies("additive_save");
  openBoth(engine, steps, files);
  steps.step(0, [&engine, files] {
    // Moving by parenting: A Box under B's wall goes to B, B Box under A's parent goes to A (and so do their children)
    CHECK(!engine.isSceneDirty());
    reparent(engine, "A Box", "B Wall");
    CHECK(engine.isSceneDirty());
    CHECK_EQ(engine.dirtyScenes().size(), size_t(2)); // both files changed
    reparent(engine, "B Box", "A Parent");
    CHECK_EQ(sceneOfName(engine, "A Box"), sceneId(engine, files.b));
    CHECK_EQ(sceneOfName(engine, "B Box"), sceneId(engine, files.a));
    CHECK(engine.saveAllScenes());
    CHECK(!engine.isSceneDirty());
    CHECK(engine.dirtyScenes().empty());
    const std::string a = readFile(files.a), b = readFile(files.b);
    CHECK(has(a, "A Camera") && has(a, "A Parent") && has(a, "A Child") && has(a, "B Box"));
    CHECK(!has(a, "A Box") && !has(a, "B Wall") && !has(a, "B Camera"));
    CHECK(has(b, "B Camera") && has(b, "B Wall") && has(b, "A Box"));
    CHECK(!has(b, "B Box") && !has(b, "A Parent") && !has(b, "A Camera"));

    // Only the scene that changed is dirty, and saving it alone cleans it
    engine.beginCheckpoint();
    engine.find("B Wall").setPosition(glm::vec3(9, 9, 9));
    engine.endCheckpoint();
    CHECK(engine.isSceneDirty());
    CHECK(engine.isSceneDirty(files.b));
    CHECK(!engine.isSceneDirty(files.a));
    CHECK_EQ(engine.dirtyScenes().size(), size_t(1));
    CHECK(engine.saveOpenScene(files.b));
    CHECK(!engine.isSceneDirty());

    // Save As on the active scene is that scene's; another open scene's file is refused
    CHECK(!engine.saveScene(files.b));
    CHECK(engine.saveScene(files.c));
    CHECK_EQ(engine.getScenePath(), files.c);
    const std::vector<std::string> open = engine.loadedScenes();
    CHECK_EQ(open[0], files.c);
    CHECK(has(readFile(files.c), "A Parent") && !has(readFile(files.c), "B Wall"));
    CHECK(!engine.isSceneDirty());
  });
}

TEST_CASE(additive_scenes_unload) {
  const Files files = copies("additive_unload");
  // B first, so its camera is the view; A joins and does not take it
  engine.loadScene(files.b);
  steps.until([&engine] { return !engine.isLoadingScene(); }, "B to load", 300);
  steps.step(0, [&engine, files] { engine.loadSceneAdditive(files.a); });
  steps.until([&engine] { return !engine.isLoadingScene(); }, "A to load", 300);
  steps.step(0, [&engine, files] {
    CHECK_EQ(activeCameraEntity(engine), engine.findEntity("B Camera"));
    const size_t before = engine.getEntityManager().entities().size();
    CHECK(engine.layers().indexOf("zz_additive_layer") != -1);
    CHECK(!engine.unloadScene("nowhere.json"));
    CHECK(engine.unloadScene(files.b));
    CHECK_EQ(engine.getEntityManager().entities().size(), before); // takes effect at the end of the frame
  });
  steps.step(1, [&engine, files] {
    CHECK_EQ(engine.loadedScenes().size(), size_t(1));
    CHECK_EQ(engine.getScenePath(), files.a); // the active one moved off the scene that went
    CHECK_EQ(engine.findEntity("B Box"), -1);
    CHECK_EQ(engine.findEntity("B Camera"), -1);
    CHECK(engine.findEntity("A Box") != -1);
    CHECK_EQ(engine.getEntityManager().entities().size(), size_t(4));
    // The view moved to the camera that is left
    CHECK_EQ(activeCameraEntity(engine), engine.findEntity("A Camera"));
    CHECK(engine.layers().indexOf("zz_additive_layer") == -1); // its layers went with it
    CHECK(!engine.unloadScene(files.a)); // the only one stays
    engine.loadSceneAdditive(files.b);
  });
  steps.until([&engine] { return !engine.isLoadingScene(); }, "B to load again", 300);
  steps.step(0, [&engine, files] {
    CHECK_EQ(engine.loadedScenes().size(), size_t(2));
    CHECK(engine.setActiveScene(files.b));
    // A parent and its children go together
    CHECK(engine.unloadScene(files.a));
  });
  steps.step(1, [&engine, files] {
    CHECK_EQ(engine.findEntity("A Parent"), -1);
    CHECK_EQ(engine.findEntity("A Child"), -1);
    CHECK_EQ(engine.getScenePath(), files.b);
    CHECK_EQ(engine.getEntityManager().entities().size(), size_t(3));
  });
}

TEST_CASE(additive_scenes_load_scene_replaces_everything) {
  const Files files = copies("additive_replace");
  openBoth(engine, steps, files);
  steps.step(0, [&engine, files] {
    CHECK_EQ(engine.loadedScenes().size(), size_t(2));
    engine.loadScene(files.b);
  });
  steps.until([&engine] { return !engine.isLoadingScene(); }, "B to load", 300);
  steps.step(0, [&engine, files] {
    CHECK_EQ(engine.loadedScenes().size(), size_t(1));
    CHECK_EQ(engine.getScenePath(), files.b);
    CHECK_EQ(engine.findEntity("A Box"), -1);
    CHECK(engine.findEntity("B Box") != -1);
    CHECK_EQ(engine.getEntityManager().entities().size(), size_t(3));
    CHECK(!engine.isSceneDirty());
    // loadScene then loadSceneAdditive in one frame: in that order
    engine.loadScene(files.a);
    engine.loadSceneAdditive(files.b);
  });
  steps.until([&engine] { return !engine.isLoadingScene(); }, "both to load", 300);
  steps.step(0, [&engine, files] {
    const std::vector<std::string> open = engine.loadedScenes();
    CHECK_EQ(open.size(), size_t(2));
    CHECK(open.size() == 2 && open[0] == files.a && open[1] == files.b);
    // A new scene replaces them all too
    engine.newScene();
  });
  steps.step(2, [&engine] {
    CHECK_EQ(engine.loadedScenes().size(), size_t(1));
    CHECK_EQ(engine.getScenePath(), std::string());
    CHECK(!engine.isSceneDirty());
    CHECK_EQ(engine.findEntity("A Box"), -1);
  });
}

TEST_CASE(additive_scenes_play_stop_and_undo) {
  const Files files = copies("additive_play");
  openBoth(engine, steps, files);
  steps.step(0, [&engine, files] {
    CHECK(engine.setActiveScene(files.b));
    engine.play();
  });
  steps.step(2, [&engine, files] {
    CHECK(engine.getPlayMode() == Engine::PlayMode::Testing);
    // While testing: scenes change, entities move between them, the active one changes
    CHECK(engine.setActiveScene(files.a));
    CHECK(engine.find("B Box").setParent(engine.find("A Parent")));
    CHECK_EQ(sceneOfName(engine, "B Box"), sceneId(engine, files.a));
    engine.find("A Box").destroy();
    engine.loadSceneAdditive(files.c);
  });
  steps.until([&engine] { return !engine.isLoadingScene(); }, "C to load", 300);
  steps.step(0, [&engine, files] {
    CHECK_EQ(engine.loadedScenes().size(), size_t(3));
    CHECK(engine.findEntity("C Box") != -1);
    engine.stop(); // the test host resumes at once
  });
  steps.step(3, [&engine, files] {
    CHECK(engine.getPlayMode() == Engine::PlayMode::Running);
    // Everything as at Play: the two scenes, the active one, who belongs to which
    CHECK_EQ(engine.loadedScenes().size(), size_t(2));
    CHECK_EQ(engine.getScenePath(), files.b);
    CHECK_EQ(engine.findEntity("C Box"), -1);
    CHECK_EQ(sceneOfName(engine, "A Box"), sceneId(engine, files.a));
    CHECK_EQ(sceneOfName(engine, "B Box"), sceneId(engine, files.b));
    CHECK_EQ(sceneOfName(engine, "A Child"), sceneId(engine, files.a));
    CHECK(!engine.isSceneDirty());

    // Undo and redo keep membership
    reparent(engine, "A Box", "B Wall");
    CHECK_EQ(sceneOfName(engine, "A Box"), sceneId(engine, files.b));
    CHECK(engine.setActiveScene(files.a)); // not an edit: undo leaves it
    engine.undo();
  });
  steps.step(2, [&engine, files] {
    CHECK_EQ(sceneOfName(engine, "A Box"), sceneId(engine, files.a));
    CHECK_EQ(engine.getScenePath(), files.a);
    CHECK_EQ(engine.loadedScenes().size(), size_t(2));
    CHECK(!engine.isSceneDirty()); // back to what is saved
    engine.redo();
  });
  steps.step(2, [&engine, files] {
    CHECK_EQ(sceneOfName(engine, "A Box"), sceneId(engine, files.b));
    CHECK(engine.isSceneDirty(files.a) && engine.isSceneDirty(files.b));
    CHECK_EQ(engine.getScenePath(), files.a);
  });
}
