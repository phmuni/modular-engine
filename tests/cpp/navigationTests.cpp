// The navigation mesh through the C++ API: levels with floors above floors, stairs, ramps, ceilings, gaps, rotated and
// mirrored bodies, kinematic and dynamic ones, and tiles rebuilt one box at a time.

#include "components/rigidBody.h"
#include "describe.h"
#include "foundation/core/engine.h"
#include "testing.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <memory>
#include <vector>

namespace {
using Path = std::vector<glm::vec3>;
const float kInf = std::numeric_limits<float>::infinity();

float pathLength(const Path &path) {
  float length = 0.0f;
  for (size_t i = 1; i < path.size(); ++i)
    length += glm::distance(path[i - 1], path[i]);
  return length;
}
float highest(const Path &path) {
  float y = -kInf;
  for (const glm::vec3 &p : path)
    y = std::max(y, p.y);
  return y;
}
float lowest(const Path &path) {
  float y = kInf;
  for (const glm::vec3 &p : path)
    y = std::min(y, p.y);
  return y;
}

// A level's floor: top at y = 0, `halfX` and `halfZ` either side of the origin
GameEntity floor(Engine &engine, float halfX, float halfZ) {
  return engine.create("Floor", Desc("box").at(0, -0.5f, 0).size(halfX * 2.0f, 1, halfZ * 2.0f).body(BodyType::Static));
}

// A slab whose top surface runs from (x0, y0) to (x1, y1) at z, `width` across and `thickness` deep below the surface:
// a ramp when it rises, a deck when it is level
GameEntity slab(Engine &engine, glm::vec2 from, glm::vec2 to, float z, float width, float thickness = 0.2f) {
  const glm::vec2 run = to - from;
  const float length = glm::length(run);
  const glm::vec2 normal(-run.y / length, run.x / length);
  const glm::vec2 center = (from + to) * 0.5f - normal * (thickness * 0.5f);
  GameEntity entity = engine.create("Slab", Desc("box").at(center.x, center.y, z).size(length, thickness, width).body(BodyType::Static));
  entity.setRotation(glm::vec3(0, 0, glm::degrees(std::atan2(run.y, run.x))));
  return entity;
}

// Whether `path` runs from its first point to its last without a hop (points that far apart are a jump)
bool continuous(const Path &path, float maxGap) {
  for (size_t i = 1; i < path.size(); ++i)
    if (glm::distance(path[i - 1], path[i]) > maxGap)
      return false;
  return true;
}

// A corridor 7.5 units wide and 32 long (x), walls 2 high on both sides, the floor's top at y = 0
void corridor(Engine &engine) {
  floor(engine, 16.0f, 4.0f);
  engine.create("WallA", Desc("box").at(0, 1, 4).size(32, 2, 0.5f).body(BodyType::Static));
  engine.create("WallB", Desc("box").at(0, 1, -4).size(32, 2, 0.5f).body(BodyType::Static));
}

const glm::vec3 kBoundsMin(-30, -1, -30), kBoundsMax(30, 8, 30);
bool buildDefault(Engine &engine) { return engine.navigation().build(kBoundsMin, kBoundsMax); }
} // namespace

