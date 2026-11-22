// Physics through the C++ API: a body falls onto a floor and rests, contacts, raycasts, and Collision boxes.

#include "components/collision.h"
#include "describe.h"
#include "foundation/core/engine.h"
#include "testing.h"
#include <cmath>
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

// Bodies with gravityScale 0 drift as in space while the world keeps its gravity: a hit hands its momentum on
TEST_CASE(physics_zero_gravity_collision) {
  CHECK_NEAR(engine.physics().getGravity(), glm::vec3(0, -9.81f, 0), 0.01f);
  auto floating = [&engine](GameEntity entity, float restitution) {
    RigidBody *body = engine.tryGet<RigidBody>(entity.id());
    CHECK(body != nullptr);
    if (!body)
      return;
    body->gravityScale = 0.0f;
    body->linearDamping = 0.0f;
    body->angularDamping = 0.0f;
    body->restitution = restitution;
  };
  // Equal masses, head on at 10 m/s: elastic, the shot stops and the target takes its speed; inelastic, they share it
  GameEntity shot = engine.create("Shot", Desc("sphere").at(-5, 10, 0).body(BodyType::Dynamic, BodyShape::Sphere, 1.0f));
  GameEntity target = engine.create("Target", Desc("sphere").at(0, 10, 0).body(BodyType::Dynamic, BodyShape::Sphere, 1.0f));
  GameEntity lumpShot = engine.create("LumpShot", Desc("sphere").at(-5, 10, 20).body(BodyType::Dynamic, BodyShape::Sphere, 1.0f));
  GameEntity lump = engine.create("Lump", Desc("sphere").at(0, 10, 20).body(BodyType::Dynamic, BodyShape::Sphere, 1.0f));
  floating(shot, 1.0f);
  floating(target, 1.0f);
  floating(lumpShot, 0.0f);
  floating(lump, 0.0f);
  shot.setVelocity(glm::vec3(10, 0, 0));
  lumpShot.setVelocity(glm::vec3(10, 0, 0));

  steps.seconds(1.0f, [shot, target, lumpShot, lump]() {
    CHECK_NEAR(target.velocity(), glm::vec3(10, 0, 0), 0.5f);
    CHECK_NEAR(shot.velocity(), glm::vec3(0), 0.5f);
    CHECK_NEAR(lump.velocity(), glm::vec3(5, 0, 0), 0.5f);
    CHECK_NEAR(lumpShot.velocity(), glm::vec3(5, 0, 0), 0.5f);
    CHECK_NEAR(shot.velocity().x + target.velocity().x, 10.0f, 0.1f); // momentum kept in both
    CHECK_NEAR(lumpShot.velocity().x + lump.velocity().x, 10.0f, 0.1f);
    CHECK_NEAR(target.position().y, 10.0f, 0.01f); // none of them falls
    CHECK_NEAR(lump.position().y, 10.0f, 0.01f);
  });
  // Without damping the speed it was given lasts
  steps.seconds(2.0f, [&engine, target, lump]() {
    CHECK_NEAR(target.velocity(), glm::vec3(10, 0, 0), 0.5f);
    CHECK_NEAR(lump.velocity(), glm::vec3(5, 0, 0), 0.5f);
    CHECK_NEAR(target.position().y, 10.0f, 0.01f);
    // A setting changed in flight (the body is built again) keeps its motion: a little gravity now bends its path
    engine.tryGet<RigidBody>(target.id())->gravityScale = 0.1f;
  });
  steps.seconds(1.0f, [target]() {
    CHECK_NEAR(target.velocity().x, 10.0f, 0.5f);
    CHECK(target.velocity().y < -0.5f && target.velocity().y > -1.5f); // about 0.981 m/s after a second
  });
}

