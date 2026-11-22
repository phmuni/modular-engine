// Joints (the Joint component, Jolt constraints): a hinge turned by a motor and stopped by a pillar, limits, position
// motors and springs, sliders, fixed and distance joints, collideConnected, rebuilding, scene files and the editor panel.
// Jolt is deterministic, so what is checked here is the same on every run.

#include "components/joint.h"
#include "describe.h"
#include "foundation/core/engine.h"
#include "systems/editorUISystem.h"
#include "systems/sceneSerializer.h"
#include "systems/stateSystem.h"
#include "testing.h"
#include <cmath>
#include <glm/gtc/quaternion.hpp>
#include <memory>

#include <glad/glad.h>

namespace {
Joint hingeTo(const std::string &target, const glm::vec3 &anchor) {
  Joint joint;
  joint.type = JointType::Hinge;
  joint.target = target;
  joint.anchor = anchor;
  return joint;
}

Joint &jointOf(Engine &engine, const GameEntity &entity) { return *engine.tryGet<Joint>(entity.id()); }

// A wall 4 long, 2 high and 0.3 thick whose hinge is at its -x end, with its middle at x = 0: the pivot is at (-2, 1, 0)
GameEntity makeWall(Engine &engine, float mass = 0.0f) {
  GameEntity wall =
      engine.create("Wall", Desc("box").at(0, 1, 0).size(4, 2, 0.3f).body(BodyType::Dynamic, BodyShape::Box, mass));
  Joint joint = hingeTo("", glm::vec3(-0.5f, 0, 0));
  joint.motor = JointMotor::Velocity;
  joint.motorSpeed = 60.0f;
  wall.setJoint(joint);
  return wall;
}

// How far the wall's face is from the pillar (0.6 square, axis-aligned): negative when they overlap. In the wall's own
// frame the pillar is |z| away, and its square reaches 0.3 * (|cos| + |sin|) toward the wall at the wall's turn.
float gapToPillar(const GameEntity &wall, const GameEntity &pillar) {
  const float yaw = glm::radians(wall.rotation().y);
  const glm::vec3 local = glm::inverse(glm::angleAxis(yaw, glm::vec3(0, 1, 0))) * (pillar.position() - wall.position());
  return std::fabs(local.z) - 0.15f - 0.3f * (std::fabs(std::cos(yaw)) + std::fabs(std::sin(yaw)));
}

float distance(const GameEntity &a, const GameEntity &b) { return glm::length(a.position() - b.position()); }
} // namespace

// THE CASE THIS WAS MADE FOR: a wall spun by a motor around a vertical axis is stopped by a static pillar in its way
TEST_CASE(joint_motor_wall_stops_at_a_pillar) {
  engine.physics().setGravity(glm::vec3(0)); // a wall turning flat: gravity changes nothing about the hinge
  GameEntity wall = makeWall(engine);
  // At 3 units from the pivot, 60 degrees along the sweep (it turns right-handed about +y: x toward -z)
  const glm::vec3 pillarPosition(-2.0f + 3.0f * std::cos(glm::radians(60.0f)), 1.0f,
                                 -3.0f * std::sin(glm::radians(60.0f)));
  GameEntity pillar = engine.create("Pillar", Desc("box").at(pillarPosition.x, pillarPosition.y, pillarPosition.z).size(0.6f, 2, 0.6f).body(BodyType::Static));
  auto stalledAt = std::make_shared<float>(0.0f);

  steps.step(1, [&engine, wall]() {
    CHECK(engine.physics().isJointBuilt(wall.id()));
    CHECK_NEAR(wall.jointAngle(), 0.0f, 5.0f);
  });
  steps.seconds(1.0f, [wall]() mutable { CHECK(wall.jointAngle() > 20.0f); }); // it turns the way the speed says
  steps.seconds(2.5f, [wall, pillar, stalledAt]() mutable {
    const float angle = wall.jointAngle();
    *stalledAt = angle;
    // Stopped short of the pillar: it stands 60 degrees round, the wall's face meets it about 9 degrees earlier
    CHECK(angle > 40.0f && angle < 56.0f);
    CHECK(std::isfinite(wall.position().x) && std::isfinite(wall.position().y) && std::isfinite(wall.position().z));
    CHECK_NEAR(wall.position().y, 1.0f, 0.05f); // the hinge keeps it on its axis
    // Not inside the pillar, and pressing on it: how far its face is from the pillar's nearest corner
    const float gap = gapToPillar(wall, pillar);
    CHECK(gap > -0.05f);
    CHECK(gap < 0.05f);
    CHECK_NEAR(wall.angularVelocity().y, 0.0f, 5.0f); // stalled
  });
  // Kept pushing for a long time: no tunneling, no creep through, no explosion
  steps.seconds(6.0f, [wall, pillar, stalledAt]() mutable {
    CHECK(wall.jointAngle() < 58.0f);
    CHECK_NEAR(wall.jointAngle(), *stalledAt, 3.0f);
    CHECK(gapToPillar(wall, pillar) > -0.06f);
    CHECK(glm::length(wall.position()) < 10.0f);
  });
}