// A bridge over a floor: under it the path stays on the floor, to the deck it goes up a ramp, and a point resolves to
// the level it is at
TEST_CASE(navigation_bridge_over_floor) {
  floor(engine, 28.0f, 12.0f);
  slab(engine, {-6, 3}, {6, 3}, 0, 4, 0.3f);           // the deck: top at 3, 2.7 above the floor underneath
  slab(engine, {-18, 0}, {-6, 3}, 0, 4, 0.3f);         // up to it from the west
  slab(engine, {6, 3}, {18, 0}, 0, 4, 0.3f);           // and down to the east
  CHECK(engine.navigation().build(glm::vec3(-28, -1, -12), glm::vec3(28, 8, 12)));
  NavigationSystem &nav = engine.navigation();

  // Under the deck: across it from one side to the other stays on the floor
  const Path under = nav.findPath(glm::vec3(0, 0, -8), glm::vec3(0, 0, 8));
  CHECK(under.size() >= 2);
  CHECK(highest(under) < 0.3f);
  CHECK(pathLength(under) < 17.5f); // straight through (16)

  // Onto the deck from the floor at the west end: up the ramp
  const Path over = nav.findPath(glm::vec3(-24, 0, 0), glm::vec3(0, 3, 0));
  CHECK(over.size() >= 3);
  if (!over.empty()) {
    CHECK_NEAR(over.back(), glm::vec3(0, 3, 0), 0.15f);
    CHECK_NEAR(over.front().y, 0.0f, 0.15f);
    CHECK(continuous(over, 30.0f));
    // It climbs without ever going back down on the way
    for (size_t i = 1; i < over.size(); ++i)
      CHECK(over[i].y >= over[i - 1].y - 0.1f);
    CHECK(pathLength(over) < 27.5f); // 24 across and 3 up is 24.2 in a straight line; no hop through the floor under
  }

  // From the deck to the east end of the floor: down the other ramp
  const Path down = nav.findPath(glm::vec3(0, 3, 0), glm::vec3(24, 0, 0));
  CHECK(down.size() >= 3);
  if (!down.empty())
    CHECK_NEAR(down.back().y, 0.0f, 0.15f);

  // Each level's points resolve to their own: the floor under the deck, the deck, a character's middle on either
  const auto floorPoint = nav.getNearestWalkable(glm::vec3(0, 0, 0));
  const auto deckPoint = nav.getNearestWalkable(glm::vec3(0, 3, 0));
  const auto middleUnder = nav.getNearestWalkable(glm::vec3(0, 0.9f, 0));
  const auto middleOver = nav.getNearestWalkable(glm::vec3(0, 3.9f, 0));
  CHECK(floorPoint && deckPoint && middleUnder && middleOver);
  if (floorPoint && deckPoint && middleUnder && middleOver) {
    CHECK_NEAR(floorPoint->y, 0.0f, 0.15f);
    CHECK_NEAR(deckPoint->y, 3.0f, 0.15f);
    CHECK_NEAR(middleUnder->y, 0.0f, 0.15f);
    CHECK_NEAR(middleOver->y, 3.0f, 0.15f);
    CHECK_NEAR(floorPoint->x, 0.0f, 0.01f);
  }
  CHECK(nav.isWalkable(glm::vec3(0, 0, 0)));
  CHECK(nav.isWalkable(glm::vec3(0, 3, 0)));
  CHECK(!nav.isWalkable(glm::vec3(0, 2.4f, 0))); // in the air between the two, past a step under the deck and over the floor's reach
  // A path to the deck that starts under it does not climb through it
  const Path fromUnder = nav.findPath(glm::vec3(0, 0.9f, 0), glm::vec3(0, 3.9f, 0));
  CHECK(fromUnder.size() >= 3);
  if (!fromUnder.empty()) {
    CHECK_NEAR(fromUnder.front().y, 0.0f, 0.15f);
    CHECK_NEAR(fromUnder.back().y, 3.0f, 0.15f);
  }
}