// A box landing with most of it past another box's edge tips over that edge: contacts turn bodies, not only move them
TEST_CASE(physics_box_tips_over_edge) {
  engine.create("Floor", Desc("box").at(0, -0.5f, 0).size(30, 1, 30).body(BodyType::Static));
  GameEntity base = engine.create("Base", Desc("box").at(0, 0.75f, 0).size(1.5f).body(BodyType::Dynamic, BodyShape::Box, 20.0f));
  GameEntity top = engine.create("Top", Desc("box").at(1.1f, 3.0f, 0).size(1.0f).body(BodyType::Dynamic, BodyShape::Box, 20.0f));
  steps.seconds(1.5f, [top, base]() {
    CHECK(std::abs(top.rotation().z) > 60.0f); // rolled about z, off the base's +x edge
    CHECK(top.position().x > 1.3f);
    CHECK_NEAR(top.position().y, 0.5f, 0.1f); // lying on the floor
    CHECK_NEAR(base.rotation(), glm::vec3(0), 2.0f);
  });
}

TEST_CASE(physics_body_lands_on_character) {
  // A box dropped on a standing character stays out of its capsule (the capsule is a body dynamic bodies meet, not
  // only one that shoves them away afterwards), and raycasts still pass through the character
  engine.create("Floor", Desc("box").at(0, -0.5f, 0).size(30, 1, 30).body(BodyType::Static));
  GameEntity hero = engine.create("Hero", Desc().at(0, 0.9f, 0).character());
  GameEntity box = engine.create("Box", Desc("box").at(0.1f, 4, 0).size(0.6f, 0.6f, 0.6f).body(BodyType::Dynamic));
  // Every frame for a second and a half: the box's middle never gets nearer the capsule's axis (from 0.35 to 1.45
  // up, radius 0.35) than the capsule's radius, as it would falling through it
  auto deepest = std::make_shared<float>(1e9f);
  auto frames = std::make_shared<int>(0);
  steps.until(
      [hero, box, deepest, frames]() {
        const glm::vec3 base = hero.position() - glm::vec3(0, 0.55f, 0), top = hero.position() + glm::vec3(0, 0.55f, 0);
        const glm::vec3 at = box.position();
        const float along = glm::clamp(glm::dot(at - base, top - base) / glm::dot(top - base, top - base), 0.0f, 1.0f);
        *deepest = std::min(*deepest, glm::length(at - (base + (top - base) * along)));
        return ++*frames >= 90;
      },
      "a second and a half", 200);
  steps.step(1, [&engine, hero, deepest]() {
    CHECK(*deepest > 0.35f);
    CHECK_NEAR(hero.position().y, 0.9f, 0.1f);
    const auto throughHero = engine.physics().raycast(glm::vec3(5, 0.9f, 0), glm::vec3(-1, 0, 0), 10.0f);
    CHECK(!throughHero.has_value() || throughHero->entity != hero.id());
  });
}

TEST_CASE(physics_body_falls_when_character_walks_away) {
  // A box resting on a character's head (long enough to fall asleep) falls when the character walks out from under it
  engine.create("Floor", Desc("box").at(0, -0.5f, 0).size(30, 1, 30).body(BodyType::Static));
  GameEntity hero = engine.create("Hero", Desc().at(0, 0.9f, 0).character());
  // A wide flat box, so it stays on the round head instead of rolling off
  GameEntity box = engine.create("Box", Desc("box").at(0, 2.2f, 0).size(1.2f, 0.2f, 1.2f).body(BodyType::Dynamic));
  steps.seconds(3.0f, [box]() { CHECK(box.position().y > 1.7f); });
  steps.step(1, [hero]() mutable { hero.move(glm::vec3(4, 0, 0)); });
  steps.seconds(2.0f, [hero, box]() mutable {
    hero.move(glm::vec3(0));
    CHECK(hero.position().x > 3.0f);
    CHECK_NEAR(box.position().y, 0.1f, 0.15f); // on the floor
  });
}

