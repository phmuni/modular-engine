// Nested prefabs: a prefab file holding instances of other prefabs, as references with the overrides it makes to them.
// Files built here live in a temporary folder; the fixtures (tests/fixtures/prefabs) are only read.

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
using nlohmann::json;

const char *kStreet = "../tests/fixtures/prefabs/street.json";
const char *kCycleA = "../tests/fixtures/prefabs/cycle_a.json";
const char *kCycleSelf = "../tests/fixtures/prefabs/cycle_self.json";

struct Files {
  std::string dir, door, house, scene;
};

// The entity named `name` at or below `root`; -1 if none
Entity named(Engine &engine, Entity root, const std::string &name) {
  std::vector<Entity> pending{root};
  for (size_t i = 0; i < pending.size(); ++i) {
    const Name *entityName = engine.tryGet<Name>(pending[i]);
    if (entityName && entityName->name == name)
      return pending[i];
    for (Entity child : engine.getChildren(pending[i]))
      pending.push_back(child);
  }
  return -1;
}

float opacity(Engine &engine, Entity entity) { return engine.get<Model>(entity).opacity; }

json readJson(const std::string &path) {
  std::ifstream in(path);
  return json::parse(in);
}

void editFile(const std::string &path, const std::function<void(json &)> &edit) {
  json file = readJson(path);
  edit(file);
  std::ofstream out(path);
  out << file.dump(2);
}

