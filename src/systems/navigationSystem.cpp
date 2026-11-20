// The navigation grid and A* over it.

#include "systems/navigationSystem.h"

#include "components/rigidBody.h"
#include "foundation/core/logger.h"
#include "systems/physicsSystem.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <queue>

namespace {
constexpr int kMaxCells = 4'000'000;
constexpr float kSnapDistance = 1.0f; // how far findPath moves an end point that is not on walkable ground
constexpr float kSqrt2 = 1.41421356f;
} // namespace

glm::vec3 NavigationSystem::getCellCenter(int x, int z) const {
  const float size = m_settings.cellSize;
  return glm::vec3(m_grid.origin.x + (static_cast<float>(x) + 0.5f) * size, m_height[index(x, z)],
                   m_grid.origin.z + (static_cast<float>(z) + 0.5f) * size);
}

bool NavigationSystem::build(const glm::vec3 &min, const glm::vec3 &max, const NavigationSettings &settings) {
  const auto started = std::chrono::steady_clock::now();
  m_built = false;
  m_dirty = false;
  m_lastPath.clear();
  m_settings = settings;
  m_settings.cellSize = std::max(settings.cellSize, 0.01f);
  m_min = glm::min(min, max);
  m_max = glm::max(min, max);
  const glm::vec3 size = m_max - m_min;
  const double cells = std::ceil(size.x / m_settings.cellSize) * std::ceil(size.z / m_settings.cellSize);
  if (size.x <= 0.0f || size.z <= 0.0f || cells > kMaxCells) {
    LOG_W("[Navigation] Not building: the bounds hold %.0f cells (1 to %d wanted); use a smaller area or cell size",
          cells, kMaxCells);
    return false;
  }

  m_physics.syncBodies(); // a map built in a game's first frame should see the level
  m_grid.origin = m_min;
  m_grid.width = static_cast<int>(std::ceil(size.x / m_settings.cellSize));
  m_grid.depth = static_cast<int>(std::ceil(size.z / m_settings.cellSize));
  const size_t count = static_cast<size_t>(m_grid.width) * static_cast<size_t>(m_grid.depth);
  m_height.assign(count, 0.0f);
  m_walkable.assign(count, 0);
  m_stamp.assign(count, 0);
  m_cost.assign(count, 0.0f);
  m_parent.assign(count, -1);
  m_search = 0;

  // The ground is what stays put and is not a sensor
  const auto isGround = [this](Entity entity) {
    const RigidBody *body = m_componentManager.tryGet<RigidBody>(entity);
    return body && body->type != BodyType::Dynamic && !body->sensor;
  };
  const float rayTop = m_max.y + 0.1f;
  const float rayLength = size.y + 0.2f;
  const float minNormalY = std::cos(glm::radians(m_settings.maxSlope));
  // The agent's capsule starts a step above the ground, so a ledge it can climb is not an obstacle
  const float clearance = std::min(m_settings.stepHeight, m_settings.agentHeight * 0.5f);
  std::vector<uint8_t> hasGround(count, 0);
  for (int z = 0; z < m_grid.depth; ++z)
    for (int x = 0; x < m_grid.width; ++x) {
      const glm::vec3 center = getCellCenter(x, z); // its height is not known yet
      const auto hit = m_physics.raycast(glm::vec3(center.x, rayTop, center.z), glm::vec3(0.0f, -1.0f, 0.0f),
                                         rayLength, isGround);
      if (!hit)
        continue;
      const size_t cell = static_cast<size_t>(index(x, z));
      hasGround[cell] = 1;
      m_height[cell] = hit->point.y;
      if (hit->normal.y < minNormalY)
        continue;
      const glm::vec3 base(center.x, hit->point.y + clearance, center.z);
      const bool blocked = m_physics.isCapsuleBlocked(base, m_settings.agentHeight - clearance, m_settings.agentRadius);
      m_walkable[cell] = blocked ? 0 : 1;
    }

  // The top of a wall is ground too, but a ridge: with a drop of more than a step to both sides along an axis it is
  // too narrow to stand on, so it is not walkable (a platform at least two cells across keeps its middle)
  const auto drops = [&](int x, int z, float height) {
    return !inGrid(x, z) || !hasGround[index(x, z)] || m_height[index(x, z)] < height - m_settings.stepHeight;
  };
  for (int z = 0; z < m_grid.depth; ++z)
    for (int x = 0; x < m_grid.width; ++x) {
      const size_t cell = static_cast<size_t>(index(x, z));
      const float height = m_height[cell];
      if (m_walkable[cell] && ((drops(x - 1, z, height) && drops(x + 1, z, height)) ||
                               (drops(x, z - 1, height) && drops(x, z + 1, height))))
        m_walkable[cell] = 0;
    }

  m_built = true;
  m_buildMs = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - started).count();
  return true;
}