// Two floors one above the other, joined by stairs: a way up, and none without the stairs. Also measures how close the
// path's height keeps to the stairs.
TEST_CASE(navigation_stacked_floors_with_stairs) {
  engine.create("Lower", Desc("box").at(0, -0.5f, 0).size(40, 1, 20).body(BodyType::Static));
  engine.create("Upper", Desc("box").at(-7, 3.8f, 0).size(26, 0.4f, 20).body(BodyType::Static)); // top at 4, x -20 to 6
  // 16 steps of 0.25 by 0.75, from x = 18 (low) to x = 6 (top at 4), 3 wide
  for (int k = 1; k <= 16; ++k) {
    const float top = 0.25f * static_cast<float>(k);
    const float x0 = 18.0f - 0.75f * static_cast<float>(k);
    engine.create("Step", Desc("box").at(x0 + 0.375f, top * 0.5f, 0).size(0.75f, top, 3).body(BodyType::Static));
  }
  CHECK(engine.navigation().build(glm::vec3(-20, -1, -10), glm::vec3(20, 9, 10)));
  NavigationSystem &nav = engine.navigation();

  const Path up = nav.findPath(glm::vec3(-15, 0, 0), glm::vec3(-15, 4, 0));
  CHECK(up.size() >= 4);
  if (!up.empty()) {
    CHECK_NEAR(up.front().y, 0.0f, 0.15f);
    CHECK_NEAR(up.back(), glm::vec3(-15, 4, 0), 0.15f);
    CHECK(highest(up) < 4.2f && lowest(up) > -0.2f);
    float farthestEast = -kInf;
    for (const glm::vec3 &p : up)
      farthestEast = std::max(farthestEast, p.x);
    CHECK(farthestEast > 15.0f); // round by the foot of the stairs
    CHECK(pathLength(up) > 45.0f && pathLength(up) < 75.0f);

    // The height error: the walked line (straight between the points) against the stairs' own profile, a step line
    std::vector<glm::vec2> profile{{18, 0}};
    for (int k = 1; k <= 16; ++k) {
      const float top = 0.25f * static_cast<float>(k);
      profile.push_back({18.0f - 0.75f * static_cast<float>(k - 1), top});
      profile.push_back({18.0f - 0.75f * static_cast<float>(k), top});
    }
    const auto distanceToProfile = [&profile](glm::vec2 p) {
      float best = kInf;
      for (size_t i = 1; i < profile.size(); ++i) {
        const glm::vec2 a = profile[i - 1], b = profile[i], ab = b - a;
        const float t = glm::clamp(glm::dot(p - a, ab) / glm::dot(ab, ab), 0.0f, 1.0f);
        best = std::min(best, glm::distance(p, a + ab * t));
      }
      return best;
    };
    float worst = 0.0f;
    for (size_t i = 1; i < up.size(); ++i) {
      const int samples = 40;
      for (int s = 0; s <= samples; ++s) {
        const glm::vec3 p = glm::mix(up[i - 1], up[i], static_cast<float>(s) / samples);
        if (p.x > 6.4f && p.x < 17.6f && std::abs(p.z) < 1.0f)
          worst = std::max(worst, distanceToProfile(glm::vec2(p.x, p.y)));
      }
    }
    std::printf("navigation: the path's distance to the stairs' profile: at most %.3f units\n", worst);
    CHECK(worst < 0.3f); // about one riser: the surface rounds the steps' edges
  }
  // Each floor's point resolves to its own
  const auto lower = nav.getNearestWalkable(glm::vec3(-15, 0.9f, 3));
  const auto upper = nav.getNearestWalkable(glm::vec3(-15, 4.9f, 3));
  CHECK(lower && upper);
  if (lower && upper) {
    CHECK_NEAR(lower->y, 0.0f, 0.15f);
    CHECK_NEAR(upper->y, 4.0f, 0.15f);
  }
  // The other way too
  const Path down = nav.findPath(glm::vec3(-15, 4, 5), glm::vec3(-15, 0, 5));
  CHECK(down.size() >= 4);
}

TEST_CASE(navigation_stacked_floors_without_stairs_have_no_path) {
  engine.create("Lower", Desc("box").at(0, -0.5f, 0).size(40, 1, 20).body(BodyType::Static));
  engine.create("Upper", Desc("box").at(-7, 3.8f, 0).size(26, 0.4f, 20).body(BodyType::Static));
  CHECK(engine.navigation().build(glm::vec3(-20, -1, -10), glm::vec3(20, 9, 10)));
  NavigationSystem &nav = engine.navigation();
  CHECK(nav.findPath(glm::vec3(-15, 0, 0), glm::vec3(-15, 4, 0)).empty());
  CHECK(nav.findPath(glm::vec3(-15, 0.9f, 0), glm::vec3(-15, 4.9f, 0)).empty());
  CHECK(!nav.findPath(glm::vec3(-15, 0, 0), glm::vec3(15, 0, 0)).empty()); // on the lower floor, all the way
  CHECK(!nav.findPath(glm::vec3(-15, 4, 0), glm::vec3(-5, 4, 5)).empty()); // on the upper one
  CHECK(nav.isWalkable(glm::vec3(-15, 4, 0))); // a floor nothing leads to is still ground (a lift would)
}

