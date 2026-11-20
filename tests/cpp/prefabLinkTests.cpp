// Linked prefab instances (overrides kept through a file change, Apply, Revert, Unpack, scenes with the prefab gone) and
// copying several roots at once, which is what the editor's multi-select duplicate and paste use.

#include "components/model.h"
#include "components/name.h"
#include "components/prefabInstance.h"
#include "describe.h"
#include "foundation/core/engine.h"
#include "systems/sceneSerializer.h"
#include "testing.h"
#include <filesystem>
#include <fstream>
#include <memory>

namespace {
std::vector<Entity> instanceRoots(Engine &engine) {
  std::vector<Entity> roots;
  engine.getComponentManager().forEachComponent<PrefabInstance>([&](Entity entity, PrefabInstance &) { roots.push_back(entity); });
  std::sort(roots.begin(), roots.end());
  return roots;
}

// Edits a prefab file on disk: `edit` changes the parsed JSON
void editFile(const std::string &path, const std::function<void(nlohmann::json &)> &edit) {
  nlohmann::json file;
  {
    std::ifstream in(path);
    file = nlohmann::json::parse(in);
  }
  edit(file);
  std::ofstream out(path);
  out << file.dump(2);
}

struct Fixture {
  std::string file;
  Entity a = -1, b = -1;
};
} // namespace

TEST_CASE(prefab_instances_follow_file_and_keep_overrides) {
  const std::string file = testing::tempDir("prefab_link") + "/crate.json";
  auto state = std::make_shared<Fixture>();
  state->file = file;

  // A parent with two children, saved as a prefab and linked to its file
  GameEntity crate = engine.create("Crate", Desc("box").at(0, 0, 0));
  GameEntity lid = engine.create("Lid", Desc("box").at(0, 1, 0));
  GameEntity latch = engine.create("Latch", Desc("sphere").at(1, 0, 0));
  CHECK(lid.setParent(crate));
  CHECK(latch.setParent(crate));
  auto &serializer = engine.getSceneSerializer();
  CHECK(serializer.applyInstance(crate.id(), file));
  CHECK_EQ(serializer.instanceRoot(lid.id()), crate.id());
  crate.destroy();

  steps.step(1, [&engine, state]() {
    auto &serializer = engine.getSceneSerializer();
    const auto placed = serializer.instantiate(state->file);
    CHECK_EQ(placed.size(), size_t(3));
    state->a = placed[0];
    state->b = serializer.instantiate(state->file)[0];
    engine.get<Transform>(state->b).position = glm::vec3(5, 0, 0);
    CHECK(serializer.instanceOverrides(state->a).empty());

    // b overrides one child's opacity and another's name
    auto children = engine.getChildren(state->b);
    CHECK_EQ(children.size(), size_t(2));
    engine.get<Model>(children[0]).opacity = 0.5f;
    engine.get<Name>(children[1]).name = "Renamed";
    CHECK_EQ(serializer.instanceOverrides(state->b).size(), size_t(2));

    // The file changes: both children get new opacity and position, the root a new name
    editFile(state->file, [](nlohmann::json &file) {
      file["entities"][0]["name"] = "Crate2";
      file["entities"][1]["components"]["Model"]["opacity"] = 0.25;
      file["entities"][2]["components"]["Parent"]["local"]["position"] = nlohmann::json::array({3.0, 0.0, 0.0});
    });
    CHECK_EQ(serializer.refreshPrefabs(), size_t(2)); // both instances updated
  });
  steps.step(1, [&engine, state]() {
    auto &serializer = engine.getSceneSerializer();
    auto kidsA = engine.getChildren(state->a);
    auto kidsB = engine.getChildren(state->b);
    CHECK_EQ(engine.get<Name>(state->a).name, std::string("Crate2"));
    CHECK_NEAR(engine.get<Model>(kidsA[0]).opacity, 0.25f, 1e-5f);
    CHECK_NEAR(engine.get<Model>(kidsB[0]).opacity, 0.5f, 1e-5f); // b's override survives
    CHECK_EQ(engine.get<Name>(kidsB[1]).name, std::string("Renamed"));
    CHECK_NEAR(engine.get<Transform>(kidsA[1]).position.x, engine.get<Transform>(state->a).position.x + 3.0f, 1e-3f);
    CHECK_NEAR(engine.get<Transform>(kidsB[1]).position.x, 5.0f + 3.0f, 1e-3f);
    CHECK_NEAR(engine.get<Transform>(state->b).position, glm::vec3(5, 0, 0), 1e-5f); // the root stays where it is

    // Revert drops b's overrides; Apply writes a's changes to the file and b follows
    serializer.revertInstance(state->b);
    kidsB = engine.getChildren(state->b);
    CHECK_NEAR(engine.get<Model>(kidsB[0]).opacity, 0.25f, 1e-5f);
    CHECK(serializer.instanceOverrides(state->b).empty());
    engine.get<Model>(kidsA[0]).opacity = 0.9f;
    CHECK(serializer.applyInstance(state->a));
    CHECK_NEAR(engine.get<Model>(engine.getChildren(state->b)[0]).opacity, 0.9f, 1e-5f);
    CHECK(serializer.instanceOverrides(state->a).empty());

    // Unpack breaks the link
    serializer.unpackInstance(state->a);
    CHECK_EQ(serializer.instanceRoot(state->a), Entity(-1));
    CHECK_EQ(instanceRoots(engine).size(), size_t(1));
  });
}