TEST_CASE(physics_contact_end) {
  // Two bodies that stop touching are listed once, by physics().getContactEnds() and forEachContactEnd (the C++ side of
  // onContactEnd); a body destroyed while touching is not listed
  engine.registerComponent<Ball>("Ball").field("touched", &Ball::touched);
  engine.registerComponent<Ground>("Ground").field("unused", &Ground::unused);
  GameEntity floor = engine.create("Floor", Desc("box").at(0, -0.5f, 0).size(30, 1, 30).body(BodyType::Static).components({"Ground"}));
  GameEntity ball = engine.create("Ball", Desc("sphere").at(0, 2, 0).body(BodyType::Dynamic, BodyShape::Sphere).components({"Ball"}));
  auto began = std::make_shared<int>(0);
  auto ended = std::make_shared<int>(0);
  auto lastSeen = std::make_shared<float>(-1.0f);
  auto listing = [&engine, began, ended, lastSeen]() {
    const float now = engine.time().elapsed();
    if (now == *lastSeen)
      return; // two steps in one frame count it once
    *lastSeen = now;
    engine.forEachContact<Ball, Ground>([began](Entity, Ball &, Entity, Ground &) { ++*began; });
    engine.forEachContactEnd<Ball, Ground>([ended](Entity, Ball &, Entity, Ground &) { ++*ended; });
  };
  // The lists hold one update's contacts, so every frame is looked at: a few frames of watching after each change
  auto watch = [&steps, listing](int frames) {
    auto count = std::make_shared<int>(0);
    steps.until([listing, count, frames]() { listing(); return ++*count >= frames; }, "the frames to pass", frames + 5);
  };
  steps.until([began, listing]() { listing(); return *began > 0; }, "the ball to land", 120);
  watch(30);
  steps.step(1, [ball, began, ended]() mutable {
    CHECK_EQ(*began, 1);
    CHECK_EQ(*ended, 0); // still on the floor (or asleep on it)
    ball.setPosition(glm::vec3(0, 6, 0));
  });
  watch(5);
  steps.step(1, [ended]() { CHECK_EQ(*ended, 1); }); // lifted away: one end
  // Falls and lands again; destroyed while touching, it ends without a report
  steps.until([ball, began, listing]() { listing(); return *began >= 2; }, "the ball to land again", 200,
              [ball]() mutable { ball.destroy(); });
  watch(10);
  steps.step(1, [ended, began]() {
    CHECK_EQ(*ended, 1);
    CHECK_EQ(*began, 2);
  });
}

// Spin: a sphere of 2 kg and radius 1 (inertia 0.4 * 2 * 1 = 0.8 kg m^2), with no damping, so the spin lasts
namespace {
GameEntity spinningSphere(Engine &engine, const char *name, glm::vec3 position) {
  GameEntity sphere = engine.create(
      name, Desc("sphere").at(position.x, position.y, position.z).body(BodyType::Dynamic, BodyShape::Sphere, 2.0f));
  engine.tryGet<RigidBody>(sphere.id())->angularDamping = 0.0f; // set before the body is built: it is built with it
  return sphere;
}
} // namespace

TEST_CASE(physics_angular_velocity_in_degrees) {
  engine.physics().setGravity(glm::vec3(0));
  GameEntity top = spinningSphere(engine, "Top", glm::vec3(0, 10, 0));
  top.setAngularVelocity(glm::vec3(0, 60, 0)); // degrees per second, on the frame it is created
  CHECK_NEAR(top.angularVelocity(), glm::vec3(0, 60, 0), 0.01f); // reads back before the body exists
  steps.seconds(0.5f, [top]() {
    CHECK_NEAR(top.angularVelocity(), glm::vec3(0, 60, 0), 0.5f);
    CHECK_NEAR(top.rotation().y, 30.0f, 2.0f); // 60 degrees per second for half a second
    CHECK_NEAR(top.position(), glm::vec3(0, 10, 0), 0.01f);
  });
}

TEST_CASE(physics_torque_spins_up) {
  engine.physics().setGravity(glm::vec3(0));
  GameEntity wheel = spinningSphere(engine, "Wheel", glm::vec3(0, 10, 0));
  // 8 N m about z for 30 steps: 8 / 0.8 * (30 / 60) = 5 rad/s. A torque lasts one step, so it is given on each one
  for (int i = 0; i < 30; ++i)
    steps.step(1, [wheel]() mutable { wheel.torque(glm::vec3(0, 0, 8)); });
  steps.step(1, [wheel]() {
    CHECK_NEAR(wheel.angularVelocity(), glm::vec3(0, 0, 286.479f), 1.0f);
    CHECK_NEAR(wheel.rotation().x, 0.0f, 0.01f); // no spin about the other axes
  });
  // Off now: the speed stays (no damping, no torque)
  steps.seconds(0.5f, [wheel]() { CHECK_NEAR(wheel.angularVelocity(), glm::vec3(0, 0, 286.479f), 1.0f); });
}

