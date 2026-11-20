// Scene files and prefabs: a save/load round trip in a temporary folder, savePrefab and spawn, and the navigation scene.

#include "components/transform.h"
#include "describe.h"
#include "foundation/core/engine.h"
#include "testing.h"
#include <filesystem>
#include <memory>

namespace {
// A game component, saved in scenes by its fields
struct Counter {
  int count = 7;
  std::string label = "none";
  glm::vec3 offset{0.0f};
};

void registerCounter(Engine &engine) {
  engine.registerComponent<Counter>("Counter")
      .field("count", &Counter::count)
      .field("label", &Counter::label)
      .field("offset", &Counter::offset);
}
} // namespace

TEST_CASE(scene_round_trip) {
  registerCounter(engine);
  const std::string file = testing::tempDir("scene") + "/round_trip.json";

  GameEntity box = engine.create("Box", Desc("box").at(1, 2, 3).size(2, 3, 4).color(1, 0, 0).tags({"crate", "red"}));
  box.setRotation(glm::vec3(0, 30, 0));
  GameEntity floor = engine.create("Floor", Desc("box").at(0, -1, 0).size(10, 1, 10).body(BodyType::Static));
  GameEntity counter = engine.create("Counted", Desc().at(5, 0, 0).components({"Counter"}));
  engine.get<Counter>(counter.id()).count = 42;
  engine.get<Counter>(counter.id()).label = "forty-two";
  engine.get<Counter>(counter.id()).offset = glm::vec3(1, 2, 3);
  GameEntity parent = engine.create("Parent", Desc("sphere").at(10, 0, 0));
  GameEntity child = engine.create("Child", Desc("box").at(10, 2, 0));
  CHECK(child.setParent(parent));
  const size_t entityCount = engine.getEntityManager().entities().size();

  CHECK(engine.saveScene(file));
  CHECK(std::filesystem::exists(file));

  // Change everything, so loading has something to put back
  box.destroy();
  counter.setPosition(glm::vec3(99, 99, 99));
  engine.get<Counter>(counter.id()).count = 0;
  floor.destroy();
  engine.create("Intruder", Desc("box"));
  engine.loadScene(file);
  CHECK(engine.isLoadingScene());

  steps.until([&engine]() { return !engine.isLoadingScene(); }, "the scene to load", 300);
  steps.step(1, [&engine, box, counter, parent, entityCount, file]() {
    // The old handles belong to the scene that was replaced
    CHECK(!box.valid());
    CHECK(!counter.valid());
    CHECK(!parent.valid());
    CHECK(!engine.find("Intruder").valid());
    CHECK_EQ(engine.getEntityManager().entities().size(), entityCount);

    GameEntity loadedBox = engine.find("Box");
    CHECK(loadedBox.valid());
    CHECK_NEAR(loadedBox.position(), glm::vec3(1, 2, 3), 1e-4f);
    CHECK_NEAR(loadedBox.rotation(), glm::vec3(0, 30, 0), 1e-3f);
    CHECK_NEAR(loadedBox.scale(), glm::vec3(2, 3, 4), 1e-4f);
    CHECK_NEAR(loadedBox.color(), glm::vec3(1, 0, 0), 1e-4f);
    CHECK(loadedBox.hasTag("crate"));
    CHECK(loadedBox.hasTag("red"));
    CHECK_EQ(loadedBox.tags().size(), size_t(2));
    CHECK_EQ(engine.findTagged("crate").size(), size_t(1));

    GameEntity loadedFloor = engine.find("Floor");
    CHECK(loadedFloor.valid());
    CHECK(loadedFloor.has("RigidBody"));
    CHECK_EQ(engine.get<RigidBody>(loadedFloor.id()).type, BodyType::Static);

    GameEntity loadedCounter = engine.find("Counted");
    CHECK(loadedCounter.valid());
    CHECK_NEAR(loadedCounter.position(), glm::vec3(5, 0, 0), 1e-4f);
    CHECK_EQ(engine.get<Counter>(loadedCounter.id()).count, 42);
    CHECK_EQ(engine.get<Counter>(loadedCounter.id()).label, std::string("forty-two"));
    CHECK_NEAR(engine.get<Counter>(loadedCounter.id()).offset, glm::vec3(1, 2, 3), 1e-5f);

    GameEntity loadedParent = engine.find("Parent");
    GameEntity loadedChild = engine.find("Child");
    CHECK(loadedChild.valid() && loadedParent.valid());
    CHECK(loadedChild.parent() == loadedParent);
    CHECK_EQ(engine.getScenePath(), file);
  });
}