TEST_CASE(joint_hinge_limits_stop_the_motor) {
  engine.physics().setGravity(glm::vec3(0));
  GameEntity wall = makeWall(engine);
  steps.step(0, [&engine, wall]() {
    Joint &joint = jointOf(engine, wall);
    joint.limits = true;
    joint.min = -30.0f;
    joint.max = 45.0f;
  });
  steps.seconds(2.0f, [wall]() mutable { CHECK_NEAR(wall.jointAngle(), 45.0f, 2.0f); });
  // The other way round, to the other limit
  steps.step(0, [wall]() mutable { wall.setJointMotorSpeed(-90.0f); });
  steps.seconds(2.5f, [wall]() mutable { CHECK_NEAR(wall.jointAngle(), -30.0f, 2.0f); });
  steps.seconds(1.0f, [wall]() mutable { CHECK(wall.jointAngle() >= -32.0f); });
}

TEST_CASE(joint_position_motor_and_spring_door) {
  engine.physics().setGravity(glm::vec3(0));
  GameEntity wall = makeWall(engine, 100.0f);
  steps.step(0, [wall]() mutable { wall.setJointMotorTarget(30.0f); });
  steps.seconds(3.0f, [wall]() mutable { CHECK_NEAR(wall.jointAngle(), 30.0f, 1.0f); });
  steps.step(0, [wall]() mutable { wall.setJointMotorTarget(-20.0f); });
  steps.seconds(3.0f, [wall]() mutable { CHECK_NEAR(wall.jointAngle(), -20.0f, 1.0f); });
}

TEST_CASE(joint_spring_door_swings_back) {
  engine.physics().setGravity(glm::vec3(0));
  GameEntity door = engine.create("Door", Desc("box").at(0, 1, 0).size(2, 2, 0.2f).body(BodyType::Dynamic, BodyShape::Box, 50.0f));
  Joint joint = hingeTo("", glm::vec3(-0.5f, 0, 0));
  joint.motor = JointMotor::Position;
  joint.motorTarget = 0.0f;
  joint.springFrequency = 1.5f;
  joint.springDamping = 0.5f;
  door.setJoint(joint);
  auto widest = std::make_shared<float>(0.0f);
  steps.step(5, [door]() mutable { door.angularImpulse(glm::vec3(0, 400, 0)); });
  for (int i = 0; i < 30; ++i)
    steps.step(3, [door, widest]() mutable { *widest = std::max(*widest, door.jointAngle()); });
  steps.seconds(3.0f, [door, widest]() mutable {
    CHECK(*widest > 15.0f); // it was pushed open...
    CHECK_NEAR(door.jointAngle(), 0.0f, 3.0f); // ...and came back
  });
}