TEST_CASE(physics_angular_impulse_spins_at_once) {
  engine.physics().setGravity(glm::vec3(0));
  GameEntity top = spinningSphere(engine, "Top", glm::vec3(0, 10, 0));
  top.angularImpulse(glm::vec3(0, 0, 1.6f)); // 1.6 N m s / 0.8 kg m^2 = 2 rad/s, from the frame it is created
  steps.step(1, [top]() { CHECK_NEAR(top.angularVelocity(), glm::vec3(0, 0, 114.592f), 0.5f); });
  // On a body that exists: the same again, so 4 rad/s
  steps.step(1, [top]() mutable { top.angularImpulse(glm::vec3(0, 0, 1.6f)); });
  steps.step(1, [top]() { CHECK_NEAR(top.angularVelocity(), glm::vec3(0, 0, 229.183f), 0.5f); });
}

TEST_CASE(physics_push_at_a_point) {
  engine.physics().setGravity(glm::vec3(0));
  GameEntity center = spinningSphere(engine, "Center", glm::vec3(0, 10, 0));
  GameEntity edge = spinningSphere(engine, "Edge", glm::vec3(20, 10, 0));
  steps.step(1, [center, edge]() mutable {
    // Through the center: it moves and does not spin
    center.impulseAt(glm::vec3(0, 0, 4), glm::vec3(0, 10, 0));
    // A point 1 m to the side: the lever r x J = (1,0,0) x (0,0,4) = (0,-4,0) N m s, so it spins about -y
    edge.impulseAt(glm::vec3(0, 0, 4), glm::vec3(21, 10, 0));
  });
  steps.step(1, [center, edge]() {
    CHECK_NEAR(center.velocity(), glm::vec3(0, 0, 2), 0.01f); // 4 N s / 2 kg
    CHECK_NEAR(center.angularVelocity(), glm::vec3(0), 0.01f);
    CHECK_NEAR(edge.velocity(), glm::vec3(0, 0, 2), 0.01f);
    CHECK_NEAR(edge.angularVelocity(), glm::vec3(0, -286.479f, 0), 1.0f); // -4 / 0.8 = -5 rad/s
  });
}

TEST_CASE(physics_point_pushes_on_creation_frame) {
  engine.physics().setGravity(glm::vec3(0));
  GameEntity kicked = spinningSphere(engine, "Kicked", glm::vec3(0, 10, 0));
  GameEntity pushed = spinningSphere(engine, "Pushed", glm::vec3(20, 10, 0));
  GameEntity spun = spinningSphere(engine, "Spun", glm::vec3(40, 10, 0));
  kicked.impulseAt(glm::vec3(0, 0, 4), glm::vec3(1, 10, 0)); // the same as the live case: spins about -y
  pushed.forceAt(glm::vec3(0, 0, 240), glm::vec3(21, 10, 0)); // 240 N for one step of 1/60 s is 4 N s
  spun.torque(glm::vec3(0, 0, 96)); // 96 / 0.8 / 60 = 2 rad/s after one step
  steps.step(1, [kicked, pushed, spun]() {
    CHECK_NEAR(kicked.velocity(), glm::vec3(0, 0, 2), 0.05f);
    CHECK_NEAR(kicked.angularVelocity(), glm::vec3(0, -286.479f, 0), 2.0f);
    CHECK_NEAR(pushed.velocity(), glm::vec3(0, 0, 2), 0.05f);
    CHECK_NEAR(pushed.angularVelocity(), glm::vec3(0, -286.479f, 0), 2.0f);
    CHECK_NEAR(spun.angularVelocity(), glm::vec3(0, 0, 114.592f), 1.0f);
  });
}