TEST_CASE(prefab_scene_keeps_overrides_and_survives_a_missing_file) {
  const std::string dir = testing::tempDir("prefab_scene");
  const std::string prefab = dir + "/post.json";
  const std::string scene = dir + "/scene.json";

  GameEntity post = engine.create("Post", Desc("box").at(0, 0, 0));
  GameEntity cap = engine.create("Cap", Desc("sphere").at(0, 2, 0));
  CHECK(cap.setParent(post));
  CHECK(engine.getSceneSerializer().applyInstance(post.id(), prefab));
  post.destroy();
  const Entity root = engine.getSceneSerializer().instantiate(prefab)[0];
  engine.get<Transform>(root).position = glm::vec3(4, 0, 0);
  engine.get<Model>(engine.getChildren(root)[0]).opacity = 0.4f;
  CHECK(engine.saveScene(scene));

  // The file changes while the scene is not loaded; loading brings the instance up to date, overrides included
  editFile(prefab, [](nlohmann::json &file) { file["entities"][0]["components"]["Model"]["opacity"] = 0.6; });
  engine.loadScene(scene);
  steps.until([&engine]() { return !engine.isLoadingScene(); }, "the scene to load", 300);
  steps.step(1, [&engine, prefab, scene]() {
    auto roots = instanceRoots(engine);
    CHECK_EQ(roots.size(), size_t(1));
    CHECK_NEAR(engine.get<Model>(roots[0]).opacity, 0.6f, 1e-5f); // not overridden: follows the file
    CHECK_NEAR(engine.get<Model>(engine.getChildren(roots[0])[0]).opacity, 0.4f, 1e-5f); // overridden: kept
    CHECK_NEAR(engine.get<Transform>(roots[0]).position, glm::vec3(4, 0, 0), 1e-5f);

    // Without the prefab file the scene's own data stands
    CHECK(engine.saveScene(scene));
    std::filesystem::remove(prefab);
    engine.loadScene(scene);
  });
  steps.until([&engine]() { return !engine.isLoadingScene(); }, "the scene to load again", 300);
  steps.step(1, [&engine]() {
    auto roots = instanceRoots(engine);
    CHECK_EQ(roots.size(), size_t(1));
    CHECK(!engine.getSceneSerializer().prefabFileExists(roots[0]));
    CHECK_EQ(engine.getChildren(roots[0]).size(), size_t(1));
    CHECK_NEAR(engine.get<Model>(roots[0]).opacity, 0.6f, 1e-5f);
    CHECK_NEAR(engine.get<Model>(engine.getChildren(roots[0])[0]).opacity, 0.4f, 1e-5f);
  });
}