bool NavigationSystem::buildIn(const std::optional<glm::vec3> &min, const std::optional<glm::vec3> &max,
                               const NavigationSettings &settings) {
  return min && max ? build(*min, *max, settings) : buildFromScene(settings);
}

bool NavigationSystem::buildFromScene(const NavigationSettings &settings) {
  m_physics.syncBodies();
  glm::vec3 min, max;
  if (!m_physics.getStaticBounds(min, max)) {
    LOG_W("[Navigation] Not building: the scene has no static bodies to take the bounds from");
    return false;
  }
  return build(min, max, settings);
}

void NavigationSystem::update() {
  if (m_dirty && m_built)
    build(m_min, m_max, m_settings);
}

void NavigationSystem::clear() {
  m_built = false;
  m_dirty = false;
  m_lastPath.clear();
}

bool NavigationSystem::cellOf(const glm::vec3 &point, int &x, int &z) const {
  if (!m_built)
    return false;
  x = static_cast<int>(std::floor((point.x - m_grid.origin.x) / m_settings.cellSize));
  z = static_cast<int>(std::floor((point.z - m_grid.origin.z) / m_settings.cellSize));
  return inGrid(x, z);
}

bool NavigationSystem::isWalkable(const glm::vec3 &point) const {
  int x, z;
  return cellOf(point, x, z) && m_walkable[index(x, z)];
}

bool NavigationSystem::nearestCell(const glm::vec3 &point, float maxDistance, int &x, int &z) const {
  if (!m_built)
    return false;
  const int centerX = static_cast<int>(std::floor((point.x - m_grid.origin.x) / m_settings.cellSize));
  const int centerZ = static_cast<int>(std::floor((point.z - m_grid.origin.z) / m_settings.cellSize));
  if (isCellWalkable(centerX, centerZ)) {
    x = centerX;
    z = centerZ;
    return true;
  }
  const int reach = static_cast<int>(std::ceil(maxDistance / m_settings.cellSize));
  float best = maxDistance * maxDistance;
  bool found = false;
  for (int dz = -reach; dz <= reach; ++dz)
    for (int dx = -reach; dx <= reach; ++dx) {
      if (!isCellWalkable(centerX + dx, centerZ + dz))
        continue;
      const glm::vec3 cell = getCellCenter(centerX + dx, centerZ + dz);
      const float distance = (cell.x - point.x) * (cell.x - point.x) + (cell.z - point.z) * (cell.z - point.z);
      if (distance <= best) {
        best = distance;
        x = centerX + dx;
        z = centerZ + dz;
        found = true;
      }
    }
  return found;
}

std::optional<glm::vec3> NavigationSystem::getNearestWalkable(const glm::vec3 &point, float maxDistance) const {
  int x, z;
  if (!nearestCell(point, maxDistance, x, z))
    return std::nullopt;
  const glm::vec3 center = getCellCenter(x, z);
  int ownX, ownZ;
  if (cellOf(point, ownX, ownZ) && ownX == x && ownZ == z)
    return glm::vec3(point.x, center.y, point.z);
  return center;
}

bool NavigationSystem::canStep(int x, int z, int dx, int dz) const {
  const int toX = x + dx, toZ = z + dz;
  if (!isCellWalkable(x, z) || !isCellWalkable(toX, toZ))
    return false;
  if (dx != 0 && dz != 0) // a diagonal needs the cells beside it open, so it never cuts a corner
    return canStep(x, z, dx, 0) && canStep(x, z, 0, dz) && canStep(toX, z, 0, dz) && canStep(x, toZ, dx, 0);
  return std::abs(m_height[index(toX, toZ)] - m_height[index(x, z)]) <= m_settings.stepHeight;
}