// A ceiling lower than the agent blocks what is under it, a higher one does not
TEST_CASE(navigation_ceiling) {
  floor(engine, 14.0f, 6.0f);
  GameEntity ceiling = engine.create("Ceiling", Desc("box").at(0, 1.4f, 0).size(6, 0.2f, 12).body(BodyType::Static)); // bottom at 1.3
  CHECK(engine.navigation().build(glm::vec3(-14, -1, -6), glm::vec3(14, 8, 6)));
  CHECK(engine.navigation().findPath(glm::vec3(-10, 0, 0), glm::vec3(10, 0, 0)).empty());
  CHECK(!engine.navigation().isWalkable(glm::vec3(0, 0, 0)));
  CHECK(engine.navigation().isWalkable(glm::vec3(-10, 0, 0)));

  ceiling.setPosition(glm::vec3(0, 2.4f, 0)); // bottom at 2.3, over the agent's 1.8
  CHECK(engine.navigation().build(glm::vec3(-14, -1, -6), glm::vec3(14, 8, 6)));
  const Path through = engine.navigation().findPath(glm::vec3(-10, 0, 0), glm::vec3(10, 0, 0));
  CHECK(through.size() >= 2);
  CHECK(engine.navigation().isWalkable(glm::vec3(0, 0, 0)));
  if (!through.empty())
    CHECK(highest(through) < 0.2f);

  // Bounds that stop below the ceiling do not see it at all
  ceiling.setPosition(glm::vec3(0, 1.4f, 0));
  CHECK(engine.navigation().build(glm::vec3(-14, -1, -6), glm::vec3(14, 1.0f, 6)));
  CHECK(!engine.navigation().findPath(glm::vec3(-10, 0, 0), glm::vec3(10, 0, 0)).empty());
}

// A ramp is walked up when it is not steeper than maxSlope; the same ramp is a wall when it is
TEST_CASE(navigation_slope) {
  floor(engine, 20.0f, 8.0f);
  engine.create("Platform", Desc("box").at(4, 1.5f, 0).size(8, 3, 8).body(BodyType::Static)); // x 0 to 8, top at 3
  slab(engine, {-5.196f, 0}, {0, 3}, 0, 4); // 30 degrees
  NavigationSettings steep;
  steep.maxSlope = 25.0f;
  CHECK(engine.navigation().build(glm::vec3(-20, -1, -8), glm::vec3(20, 8, 8), steep));
  CHECK(engine.navigation().findPath(glm::vec3(-12, 0, 0), glm::vec3(4, 3, 0)).empty());

  NavigationSettings gentle;
  gentle.maxSlope = 35.0f;
  CHECK(engine.navigation().build(glm::vec3(-20, -1, -8), glm::vec3(20, 8, 8), gentle));
  const Path up = engine.navigation().findPath(glm::vec3(-12, 0, 0), glm::vec3(4, 3, 0));
  CHECK(up.size() >= 3);
  if (up.size() >= 3) {
    CHECK_NEAR(up.back().y, 3.0f, 0.15f);
    // The height error on the ramp: the walked line against the ramp's surface y = (x + 5.196) * tan(30 degrees)
    float worst = 0.0f;
    for (size_t i = 1; i < up.size(); ++i)
      for (int s = 0; s <= 40; ++s) {
        const glm::vec3 p = glm::mix(up[i - 1], up[i], static_cast<float>(s) / 40.0f);
        if (p.x > -4.8f && p.x < -0.4f && std::abs(p.z) < 1.2f)
          worst = std::max(worst, std::abs(p.y - (p.x + 5.196f) * 0.57735f));
      }
    std::printf("navigation: the path's height error on a 30 degree ramp: at most %.3f units\n", worst);
    CHECK(worst < 0.15f);
  }
}

// A ledge of up to stepHeight is walked over, a higher one is a wall
TEST_CASE(navigation_step) {
  corridor(engine);
  GameEntity step = engine.create("Step", Desc("box").at(0, 0.15f, 0).size(1, 0.3f, 7.6f).body(BodyType::Static)); // top at 0.3
  CHECK(buildDefault(engine));
  const Path over = engine.navigation().findPath(glm::vec3(-10, 0, 0), glm::vec3(10, 0, 0));
  CHECK(over.size() >= 2);
  CHECK(!over.empty() && highest(over) > 0.25f && highest(over) < 0.45f);

  step.setPosition(glm::vec3(0, 0.3f, 0));
  step.setScale(glm::vec3(1, 0.6f, 7.6f)); // top at 0.6
  CHECK(buildDefault(engine));
  CHECK(engine.navigation().findPath(glm::vec3(-10, 0, 0), glm::vec3(10, 0, 0)).empty());
  // A higher stepHeight takes it
  NavigationSettings tall;
  tall.stepHeight = 0.7f;
  CHECK(engine.navigation().build(kBoundsMin, kBoundsMax, tall));
  CHECK(!engine.navigation().findPath(glm::vec3(-10, 0, 0), glm::vec3(10, 0, 0)).empty());
}