std::string readText(const std::string &path) {
  std::ifstream in(path, std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// The entry of a prefab file that holds a nested instance (the first one)
size_t nestedEntry(const json &file) {
  for (size_t i = 0; i < file["entities"].size(); ++i)
    if (file["entities"][i]["components"].contains("PrefabInstance"))
      return i;
  return file["entities"].size();
}

std::vector<Entity> roots(Engine &engine) {
  std::vector<Entity> found;
  engine.getComponentManager().forEachComponent<PrefabInstance>([&](Entity entity, PrefabInstance &) { found.push_back(entity); });
  std::sort(found.begin(), found.end());
  return found;
}

// door.json: Door (a box) with a Handle. house.json: House with a Wall and a Door instance whose opacity it sets to 0.5.
Files makeHouse(Engine &engine, const std::string &name) {
  Files files;
  files.dir = testing::tempDir(name);
  files.door = files.dir + "/door.json";
  files.house = files.dir + "/house.json";
  files.scene = files.dir + "/scene.json";
  auto &serializer = engine.getSceneSerializer();

  GameEntity door = engine.create("Door", Desc("box"));
  GameEntity handle = engine.create("Handle", Desc("sphere").at(0, 1, 0));
  CHECK(handle.setParent(door));
  CHECK(serializer.applyInstance(door.id(), files.door));
  door.destroy();

  const Entity inner = serializer.instantiate(files.door)[0];
  engine.get<Model>(inner).opacity = 0.5f;
  GameEntity house = engine.create("House", Desc("box").at(0, 0, 0));
  GameEntity wall = engine.create("Wall", Desc("box").at(0, 0, 3));
  CHECK(wall.setParent(house));
  engine.get<Transform>(inner).position = glm::vec3(2, 0, 0);
  CHECK(engine.setParent(inner, house.id()));
  CHECK(serializer.applyInstance(house.id(), files.house));
  house.destroy(); // takes its children, the door instance too
  return files;
}
} // namespace

TEST_CASE(nested_prefab_is_saved_as_a_reference) {
  const Files files = makeHouse(engine, "nested_save");
  const json house = readJson(files.house);
  // House, Wall and the door: the door's own Handle is not copied into the house file
  CHECK_EQ(house["entities"].size(), size_t(3));
  const size_t at = nestedEntry(house);
  CHECK(at < house["entities"].size());
  const json &link = house["entities"][at]["components"];
  CHECK(link["PrefabInstance"]["path"].get<std::string>().find("door.json") != std::string::npos);
  CHECK(link["PrefabInstance"].contains("overrides")); // the opacity this prefab sets
  CHECK(!link.contains("Model"));
  CHECK(link.contains("Parent"));
  CHECK_EQ(readText(files.house).find("Handle"), std::string::npos);

  // Instantiating brings the door's entities in, linked at both levels
  auto &serializer = engine.getSceneSerializer();
  const auto placed = serializer.instantiate(files.house);
  CHECK_EQ(placed.size(), size_t(4)); // House, Wall, Door, Handle
  const Entity top = placed[0];
  const Entity door = named(engine, top, "Door");
  const Entity handle = named(engine, top, "Handle");
  CHECK(door != -1);
  CHECK(handle != -1);
  CHECK_EQ(engine.getParent(handle), door);
  CHECK_EQ(serializer.instanceRoot(handle), door);
  CHECK_EQ(serializer.instanceRoot(door), door);
  CHECK_EQ(serializer.instanceRoot(named(engine, top, "Wall")), top);
  CHECK_EQ(serializer.enclosingInstance(door), top);
  CHECK_EQ(serializer.enclosingInstance(top), Entity(-1));
  CHECK_EQ(roots(engine).size(), size_t(2));
  CHECK_NEAR(opacity(engine, door), 0.5f, 1e-5f); // the house's override of the door
  CHECK_NEAR(opacity(engine, handle), 1.0f, 1e-5f);
  CHECK(serializer.instanceOverrides(top).empty());
  CHECK(serializer.instanceOverrides(door).empty());

  // spawn moves everything with the root
  const Entity moved = engine.instantiate(files.house, glm::vec3(10, 0, 0));
  CHECK_NEAR(engine.get<Transform>(named(engine, moved, "Handle")).position.x, 12.0f, 1e-3f);
}

TEST_CASE(nested_prefab_follows_an_inner_file_edit) {
  const Files files = makeHouse(engine, "nested_follow");
  auto &serializer = engine.getSceneSerializer();
  const Entity a = serializer.instantiate(files.house)[0];
  const Entity b = serializer.instantiate(files.house)[0];
  const Entity alone = serializer.instantiate(files.door)[0];
  // b changes the handle of its door; a changes nothing
  engine.get<Model>(named(engine, b, "Handle")).opacity = 0.2f;
  CHECK_EQ(serializer.instanceOverrides(b).size(), size_t(1));

  editFile(files.door, [](json &file) {
    file["entities"][0]["components"]["Model"]["opacity"] = 0.9;
    file["entities"][1]["components"]["Model"]["opacity"] = 0.6;
  });
  CHECK_EQ(serializer.refreshPrefabs(), size_t(3)); // both houses and the door on its own
  CHECK_NEAR(opacity(engine, named(engine, a, "Handle")), 0.6f, 1e-5f);
  CHECK_NEAR(opacity(engine, named(engine, b, "Handle")), 0.2f, 1e-5f); // b's override stays
  CHECK_NEAR(opacity(engine, named(engine, a, "Door")), 0.5f, 1e-5f);   // the house sets the door's opacity: not the file
  CHECK_NEAR(opacity(engine, alone), 0.9f, 1e-5f);
  CHECK(serializer.instanceOverrides(a).empty());

  // A part new in the inner file shows up in the houses too
  editFile(files.door, [](json &file) {
    json knob = file["entities"][1];
    knob["name"] = "Knob";
    file["entities"].push_back(knob);
  });
  CHECK_EQ(serializer.refreshPrefabs(), size_t(3));
  CHECK(named(engine, a, "Knob") != -1);
  CHECK_EQ(serializer.instanceRoot(named(engine, b, "Knob")), named(engine, b, "Door"));
  CHECK(serializer.instanceOverrides(a).empty());
  CHECK_EQ(serializer.refreshPrefabs(), size_t(0));
}

TEST_CASE(nested_prefab_overrides_stack_in_three_layers) {
  const Files files = makeHouse(engine, "nested_layers");
  auto &serializer = engine.getSceneSerializer();
  const Entity a = serializer.instantiate(files.house)[0];
  const Entity b = serializer.instantiate(files.house)[0];
  const Entity alone = serializer.instantiate(files.door)[0];
  const Entity doorA = named(engine, a, "Door"), doorB = named(engine, b, "Door");
  // door file 1.0 < the house's 0.5 < the scene's 0.2 on a's door
  engine.get<Model>(doorA).opacity = 0.2f;
  CHECK_NEAR(opacity(engine, doorB), 0.5f, 1e-5f);
  CHECK_NEAR(opacity(engine, alone), 1.0f, 1e-5f);
  CHECK_EQ(serializer.instanceOverrides(a).size(), size_t(1)); // seen from the house instance...
  CHECK_EQ(serializer.instanceOverrides(doorA).size(), size_t(1)); // ...and from the nested one

  // The door file changes: only what no layer above sets follows
  editFile(files.door, [](json &file) { file["entities"][0]["components"]["Model"]["opacity"] = 0.9; });
  serializer.refreshPrefabs();
  CHECK_NEAR(opacity(engine, doorA), 0.2f, 1e-5f);
  CHECK_NEAR(opacity(engine, doorB), 0.5f, 1e-5f);
  CHECK_NEAR(opacity(engine, alone), 0.9f, 1e-5f);

  // The house stops overriding it: b follows the door file now, a keeps the scene's value
  const size_t doorAt = nestedEntry(readJson(files.house));
  editFile(files.house, [](json &file) { file["entities"][nestedEntry(file)]["components"]["PrefabInstance"].erase("overrides"); });
  CHECK_EQ(serializer.refreshPrefabs(), size_t(2));
  CHECK_NEAR(opacity(engine, doorA), 0.2f, 1e-5f);
  CHECK_NEAR(opacity(engine, doorB), 0.9f, 1e-5f);

  // And a scene saves the layers it owns: only a's change, addressed by the nested key
  CHECK(testing::saveWholeScene(engine, files.scene));
  const json scene = readJson(files.scene);

  int withOverrides = 0;
  for (const json &entry : scene["entities"]) {
    const json &components = entry["components"];
    if (components.contains("PrefabInstance") && !components["PrefabInstance"].value("overrides", json::array()).empty()) {
      ++withOverrides;
      CHECK_EQ(components["PrefabInstance"]["overrides"].size(), size_t(1));
      const std::string path = components["PrefabInstance"]["overrides"][0]["path"].get<std::string>();
      CHECK(path.rfind("/" + std::to_string(doorAt) + "/components/Model", 0) == 0); // the house's entry of the door
    }
  }
  CHECK_EQ(withOverrides, 1);
}

TEST_CASE(nested_prefab_scene_round_trip) {
  const size_t base = engine.getEntityManager().entities().size(); // what the engine starts with
  const Files files = makeHouse(engine, "nested_scene");
  auto &serializer = engine.getSceneSerializer();
  const Entity a = serializer.instantiate(files.house)[0];
  engine.get<Transform>(a).position = glm::vec3(5, 0, 0);
  engine.get<Model>(named(engine, a, "Door")).opacity = 0.2f;
  engine.get<Model>(named(engine, a, "Handle")).opacity = 0.3f;
  engine.get<Model>(named(engine, a, "Wall")).opacity = 0.7f;
  CHECK(testing::saveWholeScene(engine, files.scene));

  // The scene holds everything in full, plus the links
  const json scene = readJson(files.scene);
  CHECK_EQ(scene["entities"].size(), base + 4);
  bool outerPart = false;
  for (const json &entry : scene["entities"])
    if (entry["components"].contains("PrefabPart") && entry["components"]["PrefabPart"].contains("outer"))
      outerPart = true;
  CHECK(outerPart);

  // The inner file changes while the scene is not loaded
  editFile(files.door, [](json &file) {
    file["entities"][1]["components"]["Model"]["opacity"] = 0.6;
    file["entities"][1]["name"] = "Grip";
  });
  engine.loadScene(files.scene);
  steps.until([&engine]() { return !engine.isLoadingScene(); }, "the scene to load", 300);
  steps.step(1, [&engine, files, base]() {
    auto &serializer = engine.getSceneSerializer();
    const auto all = roots(engine);
    CHECK_EQ(all.size(), size_t(2));
    const Entity top = all[0];
    CHECK_EQ(serializer.enclosingInstance(all[1]), top);
    CHECK_NEAR(engine.get<Transform>(top).position.x, 5.0f, 1e-5f);
    const Entity door = named(engine, top, "Door");
    CHECK_NEAR(opacity(engine, door), 0.2f, 1e-5f);                    // the scene's override, kept
    CHECK_NEAR(opacity(engine, named(engine, top, "Wall")), 0.7f, 1e-5f);
    const Entity grip = named(engine, top, "Grip"); // the inner file's change reached the loaded scene
    CHECK(grip != -1);
    CHECK_EQ(named(engine, top, "Handle"), Entity(-1));
    CHECK_NEAR(opacity(engine, grip), 0.3f, 1e-5f); // renamed by the file; its opacity is the scene's
    CHECK_EQ(serializer.instanceOverrides(top).size(), size_t(3)); // the door's, the handle's and the wall's opacity
    CHECK_EQ(engine.getEntityManager().entities().size(), base + 4);

    // Both files gone: the scene's own data stands, nested entities included
    CHECK(testing::saveWholeScene(engine, files.scene));
    std::filesystem::remove(files.door);
    engine.loadScene(files.scene);
  });
  steps.until([&engine]() { return !engine.isLoadingScene(); }, "the scene to load without the door", 300);
  steps.step(1, [&engine, files, base]() {
    const auto all = roots(engine);
    CHECK_EQ(all.size(), size_t(2));
    const Entity top = all[0];
    CHECK_EQ(engine.getEntityManager().entities().size(), base + 4);
    CHECK_NEAR(opacity(engine, named(engine, top, "Door")), 0.2f, 1e-5f);
    CHECK_NEAR(opacity(engine, named(engine, top, "Grip")), 0.3f, 1e-5f);
    CHECK_NEAR(opacity(engine, named(engine, top, "Wall")), 0.7f, 1e-5f);
    CHECK_EQ(engine.getSceneSerializer().instanceOverrides(top).size(), size_t(1)); // only the wall: the door is not known
    CHECK(testing::saveWholeScene(engine, files.scene));
    std::filesystem::remove(files.house);
    engine.loadScene(files.scene);
  });
  steps.until([&engine]() { return !engine.isLoadingScene(); }, "the scene to load without either", 300);
  steps.step(1, [&engine, base]() {
    const auto all = roots(engine);
    CHECK_EQ(all.size(), size_t(2));
    CHECK_EQ(engine.getEntityManager().entities().size(), base + 4);
    CHECK_NEAR(opacity(engine, named(engine, all[0], "Door")), 0.2f, 1e-5f);
    CHECK_NEAR(opacity(engine, named(engine, all[0], "Wall")), 0.7f, 1e-5f);
  });
}

namespace {
struct ApplyState {
  Files files;
  Entity a = -1, b = -1;
  std::string doorBefore, houseBefore;
};
} // namespace

TEST_CASE(nested_prefab_apply_to_inner_and_outer_with_undo) {
  auto state = std::make_shared<ApplyState>();
  state->files = makeHouse(engine, "nested_apply");
  steps.step(1, [&engine, state]() {
    auto &serializer = engine.getSceneSerializer();
    state->a = serializer.instantiate(state->files.house)[0];
    state->b = serializer.instantiate(state->files.house)[0];
    state->doorBefore = readText(state->files.door);
    state->houseBefore = readText(state->files.house);
    const Entity doorA = named(engine, state->a, "Door");
    engine.get<Model>(doorA).opacity = 0.2f;
    engine.get<Model>(named(engine, state->a, "Handle")).opacity = 0.3f;

    // Apply on the nested root writes the door file only
    engine.beginCheckpoint();
    CHECK(serializer.applyInstance(doorA));
    engine.endCheckpoint();
    CHECK_EQ(readText(state->files.house), state->houseBefore);
    CHECK(readText(state->files.door) != state->doorBefore);
    const json door = readJson(state->files.door);
    CHECK_NEAR(door["entities"][0]["components"]["Model"]["opacity"].get<float>(), 0.2f, 1e-5f);
    CHECK_NEAR(door["entities"][1]["components"]["Model"]["opacity"].get<float>(), 0.3f, 1e-5f);
    CHECK_EQ(serializer.enclosingInstance(doorA), state->a); // still part of the house instance
    CHECK_NEAR(opacity(engine, named(engine, state->b, "Handle")), 0.3f, 1e-5f); // the other house follows
    CHECK_NEAR(opacity(engine, named(engine, state->b, "Door")), 0.5f, 1e-5f);   // its own house override holds
    CHECK(engine.canUndo());
    engine.undo();
  });
  steps.step(2, [&engine, state]() {
    // The door file and everything that followed it are back
    CHECK_EQ(readText(state->files.door), state->doorBefore);
    CHECK_EQ(readText(state->files.house), state->houseBefore);
    CHECK_NEAR(opacity(engine, named(engine, state->b, "Handle")), 1.0f, 1e-5f);
    CHECK_NEAR(opacity(engine, named(engine, state->a, "Handle")), 0.3f, 1e-5f); // the scene's own change is back too
    CHECK_EQ(engine.getSceneSerializer().refreshPrefabs(), size_t(0));
    CHECK(engine.canRedo());
    engine.redo();
  });
  steps.step(2, [&engine, state]() {
    CHECK(readText(state->files.door) != state->doorBefore);
    CHECK_NEAR(opacity(engine, named(engine, state->b, "Handle")), 0.3f, 1e-5f);
    CHECK_EQ(engine.getSceneSerializer().refreshPrefabs(), size_t(0));
    engine.undo();
  });
  steps.step(2, [&engine, state]() {
    // Apply on the outer root writes the house file: the door's differences become the house's override of it
    auto &serializer = engine.getSceneSerializer();
    CHECK_EQ(readText(state->files.door), state->doorBefore);
    CHECK_NEAR(opacity(engine, named(engine, state->a, "Handle")), 0.3f, 1e-5f);
    engine.beginCheckpoint();
    CHECK(serializer.applyInstance(state->a));
    engine.endCheckpoint();
    CHECK_EQ(readText(state->files.door), state->doorBefore); // the door file is not touched
    const json house = readJson(state->files.house);
    CHECK_EQ(house["entities"].size(), size_t(3));
    CHECK(house["entities"][nestedEntry(house)]["components"]["PrefabInstance"]["overrides"].size() >= size_t(2));
    CHECK_NEAR(opacity(engine, named(engine, state->b, "Door")), 0.2f, 1e-5f); // the other house follows
    CHECK_NEAR(opacity(engine, named(engine, state->b, "Handle")), 0.3f, 1e-5f);
    CHECK(serializer.instanceOverrides(state->a).empty());
    engine.undo();
  });
  steps.step(2, [&engine, state]() {
    CHECK_EQ(readText(state->files.house), state->houseBefore);
    CHECK_NEAR(opacity(engine, named(engine, state->b, "Door")), 0.5f, 1e-5f);
    CHECK_NEAR(opacity(engine, named(engine, state->b, "Handle")), 1.0f, 1e-5f);
    CHECK_EQ(engine.getSceneSerializer().refreshPrefabs(), size_t(0));
    engine.redo();
  });
  steps.step(2, [&engine, state]() {
    CHECK(readText(state->files.house) != state->houseBefore);
    CHECK_NEAR(opacity(engine, named(engine, state->b, "Door")), 0.2f, 1e-5f);
    CHECK_EQ(engine.getSceneSerializer().refreshPrefabs(), size_t(0));
  });
}

TEST_CASE(nested_prefab_revert_and_unpack_at_each_level) {
  const Files files = makeHouse(engine, "nested_revert");
  auto &serializer = engine.getSceneSerializer();
  const Entity a = serializer.instantiate(files.house)[0];
  const Entity doorA = named(engine, a, "Door");
  engine.get<Model>(doorA).opacity = 0.2f;
  engine.get<Model>(named(engine, a, "Handle")).opacity = 0.3f;
  engine.get<Model>(named(engine, a, "Wall")).opacity = 0.7f;
  CHECK_EQ(serializer.instanceOverrides(a).size(), size_t(3));
  CHECK_EQ(serializer.instanceOverrides(doorA).size(), size_t(2));

  // Revert on the nested root: back to what the house makes of the door (0.5), the wall untouched
  serializer.revertInstance(doorA);
  CHECK_NEAR(opacity(engine, doorA), 0.5f, 1e-5f);
  CHECK_NEAR(opacity(engine, named(engine, a, "Handle")), 1.0f, 1e-5f);
  CHECK_NEAR(opacity(engine, named(engine, a, "Wall")), 0.7f, 1e-5f);
  CHECK(serializer.instanceOverrides(doorA).empty());
  CHECK_EQ(serializer.instanceOverrides(a).size(), size_t(1));

  // Revert on the outer root: everything below it too, down to the file (door file + the house's override)
  engine.get<Model>(doorA).opacity = 0.1f;
  engine.get<Model>(named(engine, a, "Handle")).opacity = 0.3f;
  serializer.revertInstance(a);
  CHECK_NEAR(opacity(engine, doorA), 0.5f, 1e-5f);
  CHECK_NEAR(opacity(engine, named(engine, a, "Handle")), 1.0f, 1e-5f);
  CHECK_NEAR(opacity(engine, named(engine, a, "Wall")), 1.0f, 1e-5f);
  CHECK(serializer.instanceOverrides(a).empty());

  // Unpack on the nested root: its entities become plain, the house keeps going without it
  const Entity b = serializer.instantiate(files.house)[0];
  const Entity doorB = named(engine, b, "Door"), handleB = named(engine, b, "Handle");
  serializer.unpackInstance(doorB);
  CHECK_EQ(serializer.instanceRoot(doorB), Entity(-1));
  CHECK_EQ(serializer.instanceRoot(handleB), Entity(-1));
  CHECK_EQ(serializer.instanceRoot(named(engine, b, "Wall")), b);
  CHECK(engine.getEntityManager().isAlive(handleB));
  CHECK_EQ(roots(engine).size(), size_t(3)); // a, a's door, b
  editFile(files.door, [](json &file) { file["entities"][1]["components"]["Model"]["opacity"] = 0.4; });
  serializer.refreshPrefabs();
  CHECK_NEAR(opacity(engine, named(engine, a, "Handle")), 0.4f, 1e-5f);
  CHECK_NEAR(opacity(engine, handleB), 1.0f, 1e-5f); // no longer follows

  // Unpack on the outer root: its own parts become plain; the door instance stays linked to its file, on its own
  const Entity c = serializer.instantiate(files.house)[0];
  const Entity doorC = named(engine, c, "Door");
  serializer.unpackInstance(c);
  CHECK_EQ(serializer.instanceRoot(c), Entity(-1));
  CHECK_EQ(serializer.instanceRoot(named(engine, c, "Wall")), Entity(-1));
  CHECK_EQ(serializer.instanceRoot(doorC), doorC);
  CHECK_EQ(serializer.enclosingInstance(doorC), Entity(-1));
  CHECK_EQ(serializer.instanceRoot(named(engine, c, "Handle")), doorC);
  editFile(files.door, [](json &file) { file["entities"][1]["components"]["Model"]["opacity"] = 0.15; });
  serializer.refreshPrefabs();
  CHECK_NEAR(opacity(engine, named(engine, c, "Handle")), 0.15f, 1e-5f);
  CHECK_NEAR(opacity(engine, doorC), 0.5f, 1e-5f); // what the house had set stays with it: it is its own now
}

TEST_CASE(nested_prefab_cycles_are_refused) {
  // Files that contain themselves, directly or through another: not placed, and no endless recursion
  const size_t baseline = engine.getEntityManager().entities().size();
  CHECK(engine.getSceneSerializer().instantiate(kCycleSelf).empty());
  CHECK(engine.getSceneSerializer().instantiate(kCycleA).empty());
  CHECK_EQ(engine.getEntityManager().entities().size(), baseline);

  // Saving one is refused: a house inside the house
  const Files files = makeHouse(engine, "nested_cycle");
  auto &serializer = engine.getSceneSerializer();
  const Entity a = serializer.instantiate(files.house)[0];
  const Entity inner = serializer.instantiate(files.house)[0];
  CHECK(engine.setParent(inner, named(engine, a, "Wall")));
  const std::string before = readText(files.house);
  CHECK(!serializer.applyInstance(a));
  CHECK(!serializer.savePrefab(a, files.house));
  CHECK_EQ(readText(files.house), before);

  // ...and through another prefab: the door holding a house, which holds the door
  const Entity top = serializer.instantiate(files.house)[0];
  const Entity door = named(engine, top, "Door");
  const Entity extra = serializer.instantiate(files.house)[0];
  CHECK(engine.setParent(extra, door));
  const std::string doorBefore = readText(files.door);
  CHECK(!serializer.applyInstance(door));
  CHECK_EQ(readText(files.door), doorBefore);
  // A new file is fine: nothing in it nests itself
  CHECK(serializer.savePrefab(door, files.dir + "/other.json"));
}

TEST_CASE(nested_prefab_fixture_files_and_a_missing_inner_file) {
  auto &serializer = engine.getSceneSerializer();
  // street.json (hand written): a lamp with the street's override, and a prefab that does not exist
  const auto placed = serializer.instantiate(kStreet);
  CHECK_EQ(placed.size(), size_t(4)); // Street, Lamp, Bulb and the one standing for the missing prefab
  const Entity street = placed[0];
  const Entity lamp = named(engine, street, "Lamp");
  CHECK(lamp != -1);
  CHECK_NEAR(opacity(engine, lamp), 0.4f, 1e-5f);
  CHECK(named(engine, street, "Bulb") != -1);
  CHECK_NEAR(engine.get<Transform>(lamp).position.x, 3.0f, 1e-3f);
  CHECK(serializer.instanceOverrides(street).empty()); // nothing to compare for the missing one
  CHECK_EQ(serializer.instanceOverrides(lamp).size(), size_t(0));
  Entity missing = -1;
  for (Entity entity : roots(engine))
    if (!serializer.prefabFileExists(entity))
      missing = entity;
  CHECK(missing != -1);
  CHECK_EQ(serializer.enclosingInstance(missing), street);

  // Saving the street as a prefab keeps the lamp as a reference and the missing one as it is
  const std::string copy = testing::tempDir("nested_street") + "/street_copy.json";
  CHECK(serializer.savePrefab(street, copy));
  const json file = readJson(copy);
  CHECK_EQ(file["entities"].size(), size_t(3));
  CHECK_NEAR(file["entities"][1]["components"]["PrefabInstance"]["overrides"][0]["value"].get<float>(), 0.4f, 1e-5f);
}