TEST_CASE(joint_slider_motor_and_position) {
  GameEntity platform = engine.create("Platform", Desc("box").at(0, 1, 0).size(3, 0.4f, 3).body(BodyType::Dynamic, BodyShape::Box, 500.0f));
  Joint joint;
  joint.type = JointType::Slider;
  joint.axis = glm::vec3(0, 1, 0);
  joint.limits = true;
  joint.min = 0.0f;
  joint.max = 3.0f;
  joint.motor = JointMotor::Velocity;
  joint.motorSpeed = 1.0f;
  platform.setJoint(joint);
  steps.seconds(1.0f, [platform]() mutable {
    CHECK_NEAR(platform.jointPosition(), 1.0f, 0.15f);
    CHECK_NEAR(platform.position().y, 2.0f, 0.15f);
  });
  steps.seconds(4.0f, [platform]() mutable {
    CHECK_NEAR(platform.jointPosition(), 3.0f, 0.05f); // up to the limit and no further
    CHECK_NEAR(platform.position().x, 0.0f, 0.02f);    // and it only moves along its rail
    CHECK_NEAR(platform.position().z, 0.0f, 0.02f);
  });
  steps.step(0, [platform]() mutable { platform.setJointMotorTarget(1.5f); });
  steps.seconds(3.0f, [platform]() mutable {
    CHECK_NEAR(platform.jointPosition(), 1.5f, 0.05f);
    CHECK_NEAR(platform.position().y, 2.5f, 0.05f);
  });
  steps.step(0, [platform]() mutable { platform.stopJointMotor(); });
  steps.seconds(2.0f, [platform]() mutable { CHECK(platform.jointPosition() <= 0.01f); }); // no motor: gravity drops it to the lower limit
}

TEST_CASE(joint_fixed_keeps_the_relative_pose) {
  GameEntity a = engine.create("A", Desc("box").at(0, 8, 0).size(1).body(BodyType::Dynamic));
  GameEntity b = engine.create("B", Desc("box").at(1.5f, 8, 0).size(1).body(BodyType::Dynamic));
  Joint joint;
  joint.type = JointType::Fixed;
  joint.target = "A";
  b.setJoint(joint);
  GameEntity floor = engine.create("Floor", Desc("box").at(0, -0.5f, 0).size(30, 1, 30).body(BodyType::Static));
  steps.seconds(0.4f, [a, b]() mutable { CHECK_NEAR(b.position() - a.position(), glm::vec3(1.5f, 0, 0), 0.03f); }); // falling
  steps.step(0, [b]() mutable { b.impulseAt(glm::vec3(0, 0, 3000), b.position() + glm::vec3(0, 0.5f, 0)); }); // a hit on one of them
  steps.seconds(3.0f, [a, b]() mutable {
    CHECK_NEAR(distance(a, b), 1.5f, 0.05f);
    CHECK(glm::length(a.position() - glm::vec3(0, 0.5f, 0)) > 0.1f); // the hit moved the pair
    // Both turned by the same rotation
    CHECK_NEAR(a.rotation(), b.rotation(), 2.0f);
  });
}

TEST_CASE(joint_distance_pendulum_and_bungee) {
  // A pendulum: the bob's joint reaches to a point in the world, its length the distance it started at
  GameEntity bob = engine.create("Bob", Desc("sphere").at(3, 5, 0).size(0.5f).body(BodyType::Dynamic, BodyShape::Sphere, 2.0f));
  Joint rod;
  rod.type = JointType::Distance;
  rod.targetAnchor = glm::vec3(0, 5, 0);
  bob.setJoint(rod);
  // A rope with a spring: free up to 2 units, then it stretches and pulls back
  GameEntity ball = engine.create("Ball", Desc("sphere").at(20, 8, 0).size(0.5f).body(BodyType::Dynamic, BodyShape::Sphere, 2.0f));
  Joint bungee;
  bungee.type = JointType::Distance;
  bungee.targetAnchor = glm::vec3(20, 10, 0);
  bungee.maxDistance = 2.0f;
  bungee.springFrequency = 1.0f;
  bungee.springDamping = 0.3f;
  ball.setJoint(bungee);
  // The same without the spring: hard
  GameEntity rope = engine.create("Rope", Desc("sphere").at(40, 8, 0).size(0.5f).body(BodyType::Dynamic, BodyShape::Sphere, 2.0f));
  Joint hard = bungee;
  hard.targetAnchor = glm::vec3(40, 10, 0);
  hard.springFrequency = 0.0f;
  rope.setJoint(hard);

  auto lowest = std::make_shared<float>(5.0f);
  auto longest = std::make_shared<float>(0.0f), hardest = std::make_shared<float>(0.0f);
  auto sample = [=]() mutable {
    *lowest = std::min(*lowest, bob.position().y);
    CHECK_NEAR(glm::length(bob.position() - glm::vec3(0, 5, 0)), 3.0f, 0.05f);
    *longest = std::max(*longest, glm::length(ball.position() - glm::vec3(20, 10, 0)));
    *hardest = std::max(*hardest, glm::length(rope.position() - glm::vec3(40, 10, 0)));
  };
  for (int i = 0; i < 40; ++i)
    steps.step(6, sample);
  steps.step(0, [=]() mutable {
    CHECK(*lowest < 3.0f);      // the pendulum swung down
    CHECK(*longest > 2.25f);    // the spring let it stretch past 2...
    CHECK(*hardest < 2.05f);    // ...and the hard one did not
  });
  steps.seconds(6.0f, [=]() mutable {
    // Settled where the spring holds its weight: 2 + g / (2 pi f)^2 = 2.25
    CHECK_NEAR(glm::length(ball.position() - glm::vec3(20, 10, 0)), 2.25f, 0.15f);
    CHECK_NEAR(glm::length(rope.position() - glm::vec3(40, 10, 0)), 2.0f, 0.05f);
  });
}