bool NavigationSystem::hasLineOfSight(int fromX, int fromZ, int toX, int toZ) const {
  const float lengthX = static_cast<float>(toX - fromX), lengthZ = static_cast<float>(toZ - fromZ);
  // Samples a quarter cell apart, so the walk visits every cell the line crosses; each move to another cell must be
  // one an agent could make
  const int samples = std::max(1, static_cast<int>(std::ceil(std::sqrt(lengthX * lengthX + lengthZ * lengthZ) * 4.0f)));
  int x = fromX, z = fromZ;
  for (int i = 1; i <= samples; ++i) {
    const float t = static_cast<float>(i) / static_cast<float>(samples);
    const float u = static_cast<float>(fromX) + lengthX * t, v = static_cast<float>(fromZ) + lengthZ * t;
    // Between cell middles the line must keep clear of the blocked cells around, not only those it passes through
    for (int cz = static_cast<int>(std::floor(v)); cz <= static_cast<int>(std::ceil(v)); ++cz)
      for (int cx = static_cast<int>(std::floor(u)); cx <= static_cast<int>(std::ceil(u)); ++cx)
        if (!isCellWalkable(cx, cz))
          return false;
    const int nextX = static_cast<int>(std::floor(u + 0.5f));
    const int nextZ = static_cast<int>(std::floor(v + 0.5f));
    if (nextX == x && nextZ == z)
      continue;
    if (!canStep(x, z, nextX - x, nextZ - z))
      return false;
    x = nextX;
    z = nextZ;
  }
  return true;
}

std::vector<int> NavigationSystem::search(int startX, int startZ, int goalX, int goalZ) {
  const int start = index(startX, startZ), goal = index(goalX, goalZ);
  ++m_search;
  using Entry = std::pair<float, int>; // estimated total cost, cell
  std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> open;
  // The octile distance: what the cheapest walk on an empty grid costs
  const auto estimate = [&](int x, int z) {
    const float dx = static_cast<float>(std::abs(x - goalX)), dz = static_cast<float>(std::abs(z - goalZ));
    return (std::max(dx, dz) + (kSqrt2 - 1.0f) * std::min(dx, dz)) * m_settings.cellSize;
  };
  m_stamp[start] = m_search;
  m_cost[start] = 0.0f;
  m_parent[start] = -1;
  open.push({estimate(startX, startZ), start});

  while (!open.empty()) {
    const auto [total, cell] = open.top();
    open.pop();
    const int x = cell % m_grid.width, z = cell / m_grid.width;
    if (cell == goal)
      break;
    if (total > m_cost[cell] + estimate(x, z) + 1e-4f)
      continue; // a cheaper way to this cell was found after this entry was queued
    for (int dz = -1; dz <= 1; ++dz)
      for (int dx = -1; dx <= 1; ++dx) {
        if ((dx == 0 && dz == 0) || !canStep(x, z, dx, dz))
          continue;
        const int next = index(x + dx, z + dz);
        const float cost = m_cost[cell] + (dx != 0 && dz != 0 ? kSqrt2 : 1.0f) * m_settings.cellSize;
        if (m_stamp[next] == m_search && m_cost[next] <= cost)
          continue;
        m_stamp[next] = m_search;
        m_cost[next] = cost;
        m_parent[next] = cell;
        open.push({cost + estimate(x + dx, z + dz), next});
      }
  }
  std::vector<int> cells;
  if (m_stamp[goal] != m_search)
    return cells;
  for (int cell = goal; cell != -1; cell = m_parent[cell])
    cells.push_back(cell);
  std::reverse(cells.begin(), cells.end());
  return cells;
}

std::vector<glm::vec3> NavigationSystem::findPath(const glm::vec3 &from, const glm::vec3 &to) {
  m_lastPath.clear();
  int startX, startZ, goalX, goalZ;
  if (!nearestCell(from, kSnapDistance, startX, startZ) || !nearestCell(to, kSnapDistance, goalX, goalZ))
    return m_lastPath;
  const std::vector<int> cells = search(startX, startZ, goalX, goalZ);
  if (cells.empty())
    return m_lastPath;

  // Skips ahead to the farthest cell in sight from each point kept
  m_lastPath.push_back(*getNearestWalkable(from, kSnapDistance));
  size_t anchor = 0;
  while (anchor + 1 < cells.size()) {
    size_t next = anchor + 1;
    for (size_t i = cells.size() - 1; i > anchor + 1; --i)
      if (hasLineOfSight(cells[anchor] % m_grid.width, cells[anchor] / m_grid.width, cells[i] % m_grid.width,
                         cells[i] / m_grid.width)) {
        next = i;
        break;
      }
    anchor = next;
    if (anchor + 1 < cells.size())
      m_lastPath.push_back(getCellCenter(cells[anchor] % m_grid.width, cells[anchor] / m_grid.width));
  }
  m_lastPath.push_back(*getNearestWalkable(to, kSnapDistance));
  return m_lastPath;
}
