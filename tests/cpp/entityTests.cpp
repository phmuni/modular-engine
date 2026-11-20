// GameEntity handles, Engine::create and the finders, through the C++ API (the Lua side is in tests/lua).

#include "describe.h"
#include "components/collision.h"
#include "foundation/core/engine.h"
#include "systems/sceneSerializer.h"
#include "testing.h"
#include <memory>

namespace {
// A game component, as a game registers its own
struct Marker {
  float strength = 2.5f;
  std::string label = "default";
};
} // namespace

TEST_CASE(entity_handle) {
  GameEntity e = engine.create("Thing", Desc("box").at(1, 2, 3));
  CHECK(e.valid());
  CHECK(static_cast<bool>(e));
  CHECK_EQ(e.name(), std::string("Thing"));
  CHECK_NEAR(e.position(), glm::vec3(1, 2, 3), 1e-5f);
  CHECK_NEAR(e.scale(), glm::vec3(1), 1e-5f);

  e.setPosition(glm::vec3(-4, 0.5f, 9));
  CHECK_NEAR(e.position(), glm::vec3(-4, 0.5f, 9), 1e-5f);
  e.setScale(glm::vec3(2, 3, 4));
  CHECK_NEAR(e.scale(), glm::vec3(2, 3, 4), 1e-5f);
  e.setName("Renamed");
  CHECK_EQ(e.name(), std::string("Renamed"));
  CHECK_EQ(engine.find("Renamed").id(), e.id());

  // forward is -Z turned by the yaw (rotation.y), right is the way to the right of it
  e.setRotation(glm::vec3(0, 0, 0));
  CHECK_NEAR(e.forward(), glm::vec3(0, 0, -1), 1e-5f);
  CHECK_NEAR(e.right(), glm::vec3(1, 0, 0), 1e-5f);
  e.setRotation(glm::vec3(0, 90, 0));
  CHECK_NEAR(e.rotation(), glm::vec3(0, 90, 0), 1e-4f);
  CHECK_NEAR(e.forward(), glm::vec3(-1, 0, 0), 1e-4f);
  CHECK_NEAR(e.right(), glm::vec3(0, 0, -1), 1e-4f);
  CHECK_NEAR(glm::length(e.forward()), 1.0f, 1e-5f);

  // Color and visibility
  e.setColor(glm::vec3(1, 0, 0.5f));
  CHECK_NEAR(e.color(), glm::vec3(1, 0, 0.5f), 1e-5f);
  CHECK(e.isVisible());
  e.setVisible(false);
  CHECK(!e.isVisible());
  CHECK(e.has("Hidden")); // hiding is a component, which the editor and scenes show
  e.setVisible(true);
  CHECK(e.isVisible());

  CHECK(e.has("Model"));
  CHECK(e.has("Transform"));
  CHECK(!e.has("RigidBody"));
  CHECK(!e.hasScript());
  CHECK(e.describe().find("Renamed") != std::string::npos);

  // A handle outlives its entity: valid() says, getters answer with defaults, setters do nothing
  GameEntity copy = e;
  CHECK(copy == e);
  e.destroy();
  CHECK(!e.valid());
  CHECK(!copy.valid());
  CHECK(!engine.find("Renamed").valid());
  CHECK_NEAR(copy.position(), glm::vec3(0), 1e-6f);
  copy.setPosition(glm::vec3(5)); // must not crash or revive it
  CHECK(!copy.valid());
  CHECK(copy.tags().empty());
  CHECK(!copy.has("Model"));
  CHECK_NEAR(glm::length(GameEntity().position()), 0.0f, 1e-6f); // an empty handle
  CHECK_EQ(GameEntity().id(), -1);
}