// A gap between two walls narrower than the agent is not crossed, a wider one is
TEST_CASE(navigation_gap) {
  floor(engine, 12.0f, 10.0f);
  GameEntity north = engine.create("North", Desc("box").size(0.5f, 2, 1).body(BodyType::Static));
  GameEntity south = engine.create("South", Desc("box").size(0.5f, 2, 1).body(BodyType::Static));
  // The walls run from the floor's edge (z = 10) to the gap's edge
  const auto place = [&](float gap) {
    const float length = 10.0f - gap * 0.5f;
    north.setScale(glm::vec3(0.5f, 2, length));
    north.setPosition(glm::vec3(0, 1, gap * 0.5f + length * 0.5f));
    south.setScale(glm::vec3(0.5f, 2, length));
    south.setPosition(glm::vec3(0, 1, -gap * 0.5f - length * 0.5f));
  };
  place(0.5f); // an agent is 0.7 wide
  CHECK(engine.navigation().build(glm::vec3(-12, -1, -10), glm::vec3(12, 4, 10)));
  CHECK(engine.navigation().findPath(glm::vec3(-8, 0, 0), glm::vec3(8, 0, 0)).empty());
  place(1.5f);
  CHECK(engine.navigation().build(glm::vec3(-12, -1, -10), glm::vec3(12, 4, 10)));
  const Path through = engine.navigation().findPath(glm::vec3(-8, 0, 0), glm::vec3(8, 0, 0));
  CHECK(through.size() >= 2);
  // A wider agent does not fit through the wider gap
  NavigationSettings fat;
  fat.agentRadius = 1.0f;
  CHECK(engine.navigation().build(glm::vec3(-12, -1, -10), glm::vec3(12, 4, 10), fat));
  CHECK(engine.navigation().findPath(glm::vec3(-8, 0, 0), glm::vec3(8, 0, 0)).empty());
}

// rebuild(min, max): a wall taken away opens the way, one put up closes it; the tiles elsewhere keep working and the
// paths that cross tile borders go on without a break
TEST_CASE(navigation_rebuild_tiles) {
  floor(engine, 30.0f, 30.0f);
  auto wall = std::make_shared<GameEntity>(engine.create("Wall", Desc("box").at(0, 1, 0).size(0.5f, 2, 60).body(BodyType::Static)));
  CHECK(engine.navigation().build(glm::vec3(-30, -1, -30), glm::vec3(30, 4, 30)));
  auto before = std::make_shared<Path>(engine.navigation().findPath(glm::vec3(-20, 0, 20), glm::vec3(20, 0, 20)));
  CHECK(engine.navigation().findPath(glm::vec3(-20, 0, 0), glm::vec3(20, 0, 0)).empty()); // walled off
  CHECK(engine.navigation().findPath(glm::vec3(-20, 0, 20), glm::vec3(-5, 0, 28)).size() >= 2);
  CHECK(before->empty()); // the wall runs the floor's whole width

  wall->destroy();
  steps.step(2, [&engine, wall]() {
    NavigationSystem &nav = engine.navigation();
    // Nothing changes until the map is rebuilt
    CHECK(nav.findPath(glm::vec3(-20, 0, 0), glm::vec3(20, 0, 0)).empty());
    // Outside the bounds of the map: nothing to do
    CHECK(!nav.rebuild(glm::vec3(100, 0, 100), glm::vec3(110, 5, 110)));
    CHECK(nav.rebuild(glm::vec3(-2, -1, -31), glm::vec3(2, 4, 31)));
    const Path open = nav.findPath(glm::vec3(-20, 0, 0), glm::vec3(20, 0, 0));
    CHECK(open.size() >= 2);
    CHECK_NEAR(pathLength(open), 40.0f, 0.3f);
    CHECK(continuous(open, 45.0f));
    CHECK(highest(open) < 0.15f && lowest(open) > -0.15f);
    // The same as a map built from scratch
    const Path fresh = [&]() {
      CHECK(nav.build(glm::vec3(-30, -1, -30), glm::vec3(30, 4, 30)));
      return nav.findPath(glm::vec3(-20, 0, 0), glm::vec3(20, 0, 0));
    }();
    CHECK(fresh == open);
    // A long way over the same tiles, in every direction, has no hole
    for (float z = -26.0f; z <= 26.0f; z += 13.0f) {
      const Path along = nav.findPath(glm::vec3(-27, 0, z), glm::vec3(27, 0, -z));
      CHECK(along.size() >= 2);
      CHECK(continuous(along, 80.0f));
      CHECK(highest(along) < 0.15f);
    }
  });
  // A wall put up closes it again, in the tiles it stands in only
  steps.step(1, [&engine]() {
    engine.create("NewWall", Desc("box").at(15, 1, 0).size(0.5f, 2, 60).body(BodyType::Static));
  });
  steps.step(1, [&engine]() {
    NavigationSystem &nav = engine.navigation();
    CHECK(!nav.findPath(glm::vec3(-20, 0, 0), glm::vec3(20, 0, 0)).empty()); // the map is still the old one
    CHECK(nav.rebuild(glm::vec3(14, -1, -31), glm::vec3(16, 4, 31)));
    CHECK(nav.findPath(glm::vec3(-20, 0, 0), glm::vec3(20, 0, 0)).empty());
    CHECK(!nav.findPath(glm::vec3(-20, 0, 0), glm::vec3(10, 0, 0)).empty()); // west of it is still one piece
    CHECK(!nav.findPath(glm::vec3(20, 0, -20), glm::vec3(20, 0, 20)).empty()); // and east of it
    // A box on the wall's one side is not seen as the other tiles' business
    CHECK(nav.rebuild(glm::vec3(-30, -1, -30), glm::vec3(-29, 4, -29)));
    CHECK(nav.findPath(glm::vec3(-20, 0, 0), glm::vec3(20, 0, 0)).empty());
  });
}