namespace {
float fileOpacity(const std::string &path) {
  std::ifstream in(path);
  const auto file = nlohmann::json::parse(in);
  return file["entities"][0]["components"]["Model"].value("opacity", 1.0f);
}
} // namespace

TEST_CASE(prefab_apply_is_undone_with_the_file) {
  const std::string file = testing::tempDir("prefab_undo") + "/lamp.json";
  auto state = std::make_shared<Fixture>();
  state->file = file;

  GameEntity lamp = engine.create("Lamp", Desc("box"));
  CHECK(engine.getSceneSerializer().applyInstance(lamp.id(), file));
  lamp.destroy();
  steps.step(1, [&engine, state]() {
    auto &serializer = engine.getSceneSerializer();
    state->a = serializer.instantiate(state->file)[0];
    state->b = serializer.instantiate(state->file)[0];
    CHECK_NEAR(fileOpacity(state->file), 1.0f, 1e-5f);

    // One edit: change a, then Apply it (what the editor's Apply button does inside its undo step)
    engine.beginCheckpoint();
    engine.get<Model>(state->a).opacity = 0.3f;
    CHECK(serializer.applyInstance(state->a));
    engine.endCheckpoint();
    CHECK_NEAR(fileOpacity(state->file), 0.3f, 1e-5f);
    CHECK_NEAR(engine.get<Model>(state->b).opacity, 0.3f, 1e-5f); // the other instance followed
    CHECK(engine.canUndo());
    engine.undo();
  });
  steps.step(2, [&engine, state]() {
    // The file is as it was, and so is the other instance and the one that was edited
    CHECK_NEAR(fileOpacity(state->file), 1.0f, 1e-5f);
    CHECK_NEAR(engine.get<Model>(state->a).opacity, 1.0f, 1e-5f);
    CHECK_NEAR(engine.get<Model>(state->b).opacity, 1.0f, 1e-5f);
    // Nothing is left to refresh: the cache matches the file
    CHECK_EQ(engine.getSceneSerializer().refreshPrefabs(), size_t(0));
    CHECK(engine.canRedo());
    engine.redo();
  });
  steps.step(2, [&engine, state]() {
    CHECK_NEAR(fileOpacity(state->file), 0.3f, 1e-5f);
    CHECK_NEAR(engine.get<Model>(state->a).opacity, 0.3f, 1e-5f);
    CHECK_NEAR(engine.get<Model>(state->b).opacity, 0.3f, 1e-5f);
    CHECK_EQ(engine.getSceneSerializer().refreshPrefabs(), size_t(0));
    // The file can be edited again after that: the instances follow it
    editFile(state->file, [](nlohmann::json &file) { file["entities"][0]["components"]["Model"]["opacity"] = 0.8; });
    CHECK_EQ(engine.getSceneSerializer().refreshPrefabs(), size_t(2));
    CHECK_NEAR(engine.get<Model>(state->b).opacity, 0.8f, 1e-5f);
  });
}

TEST_CASE(copy_several_roots_at_once) {
  GameEntity a = engine.create("A", Desc("box").at(0, 0, 0));
  GameEntity aChild = engine.create("AChild", Desc("box").at(0, 1, 0));
  GameEntity b = engine.create("B", Desc("sphere").at(5, 0, 0));
  CHECK(aChild.setParent(a));
  const size_t before = engine.getEntityManager().entities().size();

  auto &serializer = engine.getSceneSerializer();
  const std::string text = serializer.copyToJson(std::vector<Entity>{a.id(), b.id()});
  CHECK(!text.empty());
  const auto pasted = serializer.pasteFromJson(text);
  CHECK_EQ(pasted.size(), size_t(3));
  CHECK_EQ(engine.getEntityManager().entities().size(), before + 3);
  int roots = 0;
  for (Entity entity : pasted)
    if (engine.getParent(entity) == -1)
      ++roots;
  CHECK_EQ(roots, 2); // the child kept its parent, the two roots have none
}
