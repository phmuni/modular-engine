#pragma once
// Pathfinding: a navigation mesh (Recast and Detour, kept inside navigationSystem.cpp) built from the physics world,
// and paths over it that go around obstacles.
//
// build() takes the triangles of the static and kinematic bodies in the bounds (dynamic bodies, sensors and characters
// are not ground and not obstacles: they move or do not block, so a map made from them would be wrong a moment later)
// and voxelizes them, tile by tile. A surface is walkable when its slope is at most `maxSlope`, an agent (a capsule of
// `agentRadius` and `agentHeight`, standing on the ground and stepping over anything up to `stepHeight`) fits above it
// without touching anything, and it is at least `agentRadius` from a wall or an edge. The ground is a surface, not a
// height field: bridges, floors above floors, stairs, ramps and ceilings each get their own polygons, and a path goes
// under a bridge or over it as the ends ask. A ceiling lower than `agentHeight` leaves what is under it unwalkable.
// The bounds cut the world: what lies above their top (a roof) is not seen.
//
// The map is not one piece: a floor reached only by an elevator, or a room closed off, is walkable ground that no path
// from outside leads to. An island smaller than `minRegionArea` square units is dropped, though: the top of a tall box
// or wall is ground that nothing steps up to, and it is what that rule is for (a 2 x 2 post top, a thin wall top).
//
// The map is made of tiles of 64 x 64 voxels (12.8 x 12.8 units at the default cell size), so a big level is many
// small builds, and rebuild() makes again only the tiles a changed part of the level touches. The bounds are rounded
// out to whole tiles on the XZ plane (their height is exact). Heights are quantized to a voxel (a quarter of the cell
// size), and the ground between a polygon's corners follows its detail mesh.
//
// findPath gives the shortest way over the polygons as points on the ground, in world space, with a point wherever the
// way turns or the ground changes its slope (a straight ramp is two points, its feet). A character's Transform sits
// half its height above the ground; points given to the queries may be there or at the feet (see findPath).

#include "foundation/ecs/componentManager.h"
#include "foundation/ecs/systemManager.h"
#include <cstdint>
#include <functional>
#include <glm/glm.hpp>
#include <memory>
#include <optional>
#include <vector>

class PhysicsSystem;

struct NavigationSettings {
  // The voxel's width on the XZ plane (its height is half of that). Smaller sees thinner walls and narrower gaps and
  // costs more time and memory: about 4 times as much for half the size. Keep it at most about `agentRadius`.
  float cellSize = 0.2f;
  float agentRadius = 0.35f;
  float agentHeight = 1.8f;
  float maxSlope = 45.0f;   // degrees
  float stepHeight = 0.35f; // the highest ledge an agent walks up, rounded up to a voxel's height
  // Islands of ground smaller than this, in square units, are left out (see the file's top); 0 keeps every one
  float minRegionArea = 8.0f;
};

class NavigationSystem : public BaseSystem {
public:
  NavigationSystem(ComponentManager &componentManager, PhysicsSystem &physicsSystem);
  ~NavigationSystem();

  // Builds the map for the box between `min` and `max` from the bodies there are now (the bodies of components just
  // added included). False, with a warning, if the box is empty, not finite, would take more than about 2000 tiles
  // (580 x 580 units at the default cell size) or has no geometry to walk on.
  bool build(const glm::vec3 &min, const glm::vec3 &max, const NavigationSettings &settings = {});
  // The same, over the bounds of every static body in the scene; false when there are none
  bool buildFromScene(const NavigationSettings &settings = {});
  // The box between `min` and `max` when both are given, else the bounds of the static bodies
  bool buildIn(const std::optional<glm::vec3> &min, const std::optional<glm::vec3> &max,
               const NavigationSettings &settings = {});
  // Builds the whole map again, with the same bounds and settings, at the next update (after moving obstacles)
  void markDirty() { m_dirty = true; }
  // Builds again, now, only the tiles the box between `min` and `max` touches, for a door opened or a wall that fell;
  // the rest of the map stays as it is. False when there is no map or the box is outside its bounds.
  bool rebuild(const glm::vec3 &min, const glm::vec3 &max);
  void update();

  // Forgets the map, for a scene that replaces the level
  void clear();
  bool isBuilt() const { return m_built; }

  // A path from `from` to `to` as points on the ground, the first at `from` and the last at `to` (each moved to the
  // nearest walkable ground within a unit if it is not on it). The ground a point stands on is looked for from
  // `stepHeight` above it down to `agentHeight` below it: pass the feet or the middle of a character, and of two
  // floors one above the other it takes the one the point is at, not the other. Empty when there is no map, no
  // walkable ground near either end, or no way between them (never a part of the way).
  std::vector<glm::vec3> findPath(const glm::vec3 &from, const glm::vec3 &to);
  // Whether the point is over walkable ground (found as for findPath, without moving it sideways)
  bool isWalkable(const glm::vec3 &point) const;
  // The point itself on the ground if walkable, else the nearest walkable ground within `maxDistance` sideways
  std::optional<glm::vec3> getNearestWalkable(const glm::vec3 &point, float maxDistance = 1.0f) const;

  // The map, for drawing it: calls `visit` with the corners (3 to 6, in order around it) of each
  // polygon with a corner within `radius` of `center` sideways. The pointer is valid during the call.
  void forEachPolygon(const glm::vec3 &center, float radius,
                      const std::function<void(const glm::vec3 *corners, int count)> &visit) const;
  const NavigationSettings &getSettings() const { return m_settings; }
  // The last path findPath found (empty if it found none)
  const std::vector<glm::vec3> &getLastPath() const { return m_lastPath; }
  // How long the last build took
  float getBuildMilliseconds() const { return m_buildMs; }

private:
  struct Impl;

  ComponentManager &m_componentManager;
  PhysicsSystem &m_physics;
  std::unique_ptr<Impl> m_impl;

  NavigationSettings m_settings;
  glm::vec3 m_min{0.0f}, m_max{0.0f};
  bool m_built = false;
  bool m_dirty = false;
  float m_buildMs = 0.0f;
  std::vector<glm::vec3> m_lastPath;
};