TEST_CASE(joint_collide_connected_decides_whether_the_pair_collides) {
  engine.physics().setGravity(glm::vec3(0));
  auto pair = [&engine](float x, bool collide) {
    GameEntity a = engine.create("A" + std::to_string(int(x)), Desc("sphere").at(x, 5, 0).size(1).body(BodyType::Dynamic, BodyShape::Sphere));
    GameEntity b = engine.create("B" + std::to_string(int(x)), Desc("sphere").at(x + 0.4f, 5, 0).size(1).body(BodyType::Dynamic, BodyShape::Sphere));
    Joint rope; // loose: any distance up to 5
    rope.type = JointType::Distance;
    rope.target = a.name();
    rope.maxDistance = 5.0f;
    rope.collideConnected = collide;
    b.setJoint(rope);
    return std::make_pair(a, b);
  };
  auto passing = std::make_shared<std::pair<GameEntity, GameEntity>>(pair(0, false));
  auto pushing = std::make_shared<std::pair<GameEntity, GameEntity>>(pair(20, true));
  steps.seconds(1.0f, [passing, pushing]() mutable {
    CHECK_NEAR(distance(passing->first, passing->second), 0.4f, 0.01f); // overlapping and left alone
    CHECK(distance(pushing->first, pushing->second) > 0.95f);           // pushed apart
  });
  // Turning it on later takes effect: the pair that passed through each other now collides
  steps.step(0, [&engine, passing]() mutable { jointOf(engine, passing->second).collideConnected = true; });
  steps.seconds(1.0f, [passing]() mutable { CHECK(distance(passing->first, passing->second) > 0.95f); });
}