TEST_CASE(scene_missing_file) {
  GameEntity keep = engine.create("Keep", Desc("box"));
  // A scene that cannot be read leaves the current one alone
  engine.loadScene(testing::tempDir("scene") + "/does_not_exist.json");
  steps.step(5, [&engine, keep]() {
    CHECK(keep.valid());
    CHECK(engine.find("Keep").valid());
  });
}

TEST_CASE(prefab_save_and_spawn) {
  registerCounter(engine);
  const std::string file = testing::tempDir("prefab") + "/turret.json";

  GameEntity root = engine.create("Turret", Desc("box").at(30, 0, 0).tags({"enemy", "turret"}).components({"Counter"}));
  engine.get<Counter>(root.id()).count = 3;
  GameEntity barrel = engine.create("Barrel", Desc("cylinder").at(30, 1, 0).size(0.2f));
  CHECK(barrel.setParent(root));
  CHECK(engine.savePrefab(root.id(), file));
  CHECK(!engine.savePrefab(-1, file)); // nothing to save

  GameEntity a = engine.spawn(file, glm::vec3(5, 0, 0));
  GameEntity b = engine.spawn(file, glm::vec3(-5, 0, 2), glm::vec3(0, 90, 0));
  CHECK(a.valid() && b.valid());
  CHECK(a != root && b != root && a != b);
  // The root sits where it was spawned (not where it was saved), turned as asked; tags and components come along
  CHECK_NEAR(a.position(), glm::vec3(5, 0, 0), 1e-4f);
  CHECK_NEAR(b.position(), glm::vec3(-5, 0, 2), 1e-4f);
  CHECK_NEAR(b.rotation().y, 90.0f, 1e-3f);
  CHECK(a.hasTag("enemy") && a.hasTag("turret"));
  CHECK_EQ(engine.get<Counter>(a.id()).count, 3);
  CHECK_EQ(engine.findTagged("turret").size(), size_t(3)); // the original and the two copies
  CHECK_EQ(engine.findAll("Barrel").size(), size_t(3));
  CHECK_EQ(a.children().size(), size_t(1));
  CHECK_EQ(a.children()[0].name(), std::string("Barrel"));

  // The copy's child follows it
  steps.step(2, [a]() { CHECK_NEAR(a.children()[0].position(), glm::vec3(5, 1, 0), 1e-3f); });

  // A file that is not there gives an empty handle
  CHECK(!engine.spawn(testing::tempDir("prefab") + "/missing.json").valid());
}

// The scene the engine ships: a character computes a path around the maze to the target and walks it
TEST_CASE(navigation_scene) {
  engine.loadScene("../assets/scenes/navigation.json");
  steps.until([&engine]() { return !engine.isLoadingScene(); }, "the scene to load", 300);
  steps.step(3, [&engine]() {
    const NavigationSystem &navigation = engine.navigation();
    CHECK(navigation.isBuilt()); // the Walker's script built it
    const std::vector<glm::vec3> &path = navigation.getLastPath();
    CHECK(path.size() >= 3);
    GameEntity walker = engine.find("Walker");
    GameEntity target = engine.find("Target");
    CHECK(walker.valid() && target.valid());
    if (path.size() >= 2) {
      float length = 0.0f;
      for (size_t i = 1; i < path.size(); ++i)
        length += glm::distance(path[i - 1], path[i]);
      // Straight across it would be about 17 units: the maze makes it much longer
      CHECK(length > 22.0f);
      CHECK(length < 80.0f);
      CHECK_NEAR(path.back().x, target.position().x, 1.0f);
      CHECK_NEAR(path.back().z, target.position().z, 1.0f);
    }
  });
  steps.until(
      [&engine]() {
        GameEntity walker = engine.find("Walker");
        GameEntity target = engine.find("Target");
        return glm::distance(glm::vec2(walker.position().x, walker.position().z),
                             glm::vec2(target.position().x, target.position().z)) < 1.0f;
      },
      "the walker to reach the target", 1000);
}