// Bodies turned, scaled, mirrored and shaped as meshes are ground all the same
TEST_CASE(navigation_rotated_scaled_and_mesh_bodies) {
  // Six floors in a row (x centers 0, 30, ... 150), each with its top at y = 0
  GameEntity rotated = engine.create("Rotated", Desc("box").at(0, -0.5f, 0).size(20, 1, 10).body(BodyType::Static));
  rotated.setRotation(glm::vec3(0, 90, 0));
  engine.create("MirroredX", Desc("box").at(30, -0.5f, 0).size(-20, 1, 20).body(BodyType::Static));
  engine.create("MirroredY", Desc("box").at(60, -0.5f, 0).size(20, -1, 20).body(BodyType::Static));
  engine.create("MeshFloor", Desc("box").at(90, -0.5f, 0).size(20, 1, 20).body(BodyType::Static, BodyShape::Mesh));
  engine.create("MeshMirroredX", Desc("box").at(120, -0.5f, 0).size(-20, 1, 20).body(BodyType::Static, BodyShape::Mesh));
  engine.create("MeshMirroredY", Desc("box").at(150, -0.5f, 0).size(20, -1, 20).body(BodyType::Static, BodyShape::Mesh));
  GameEntity meshRotated = engine.create("MeshRotated", Desc("box").at(180, -0.5f, 0).size(20, 1, 8).body(BodyType::Static, BodyShape::Mesh));
  meshRotated.setRotation(glm::vec3(0, 33, 0));
  GameEntity meshTilted = engine.create("MeshTilted", Desc("box").at(210, -0.5f, 0).size(20, 1, 20).body(BodyType::Static, BodyShape::Mesh));
  meshTilted.setRotation(glm::vec3(0, 0, 180)); // upside down: its top is its bottom, a floor the same
  CHECK(engine.navigation().build(glm::vec3(-12, -2, -12), glm::vec3(222, 4, 12)));
  NavigationSystem &nav = engine.navigation();
  const char *names[] = {"rotated", "mirrored x", "mirrored y", "mesh", "mesh mirrored x", "mesh mirrored y", "mesh rotated", "mesh upside down"};
  const float centers[] = {0, 30, 60, 90, 120, 150, 180, 210};
  for (int i = 0; i < 8; ++i) {
    const bool walkable = nav.isWalkable(glm::vec3(centers[i], 0, 0));
    if (!walkable)
      testing::fail(__FILE__, __LINE__, std::string("the ") + names[i] + " floor is not walkable");
    const auto near = nav.getNearestWalkable(glm::vec3(centers[i] + 2.0f, 0.9f, 1.0f));
    if (!near)
      testing::fail(__FILE__, __LINE__, std::string("nothing near on the ") + names[i] + " floor");
    if (near)
      CHECK_NEAR(near->y, 0.0f, 0.15f);
    const Path across = nav.findPath(glm::vec3(centers[i] - 5.0f, 0, -2), glm::vec3(centers[i] + 5.0f, 0, 2));
    if (across.size() < 2)
      testing::fail(__FILE__, __LINE__, std::string("no path across the ") + names[i] + " floor");
  }
  // The rotated one is 10 along x after a quarter turn: 20 along z
  CHECK(nav.isWalkable(glm::vec3(0, 0, 9)));
  CHECK(!nav.isWalkable(glm::vec3(9, 0, 0)));
}