TEST_CASE(joint_survives_rebuilds_and_late_targets) {
  engine.physics().setGravity(glm::vec3(0));
  GameEntity wall = makeWall(engine);
  steps.seconds(1.0f, [wall]() mutable { CHECK(wall.jointAngle() > 20.0f); });
  // The body is built again with new settings: so is the joint (its angle starts again from 0), and it still drives
  steps.step(0, [&engine, wall]() mutable { engine.tryGet<RigidBody>(wall.id())->mass = 900.0f; });
  steps.step(2, [&engine, wall]() mutable {
    CHECK(engine.physics().isJointBuilt(wall.id()));
    CHECK(wall.jointAngle() < 5.0f);
  });
  steps.seconds(1.0f, [wall]() mutable { CHECK(wall.jointAngle() > 20.0f); });
  // Moved by game code: built again where it is
  steps.step(0, [wall]() mutable { wall.setPosition(glm::vec3(0, 5, 0)); });
  steps.seconds(1.0f, [&engine, wall]() mutable {
    CHECK(engine.physics().isJointBuilt(wall.id()));
    CHECK_NEAR(wall.position().y, 5.0f, 0.05f);
    CHECK(wall.jointAngle() > 20.0f);
  });

  // A target that does not exist yet: nothing is built, then the joint appears with the target
  GameEntity gate = engine.create("Gate", Desc("box").at(10, 1, 0).size(1, 2, 0.2f).body(BodyType::Dynamic, BodyShape::Box, 20.0f));
  gate.setJoint(hingeTo("Post", glm::vec3(-0.5f, 0, 0)));
  steps.step(5, [&engine, gate]() { CHECK(!engine.physics().isJointBuilt(gate.id())); });
  steps.step(0, [&engine]() { engine.create("Post", Desc("box").at(9.5f, 1, 0).size(0.2f, 2, 0.2f).body(BodyType::Static)); });
  steps.step(30, [&engine, gate]() { CHECK(engine.physics().isJointBuilt(gate.id())); });
  // The target loses its body: the joint waits again; the body returns: it is built again
  steps.step(0, [&engine]() { engine.removeComponent<RigidBody>(engine.find("Post").id()); });
  steps.step(3, [&engine, gate]() { CHECK(!engine.physics().isJointBuilt(gate.id())); });
  steps.step(0, [&engine]() { engine.getComponentManager().addInPlace<RigidBody>(engine.find("Post").id()).type = BodyType::Static; });
  steps.step(30, [&engine, gate]() { CHECK(engine.physics().isJointBuilt(gate.id())); });

  // Removing the joint frees the body: it falls
  GameEntity free = engine.create("Free", Desc("box").at(30, 5, 0).size(1).body(BodyType::Dynamic));
  free.setJoint(hingeTo("", glm::vec3(0, 0.5f, 0)));
  steps.step(0, [&engine, free]() mutable {
    engine.physics().setGravity(glm::vec3(0, -9.81f, 0));
    free.setVelocity(glm::vec3(0));
  });
  steps.step(30, [&engine, free]() mutable {
    CHECK(engine.physics().isJointBuilt(free.id()));
    CHECK(free.hasJoint());
    CHECK_NEAR(free.position().y, 5.0f, 0.1f); // hanging on its hinge: it turns, it does not fall
    free.removeJoint();
  });
  steps.step(3, [&engine, free]() mutable {
    CHECK(!free.hasJoint());
    CHECK(!engine.physics().isJointBuilt(free.id()));
  });
  steps.step(30, [free]() mutable { CHECK(free.position().y < 4.0f); });

  // Entities with joints, or that others are joined to, are destroyed: nothing dangles
  steps.step(0, [&engine]() { engine.find("Post").destroy(); });
  steps.step(5, [&engine, gate]() { CHECK(!engine.physics().isJointBuilt(gate.id())); });
  steps.step(0, [wall]() mutable { wall.destroy(); });
  steps.step(10, [&engine]() { CHECK(!engine.find("Wall").valid()); });
}

TEST_CASE(joint_scene_round_trip_and_duplicate) {
  Joint joint;
  joint.type = JointType::Slider;
  joint.target = "Rail";
  joint.anchor = glm::vec3(0.5f, 0.25f, 0);
  joint.targetAnchor = glm::vec3(1, 2, 3);
  joint.axis = glm::vec3(1, 0, 0);
  joint.limits = true;
  joint.min = -2.0f;
  joint.max = 4.0f;
  joint.minDistance = 1.0f;
  joint.maxDistance = 6.0f;
  joint.motor = JointMotor::Position;
  joint.motorSpeed = 7.0f;
  joint.motorTarget = 1.25f;
  joint.maxMotorForce = 1234.0f;
  joint.springFrequency = 3.0f;
  joint.springDamping = 0.25f;
  joint.collideConnected = true;
  engine.create("Rail", Desc("box").at(0, 0, 0).size(1).body(BodyType::Static));
  GameEntity cart = engine.create("Cart", Desc("box").at(0, 3, 0).size(1).body(BodyType::Dynamic));
  cart.setJoint(joint);

  steps.step(1, [&engine, cart, joint]() mutable {
    CHECK(cart.has("Joint"));
    const std::string path = testing::tempDir("joint_json") + "/joint.json";
    CHECK(testing::saveWholeScene(engine, path));
    engine.loadScene(path);
  });
  steps.until([&engine]() { return !engine.isLoadingScene(); }, "the scene to load", 300);
  steps.step(2, [&engine, joint]() mutable {
    GameEntity cart = engine.find("Cart");
    CHECK(cart.valid());
    const Joint *loaded = engine.tryGet<Joint>(cart.id());
    CHECK(loaded != nullptr);
    if (!loaded)
      return;
    CHECK(*loaded == joint);
    CHECK(loaded->sameMotor(joint));
    CHECK_EQ(loaded->type, JointType::Slider);
    CHECK_EQ(loaded->motor, JointMotor::Position);
    CHECK_EQ(loaded->target, std::string("Rail"));
    CHECK_NEAR(loaded->targetAnchor, glm::vec3(1, 2, 3), 1e-6f);
    CHECK_NEAR(loaded->maxMotorForce, 1234.0f, 1e-3f);

    // What the editor's Duplicate does: copy the entity as JSON and paste it. The copy keeps the joint.
    auto &serializer = engine.getSceneSerializer();
    const auto pasted = serializer.pasteFromJson(serializer.copyToJson(std::vector<Entity>{cart.id()}));
    CHECK_EQ(pasted.size(), size_t(1));
    if (pasted.empty())
      return;
    const Joint *copy = engine.tryGet<Joint>(pasted.front());
    CHECK(copy != nullptr);
    if (copy) {
      CHECK(*copy == joint);
      CHECK(copy->sameMotor(joint));
    }
  });
}