TEST_CASE(entity_create_options) {
  engine.registerComponent<Marker>("Marker").field("strength", &Marker::strength).field("label", &Marker::label);

  // Model shorthands: the engine's own box, sphere and cylinder
  for (const char *shape : {"box", "sphere", "cylinder"}) {
    GameEntity e = engine.create(shape, Desc(shape));
    CHECK(e.valid());
    CHECK(e.has("Model"));
  }

  // A static body from a box, a dynamic one from a sphere
  GameEntity floor = engine.create("Floor", Desc("box").size(10, 1, 10).body(BodyType::Static));
  CHECK(floor.has("RigidBody"));
  CHECK_EQ(engine.get<RigidBody>(floor.id()).type, BodyType::Static);
  GameEntity ball = engine.create("Ball", Desc("sphere").at(0, 3, 0).body(BodyType::Dynamic, BodyShape::Sphere, 4.0f));
  CHECK_EQ(engine.get<RigidBody>(ball.id()).type, BodyType::Dynamic);
  CHECK_EQ(engine.get<RigidBody>(ball.id()).shape, BodyShape::Sphere);
  CHECK_NEAR(engine.get<RigidBody>(ball.id()).mass, 4.0f, 1e-6f);

  // Collision hugs the model as placed
  GameEntity crate = engine.create("Crate", Desc("box").at(0, 0, 5).size(2).collision());
  CHECK(crate.has("Collision"));
  const Collision &box = engine.get<Collision>(crate.id());
  CHECK_NEAR(box.max.x - box.min.x, 2.0f, 0.05f);
  CHECK(engine.isColliding(crate.id(), engine.create("Overlapping", Desc("box").at(0.5f, 0, 5).collision()).id()));

  // Color, emission, a character and tags
  GameEntity red = engine.create("Red", Desc("box").color(1, 0, 0).tags({"enemy", "red"}));
  CHECK_NEAR(red.color(), glm::vec3(1, 0, 0), 1e-5f);
  CHECK(red.hasTag("enemy"));
  CHECK(red.hasTag("red"));
  CHECK_EQ(red.tags().size(), size_t(2));
  GameEntity hero = engine.create("Hero", Desc().at(0, 2, 0).character());
  CHECK(hero.has("CharacterController"));
  CHECK(!hero.has("Model")); // no model: only a Transform

  // Game components by name; one that is not registered is skipped (with a warning), not fatal
  GameEntity marked = engine.create("Marked", Desc().components({"Marker", "NoSuchComponent"}));
  CHECK(marked.has("Marker"));
  CHECK_NEAR(engine.get<Marker>(marked.id()).strength, 2.5f, 1e-6f);
  CHECK(!marked.has("NoSuchComponent"));
  CHECK_EQ(engine.findWithComponent("Marker").size(), size_t(1));
}

TEST_CASE(entity_find) {
  engine.registerComponent<Marker>("Marker");
  GameEntity a = engine.create("Coin", Desc("sphere").tags({"pickup"}));
  GameEntity b = engine.create("Coin", Desc("sphere").tags({"pickup", "gold"}));
  GameEntity c = engine.create("Gem", Desc("box").tags({"pickup"}).components({"Marker"}));
  GameEntity d = engine.create("Rock", Desc("box"));

  CHECK_EQ(engine.find("Coin").id(), a.id()); // the first
  CHECK(!engine.find("Nothing").valid());
  const auto coins = engine.findAll("Coin");
  CHECK_EQ(coins.size(), size_t(2));
  CHECK(coins.size() == 2 && coins[0] == a && coins[1] == b); // by entity number
  CHECK_EQ(engine.findAll("Nothing").size(), size_t(0));

  CHECK_EQ(engine.findTagged("pickup").size(), size_t(3));
  CHECK_EQ(engine.findTagged("gold").size(), size_t(1));
  CHECK_EQ(engine.findTagged("none").size(), size_t(0));
  CHECK(engine.findTagged("gold")[0] == b);

  CHECK_EQ(engine.findWithComponent("Marker").size(), size_t(1));
  CHECK(engine.findWithComponent("Marker")[0] == c);
  // The two coins, the gem and the rock have models, and so does nothing else (the test camera has none)
  CHECK_EQ(engine.findWithComponent("Model").size(), size_t(4));
  CHECK_EQ(engine.findWithComponent("Tags").size(), size_t(3));
  CHECK_EQ(engine.findWithComponent("Camera").size(), size_t(1));

  // A destroyed entity leaves every finder
  b.destroy();
  CHECK_EQ(engine.findAll("Coin").size(), size_t(1));
  CHECK_EQ(engine.findTagged("gold").size(), size_t(0));
  CHECK_EQ(engine.findTagged("pickup").size(), size_t(2));
  CHECK(d.valid());
}

TEST_CASE(entity_hierarchy) {
  GameEntity parent = engine.create("Parent", Desc("box").at(10, 0, 0));
  GameEntity child = engine.create("Child", Desc("box").at(10, 2, 0));
  CHECK(child.setParent(parent));
  CHECK(child.parent() == parent);
  CHECK_EQ(parent.children().size(), size_t(1));
  CHECK(!parent.setParent(child)); // a loop is refused
  // Moving the parent carries the child, which keeps its place relative to it
  steps.step(1, [parent]() mutable { parent.setPosition(glm::vec3(20, 0, 0)); });
  steps.step(1, [child]() { CHECK_NEAR(child.position(), glm::vec3(20, 2, 0), 1e-3f); });
}