// Dynamic bodies and sensors are not obstacles; kinematic ones are
TEST_CASE(navigation_body_kinds) {
  corridor(engine);
  engine.create("Crate", Desc("box").at(-4, 1.0f, 0).size(2, 2, 7).body(BodyType::Dynamic, BodyShape::Box, 10.0f));
  GameEntity sensor = engine.create("Trigger", Desc("box").at(4, 1, 0).size(2, 2, 7.6f).body(BodyType::Static));
  engine.get<RigidBody>(sensor.id()).sensor = true;
  CHECK(buildDefault(engine));
  NavigationSystem &nav = engine.navigation();
  CHECK(!nav.findPath(glm::vec3(-12, 0, 0), glm::vec3(12, 0, 0)).empty());
  CHECK(nav.isWalkable(glm::vec3(-4, 0, 0)));
  CHECK(nav.isWalkable(glm::vec3(4, 0, 0)));

  engine.create("Door", Desc("box").at(0, 1, 0).size(1, 2, 7.6f).body(BodyType::Kinematic));
  CHECK(buildDefault(engine));
  CHECK(engine.navigation().findPath(glm::vec3(-12, 0, 0), glm::vec3(12, 0, 0)).empty());
  CHECK(!engine.navigation().isWalkable(glm::vec3(0, 0, 0)));
  CHECK(!engine.navigation().findPath(glm::vec3(-12, 0, 0), glm::vec3(-2.5f, 0, 0)).empty());
}

// A character's middle and its feet give the same path, and its own body is no obstacle
TEST_CASE(navigation_character_middle_and_feet) {
  floor(engine, 15.0f, 15.0f);
  engine.create("Wall", Desc("box").at(0, 1, 0).size(1, 2, 12).body(BodyType::Static));
  GameEntity character = engine.create("Hero", Desc().at(-8, 0.95f, 0).character());
  engine.create("Wall2", Desc("box").at(-8, 1, 6).size(2, 2, 1).body(BodyType::Static));
  CHECK(buildDefault(engine));
  NavigationSystem &nav = engine.navigation();
  CHECK(nav.isWalkable(glm::vec3(-8, 0.95f, 0))); // where it stands
  const glm::vec3 goal(8, 0, 0);
  const Path fromFeet = nav.findPath(glm::vec3(-8, 0, 0), goal);
  const Path fromMiddle = nav.findPath(glm::vec3(-8, 0.9f, 0), goal);
  const Path fromTransform = nav.findPath(character.position(), glm::vec3(8, 0.9f, 0));
  CHECK(fromFeet.size() >= 3);
  CHECK(fromFeet == fromMiddle);
  CHECK(fromFeet == fromTransform);
}

