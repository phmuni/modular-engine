#pragma once
// Pathfinding: a grid over the XZ plane, built from the physics world, and paths over it that go around obstacles.
//
// build() casts a ray down through the middle of every cell to find the ground there. A cell is walkable when there is
// ground, its slope is at most `maxSlope`, and an agent (a capsule of `agentRadius` and `agentHeight`, standing on the
// ground and stepping over anything up to `stepHeight`) fits without touching a static or kinematic body. Dynamic
// bodies (crates) are not obstacles: they move, so a map made from them would be wrong a moment later.
//
// The map is a height field: one ground height per cell, the topmost surface inside the bounds. Bridges, floors above
// floors and ceilings are not told apart; give build() bounds whose top is below a ceiling. Keep cellSize at most
// about twice agentRadius, or an agent can slip through a wall thinner than a cell.
//
// findPath runs A* over the cells (8 neighbors, never cutting a corner of a blocked cell) and then removes the points
// a straight walk over walkable cells makes unnecessary. Points are on the ground, in world space; a character's
// Transform sits half its height above that.

#include "foundation/ecs/componentManager.h"
#include "foundation/ecs/systemManager.h"
#include <cstdint>
#include <glm/glm.hpp>
#include <optional>
#include <vector>

class PhysicsSystem;

struct NavigationSettings {
  float cellSize = 0.5f;
  float agentRadius = 0.35f;
  float agentHeight = 1.8f;
  float maxSlope = 45.0f;  // degrees
  float stepHeight = 0.35f; // the highest ledge between neighbor cells an agent walks up
};

class NavigationSystem : public BaseSystem {
public:
  NavigationSystem(ComponentManager &componentManager, PhysicsSystem &physicsSystem)
      : m_componentManager(componentManager), m_physics(physicsSystem) {}

  // Builds the map for the box between `min` and `max` from the bodies there are now (the bodies of components just
  // added included). False if the box is empty or would take more than a few million cells.
  bool build(const glm::vec3 &min, const glm::vec3 &max, const NavigationSettings &settings = {});
  // The same, over the bounds of every static body in the scene; false when there are none
  bool buildFromScene(const NavigationSettings &settings = {});
  // The box between `min` and `max` when both are given, else the bounds of the static bodies
  bool buildIn(const std::optional<glm::vec3> &min, const std::optional<glm::vec3> &max,
               const NavigationSettings &settings = {});
  // Builds the map again, with the same bounds and settings, at the next update (after moving obstacles)
  void markDirty() { m_dirty = true; }
  void update();

  // Forgets the map, for a scene that replaces the level
  void clear();
  bool isBuilt() const { return m_built; }

  // A path from `from` to `to` as points on the ground, the first at `from` and the last at `to` (each moved to the
  // nearest walkable point within a unit if it is not on one). Empty when there is no map, no walkable point near
  // either end, or no way between them.
  std::vector<glm::vec3> findPath(const glm::vec3 &from, const glm::vec3 &to);
  // Whether the point is over a walkable cell (its height is not looked at: the map has one ground per cell)
  bool isWalkable(const glm::vec3 &point) const;
  // The point itself on the ground if walkable, else the middle of the nearest walkable cell within `maxDistance`
  std::optional<glm::vec3> getNearestWalkable(const glm::vec3 &point, float maxDistance = 1.0f) const;

  // The map, for drawing it
  struct Grid {
    glm::vec3 origin{0.0f}; // the corner with the lowest x and z, at the bounds' bottom
    int width = 0, depth = 0;
  };
  const Grid &getGrid() const { return m_grid; }
  const NavigationSettings &getSettings() const { return m_settings; }
  bool isCellWalkable(int x, int z) const { return inGrid(x, z) && m_walkable[index(x, z)]; }
  glm::vec3 getCellCenter(int x, int z) const;
  // The last path findPath found (empty if it found none)
  const std::vector<glm::vec3> &getLastPath() const { return m_lastPath; }
  // How long the last build took
  float getBuildMilliseconds() const { return m_buildMs; }

private:
  ComponentManager &m_componentManager;
  PhysicsSystem &m_physics;

  NavigationSettings m_settings;
  Grid m_grid;
  glm::vec3 m_min{0.0f}, m_max{0.0f};
  std::vector<float> m_height;     // ground height per cell
  std::vector<uint8_t> m_walkable; // per cell
  bool m_built = false;
  bool m_dirty = false;
  float m_buildMs = 0.0f;
  std::vector<glm::vec3> m_lastPath;

  // A* scratch, reused between searches; a cell's entry counts only when its stamp is the current search's
  std::vector<uint32_t> m_stamp;
  std::vector<float> m_cost;
  std::vector<int> m_parent;
  uint32_t m_search = 0;

  int index(int x, int z) const { return z * m_grid.width + x; }
  bool inGrid(int x, int z) const { return x >= 0 && z >= 0 && x < m_grid.width && z < m_grid.depth; }
  bool cellOf(const glm::vec3 &point, int &x, int &z) const;
  // Whether an agent walks from cell (x, z) one step (dx, dz) to a neighbor, a diagonal only between open corners
  bool canStep(int x, int z, int dx, int dz) const;
  bool hasLineOfSight(int fromX, int fromZ, int toX, int toZ) const;
  bool nearestCell(const glm::vec3 &point, float maxDistance, int &x, int &z) const;
  std::vector<int> search(int startX, int startZ, int goalX, int goalZ);
};
