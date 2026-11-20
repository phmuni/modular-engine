// Physics through the C++ API: a body falls onto a floor and rests, contacts, raycasts, and Collision boxes.

#include "components/collision.h"
#include "describe.h"
#include "foundation/core/engine.h"
#include "testing.h"
#include <memory>

namespace {
// Game components, to ask for contacts between kinds of entity
struct Ball {
  int touched = 0;
};
struct Ground {
  int unused = 0;
};
} // namespace

TEST_CASE(physics_fall_and_rest) {
  engine.registerComponent<Ball>("Ball").field("touched", &Ball::touched);
  engine.registerComponent<Ground>("Ground").field("unused", &Ground::unused);
  GameEntity floor = engine.create("Floor", Desc("box").at(0, -0.5f, 0).size(30, 1, 30).body(BodyType::Static).components({"Ground"}));
  GameEntity ball = engine.create("Ball", Desc("sphere").at(0, 5, 0).body(BodyType::Dynamic, BodyShape::Sphere, 2.0f).components({"Ball"}));
  auto contacts = std::make_shared<int>(0);

  // The ball's contact with the floor is reported once, the frame it lands
  steps.until(
      [&engine, contacts]() {
        engine.forEachContact<Ball, Ground>([contacts](Entity, Ball &, Entity, Ground &) { ++*contacts; });
        return *contacts > 0;
      },
      "the ball to touch the floor", 120);
  steps.step(1, [ball]() { CHECK_NEAR(ball.position().y, 1.0f, 0.2f); }); // landed: the sphere's radius is 1
  steps.seconds(2.0f, [&engine, ball, floor]() mutable {
    CHECK_NEAR(ball.position().y, 1.0f, 0.1f);
    CHECK_NEAR(ball.velocity(), glm::vec3(0), 0.1f); // at rest
    CHECK_NEAR(floor.position(), glm::vec3(0, -0.5f, 0), 1e-6f);

    // A ray straight down from above hits the ball first, then (from beside it) the floor
    const auto onBall = engine.physics().raycast(glm::vec3(0, 6, 0), glm::vec3(0, -1, 0), 20.0f);
    CHECK(onBall.has_value());
    if (onBall) {
      CHECK_EQ(onBall->entity, ball.id());
      CHECK_NEAR(onBall->point.y, 2.0f, 0.1f);
    }
    const auto onFloor = engine.physics().raycast(glm::vec3(10, 6, 0), glm::vec3(0, -1, 0), 20.0f);
    CHECK(onFloor.has_value());
    if (onFloor) {
      CHECK_EQ(onFloor->entity, floor.id());
      CHECK_NEAR(onFloor->point, glm::vec3(10, 0, 0), 0.01f);
      CHECK_NEAR(onFloor->normal, glm::vec3(0, 1, 0), 0.01f);
    }
    CHECK(!engine.physics().raycast(glm::vec3(10, 6, 0), glm::vec3(0, 1, 0), 20.0f).has_value());

    // An impulse sets it going
    ball.impulse(glm::vec3(0, 0, 2000));
  });
  steps.step(5, [ball]() { CHECK(ball.velocity().z > 0.1f); });
}

TEST_CASE(physics_gravity_and_velocity) {
  GameEntity ball = engine.create("Ball", Desc("sphere").at(0, 50, 0).body(BodyType::Dynamic, BodyShape::Sphere));
  CHECK_NEAR(engine.physics().getGravity(), glm::vec3(0, -9.81f, 0), 0.01f);
  engine.physics().setGravity(glm::vec3(0));
  // Set on the frame the entity was created, before its body is in the physics world
  ball.setVelocity(glm::vec3(3, 0, 0));
  steps.seconds(1.0f, [&engine, ball]() {
    CHECK_NEAR(ball.position().y, 50.0f, 0.1f); // no gravity: no fall
    CHECK(ball.position().x > 2.5f && ball.position().x < 3.5f);
    engine.physics().setGravity(glm::vec3(0, -9.81f, 0));
  });
  steps.seconds(0.5f, [ball]() { CHECK(ball.position().y < 49.0f); });
}

TEST_CASE(physics_motion_on_creation_frame) {
  engine.physics().setGravity(glm::vec3(0));
  GameEntity fast = engine.create("Fast", Desc("sphere").at(0, 10, 0).body(BodyType::Dynamic, BodyShape::Sphere));
  GameEntity pushed = engine.create("Pushed", Desc("sphere").at(5, 10, 0).body(BodyType::Dynamic, BodyShape::Sphere));
  GameEntity spun = engine.create("Spun", Desc("sphere").at(10, 10, 0).body(BodyType::Dynamic, BodyShape::Sphere));
  GameEntity forced = engine.create("Forced", Desc("sphere").at(15, 10, 0).body(BodyType::Dynamic, BodyShape::Sphere));
  fast.setVelocity(glm::vec3(0, 0, 20));
  pushed.impulse(glm::vec3(0, 0, 2000));
  engine.physics().setAngularVelocity(spun.id(), glm::vec3(0, 5, 0));
  forced.force(glm::vec3(0, 0, 5000));
  CHECK_NEAR(fast.velocity(), glm::vec3(0, 0, 20), 0.001f); // reads back before the body exists
  steps.step(5, [&engine, fast, pushed, spun, forced]() {
    CHECK(fast.position().z > 0.5f);
    CHECK(pushed.velocity().z > 0.1f); // a heavy sphere: slow, but going
    CHECK(engine.physics().getAngularVelocity(spun.id()).y > 1.0f);
    CHECK(forced.position().z > 0.0f);
  });
}

TEST_CASE(collision_boxes) {
  GameEntity a = engine.create("A", Desc("box").at(0, 0, 0).collision());
  GameEntity neighbor = engine.create("Neighbor", Desc("box").at(0.5f, 0, 0).collision());
  GameEntity distant = engine.create("Distant", Desc("box").at(10, 0, 0).collision());
  CHECK(engine.isColliding(a.id(), neighbor.id()));
  CHECK(!engine.isColliding(a.id(), distant.id()));
  const auto &pairs = engine.findCollisions();
  CHECK_EQ(pairs.size(), size_t(1)); // each overlapping pair once

  // A ray hits the nearest box
  const auto hit = engine.raycast(glm::vec3(10, 0, 5), glm::vec3(0, 0, -1), 20.0f);
  CHECK(hit.has_value());
  if (hit)
    CHECK_EQ(hit->entity, distant.id());

  // Moving one away ends the overlap; the boxes are as they are placed, so ask again after moving
  neighbor.setPosition(glm::vec3(-8, 0, 0));
  engine.fitCollisionToModel(neighbor.id());
  CHECK(!engine.isColliding(a.id(), neighbor.id()));
  CHECK_EQ(engine.findCollisions().size(), size_t(0));
}