TEST_CASE(joint_a_missing_body_builds_nothing) {
  // No RigidBody on the entity itself: warned once, nothing built, no crash
  GameEntity ghost = engine.create("Ghost", Desc("box").at(0, 1, 0));
  ghost.setJoint(hingeTo("", glm::vec3(0)));
  steps.step(20, [&engine, ghost]() {
    CHECK(ghost.hasJoint());
    CHECK(!engine.physics().isJointBuilt(ghost.id()));
    CHECK_NEAR(ghost.jointAngle(), 0.0f, 1e-6f);
  });
}

// A fast motor (a wall that sweeps 12 degrees a frame, its tip a unit a frame) still cannot go through a pillar
TEST_CASE(joint_fast_motor_does_not_tunnel) {
  engine.physics().setGravity(glm::vec3(0));
  GameEntity wall = makeWall(engine);
  jointOf(engine, wall).motorSpeed = 720.0f;
  const glm::vec3 pillarPosition(-2.0f + 3.0f * std::cos(glm::radians(100.0f)), 1.0f,
                                 -3.0f * std::sin(glm::radians(100.0f)));
  GameEntity pillar = engine.create("Pillar", Desc("box").at(pillarPosition.x, pillarPosition.y, pillarPosition.z).size(0.6f, 2, 0.6f).body(BodyType::Static));
  auto widest = std::make_shared<float>(0.0f);
  for (int i = 0; i < 60; ++i)
    steps.step(3, [wall, widest]() mutable { *widest = std::max(*widest, wall.jointAngle()); });
  steps.step(0, [wall, pillar, widest]() mutable {
    printf("[joint fast] widest %.1f, now %.1f, gap %.3f\n", *widest, wall.jointAngle(), gapToPillar(wall, pillar));
    CHECK(*widest < 100.0f); // never past the pillar at 100 degrees
    CHECK(wall.jointAngle() > 60.0f && wall.jointAngle() < 100.0f);
    CHECK(gapToPillar(wall, pillar) > -0.08f);
  });
}

// The editor draws a selected joint's Properties section and its overlay (set MODULAR_TEST_SCREENSHOT to a .bmp path to
// look at them)
TEST_CASE(joint_editor_panel_draws) {
  steps.step(0, [&engine]() {
    GameEntity post = engine.create("Post", Desc("box").at(-3, 1, 0).size(0.4f, 2, 0.4f).body(BodyType::Static));
    GameEntity wall = makeWall(engine);
    Joint joint = hingeTo("Post", glm::vec3(-0.5f, 0, 0));
    joint.limits = true;
    joint.motor = JointMotor::Position;
    wall.setJoint(joint);
    auto &editor = engine.getSystemManager().getSystem<EditorUISystem>();
    engine.getSystemManager().getSystem<StateSystem>().setToggle(Toggle::ShowUI, true);
    editor.selectedEntity = wall.id();
    if (std::getenv("MODULAR_TEST_SCREENSHOT"))
      engine.setWindowSize(1500, 1000);
  });
  steps.step(8, [&engine]() {
    if (const char *path = std::getenv("MODULAR_TEST_SCREENSHOT"))
      engine.captureScreenshot(path, 0.0f, false);
  });
  // Another type, and a joint with no target body: both sections draw
  steps.step(0, [&engine]() {
    Joint distance;
    distance.type = JointType::Distance;
    distance.target = "Nobody";
    engine.find("Wall").setJoint(distance);
  });
  steps.step(3, [&engine]() {
    CHECK_EQ(int(glGetError()), int(GL_NO_ERROR));
    engine.getSystemManager().getSystem<StateSystem>().setToggle(Toggle::ShowUI, false);
  });
}