// Nothing to build from, absurd bounds, clear() and the same answer every time
TEST_CASE(navigation_empty_absurd_and_deterministic) {
  NavigationSystem &nav = engine.navigation();
  CHECK(!nav.build(glm::vec3(-10, -1, -10), glm::vec3(10, 5, 10))); // no bodies at all
  CHECK(!nav.buildFromScene());
  CHECK(!nav.isBuilt());
  CHECK(nav.findPath(glm::vec3(0), glm::vec3(1, 0, 1)).empty());
  CHECK(!nav.isWalkable(glm::vec3(0)));
  CHECK(!nav.getNearestWalkable(glm::vec3(0)).has_value());
  CHECK(!nav.rebuild(glm::vec3(-1), glm::vec3(1)));

  floor(engine, 15.0f, 15.0f);
  engine.create("Wall", Desc("box").at(0, 1, 0).size(1, 2, 20).body(BodyType::Static));
  engine.create("Pillar", Desc("box").at(0, 1, 12).size(3, 2, 3).body(BodyType::Static));
  const float huge = 1e9f;
  CHECK(!nav.build(glm::vec3(-huge, -1, -huge), glm::vec3(huge, 5, huge)));
  CHECK(!nav.build(glm::vec3(-3e38f, -1, -3e38f), glm::vec3(3e38f, 5, 3e38f)));
  CHECK(!nav.build(glm::vec3(-kInf, -1, -10), glm::vec3(kInf, 5, 10)));
  CHECK(!nav.build(glm::vec3(std::nanf(""), -1, -10), glm::vec3(10, 5, 10)));
  CHECK(!nav.build(glm::vec3(-10, -1, 3), glm::vec3(10, 5, 3))); // no width
  CHECK(!nav.build(glm::vec3(100, -1, 100), glm::vec3(110, 5, 110))); // nothing there
  NavigationSettings tiny;
  tiny.cellSize = 0.001f; // clamped to the smallest voxel, and then too many tiles for 400 x 400
  CHECK(!nav.build(glm::vec3(-200, -1, -200), glm::vec3(200, 5, 200), tiny));
  CHECK(!nav.isBuilt());

  CHECK(nav.buildFromScene());
  CHECK(nav.isBuilt());
  const Path first = nav.findPath(glm::vec3(-10, 0, -5), glm::vec3(10, 0, 5));
  CHECK(first.size() >= 3);
  CHECK(nav.buildFromScene());
  CHECK(first == nav.findPath(glm::vec3(-10, 0, -5), glm::vec3(10, 0, 5)));
  CHECK(nav.findPath(glm::vec3(-10, 0, -5), glm::vec3(10, 0, 5)) == nav.getLastPath());
  nav.markDirty();
  nav.update();
  CHECK(first == nav.findPath(glm::vec3(-10, 0, -5), glm::vec3(10, 0, 5)));
  CHECK(nav.getBuildMilliseconds() > 0.0f);

  // Polygons to draw
  int polygons = 0;
  nav.forEachPolygon(glm::vec3(0), 100.0f, [&polygons](const glm::vec3 *corners, int count) {
    CHECK(count >= 3 && count <= 6);
    CHECK(corners != nullptr);
    ++polygons;
  });
  CHECK(polygons > 3);
  int far = 0;
  nav.forEachPolygon(glm::vec3(1000, 0, 1000), 10.0f, [&far](const glm::vec3 *, int) { ++far; });
  CHECK_EQ(far, 0);

  nav.clear();
  CHECK(!nav.isBuilt());
  CHECK(nav.findPath(glm::vec3(-10, 0, -5), glm::vec3(10, 0, 5)).empty());
  CHECK(!nav.isWalkable(glm::vec3(-10, 0, 0)));
  CHECK(nav.getLastPath().empty());
  nav.forEachPolygon(glm::vec3(0), 100.0f, [](const glm::vec3 *, int) { CHECK(false); });
}

// Islands smaller than minRegionArea are dropped, larger ones (cut off from the rest) stay: the top of a tall box has
// nothing leading up to it, a hall reached by a lift does
TEST_CASE(navigation_islands) {
  floor(engine, 20.0f, 20.0f);
  engine.create("Post", Desc("box").at(-10, 1.5f, -10).size(3, 3, 3).body(BodyType::Static));      // a 3 x 3 top
  engine.create("Hall", Desc("box").at(10, 1.5f, 10).size(10, 3, 10).body(BodyType::Static));      // a 10 x 10 top
  CHECK(engine.navigation().build(glm::vec3(-20, -1, -20), glm::vec3(20, 8, 20)));
  CHECK(!engine.navigation().isWalkable(glm::vec3(-10, 3, -10)));
  CHECK(engine.navigation().isWalkable(glm::vec3(10, 3, 10)));
  CHECK(engine.navigation().findPath(glm::vec3(-15, 0, 0), glm::vec3(10, 3, 10)).empty());
  NavigationSettings keepAll;
  keepAll.minRegionArea = 0.0f;
  CHECK(engine.navigation().build(glm::vec3(-20, -1, -20), glm::vec3(20, 8, 20), keepAll));
  CHECK(engine.navigation().isWalkable(glm::vec3(-10, 3, -10)));
  NavigationSettings big;
  big.minRegionArea = 200.0f;
  CHECK(engine.navigation().build(glm::vec3(-20, -1, -20), glm::vec3(20, 8, 20), big));
  CHECK(!engine.navigation().isWalkable(glm::vec3(10, 3, 10)));
  CHECK(engine.navigation().isWalkable(glm::vec3(-15, 0, 0)));
}
