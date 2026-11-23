// The navigation mesh: tiles built by Recast from the physics world's triangles, queried with Detour.

#include "systems/navigationSystem.h"

#include "foundation/core/logger.h"
#include "systems/physicsSystem.h"

#include <DetourNavMesh.h>
#include <DetourNavMeshBuilder.h>
#include <DetourNavMeshQuery.h>
#include <Recast.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <glm/gtc/type_ptr.hpp>
#include <limits>
#include <memory>
#include <numeric>

namespace {
// A tile is this many voxels across (without its border): a build of a few milliseconds, and a tile's memory stays the
// same whatever the cell size is. A level of 580 x 580 units at the default cell size is 2000 tiles.
constexpr int kTileVoxels = 64;
// The polygon reference of Detour holds the tile and the polygon in 22 bits: 2^11 tiles of up to 2^11 polygons each
constexpr int kMaxTiles = 2048;
constexpr int kMaxVoxelHeight = 8000; // a span's top is 13 bits in Recast
constexpr float kSnapDistance = 1.0f; // how far findPath moves an end point that is not on walkable ground
constexpr int kMaxPathPolys = 4096;
constexpr int kMaxStraightPoints = 8192;
constexpr unsigned short kFlagWalk = 1; // a polygon without it is left out of every query (a dropped island)
constexpr float kStraightTolerance = 0.06f; // points of a path within this of the line between their neighbors go

struct RecastDeleters {
  void operator()(rcHeightfield *p) const { rcFreeHeightField(p); }
  void operator()(rcCompactHeightfield *p) const { rcFreeCompactHeightfield(p); }
  void operator()(rcContourSet *p) const { rcFreeContourSet(p); }
  void operator()(rcPolyMesh *p) const { rcFreePolyMesh(p); }
  void operator()(rcPolyMeshDetail *p) const { rcFreePolyMeshDetail(p); }
};
template <typename T> using RecastPtr = std::unique_ptr<T, RecastDeleters>;

float distanceToSegment(const glm::vec3 &p, const glm::vec3 &a, const glm::vec3 &b) {
  const glm::vec3 ab = b - a;
  const float length2 = glm::dot(ab, ab);
  const float t = length2 > 0.0f ? glm::clamp(glm::dot(p - a, ab) / length2, 0.0f, 1.0f) : 0.0f;
  return glm::distance(p, a + ab * t);
}
} // namespace

struct NavigationSystem::Impl {
  PhysicsSystem &physics;
  dtNavMesh *mesh = nullptr;
  dtNavMeshQuery *query = nullptr;
  dtQueryFilter filter;

  // What the settings and bounds make of the map, fixed at build()
  NavigationSettings settings;
  glm::vec3 origin{0.0f}; // the bounds' lowest corner
  glm::vec3 boundsMin{0.0f}, boundsMax{0.0f};
  float voxel = 0.2f, voxelHeight = 0.1f;
  float tileWorld = 12.8f;
  int tilesX = 0, tilesZ = 0;
  int walkableHeight = 0, walkableClimb = 0, walkableRadius = 0, borderSize = 0;
  bool warnedHeight = false;

  explicit Impl(PhysicsSystem &p) : physics(p) {
    filter.setIncludeFlags(kFlagWalk);
    filter.setExcludeFlags(0);
  }
  ~Impl() { release(); }

  const dtNavMesh &view() const { return *mesh; } // the read-only side: its getTile is the public one

  void release() {
    dtFreeNavMeshQuery(query);
    dtFreeNavMesh(mesh);
    query = nullptr;
    mesh = nullptr;
  }

  // The tiles' grid for the bounds and settings; false when it would hold too many tiles
  bool setup(const NavigationSettings &s, const glm::vec3 &min, const glm::vec3 &max) {
    settings = s;
    boundsMin = min;
    boundsMax = max;
    origin = min;
    voxel = s.cellSize;
    voxelHeight = voxel * 0.25f;
    tileWorld = voxel * static_cast<float>(kTileVoxels);
    walkableHeight = std::max(static_cast<int>(std::ceil(s.agentHeight / voxelHeight)), 3);
    walkableClimb = std::max(static_cast<int>(std::ceil(s.stepHeight / voxelHeight)), 0);
    walkableRadius = std::max(static_cast<int>(std::ceil(s.agentRadius / voxel)), 0);
    borderSize = walkableRadius + 3;
    const glm::vec3 size = max - min;
    const double countX = std::ceil(static_cast<double>(size.x) / tileWorld);
    const double countZ = std::ceil(static_cast<double>(size.z) / tileWorld);
    if (countX * countZ > kMaxTiles)
      return false;
    tilesX = static_cast<int>(countX);
    tilesZ = static_cast<int>(countZ);
    return true;
  }

  bool createMesh() {
    release();
    const int count = tilesX * tilesZ;
    int tileBits = 0;
    while ((1 << tileBits) < count)
      ++tileBits;
    dtNavMeshParams params{};
    params.orig[0] = origin.x;
    params.orig[1] = origin.y;
    params.orig[2] = origin.z;
    params.tileWidth = tileWorld;
    params.tileHeight = tileWorld;
    params.maxTiles = 1 << tileBits;
    params.maxPolys = 1 << (22 - tileBits);
    mesh = dtAllocNavMesh();
    query = dtAllocNavMeshQuery();
    if (!mesh || !query || dtStatusFailed(mesh->init(&params)) || dtStatusFailed(query->init(mesh, 65535))) {
      release();
      return false;
    }
    return true;
  }

  enum class TileResult { Empty, Built, Failed };

  // Makes the tile (tx, tz) from the triangles there are now and puts it in the mesh in place of the one it has
  TileResult buildTile(int tx, int tz) {
    if (const dtTileRef old = mesh->getTileRefAt(tx, tz, 0))
      mesh->removeTile(old, nullptr, nullptr);

    const float border = static_cast<float>(borderSize) * voxel;
    glm::vec3 low(origin.x + static_cast<float>(tx) * tileWorld, boundsMin.y, origin.z + static_cast<float>(tz) * tileWorld);
    glm::vec3 high(low.x + tileWorld, boundsMax.y, low.z + tileWorld);
    low.x -= border;
    low.z -= border;
    high.x += border;
    high.z += border;

    std::vector<glm::vec3> vertices;
    std::vector<int> indices;
    physics.collectTriangles(low, high, vertices, indices);
    if (indices.empty())
      return TileResult::Empty;

    // The tile reaches from the lowest to the highest triangle (inside the bounds), not the whole bounds
    float lowY = std::numeric_limits<float>::max(), highY = std::numeric_limits<float>::lowest();
    for (const glm::vec3 &v : vertices) {
      lowY = std::min(lowY, v.y);
      highY = std::max(highY, v.y);
    }
    low.y = std::max(lowY, boundsMin.y);
    high.y = std::min(highY, boundsMax.y);
    if (high.y < low.y)
      return TileResult::Empty;
    if ((high.y - low.y) / voxelHeight > static_cast<float>(kMaxVoxelHeight)) {
      if (!warnedHeight)
        LOG_W("[Navigation] A tile is %.0f units tall, more than %d voxels: leaving it out; use a lower top or a bigger "
              "cell size",
              high.y - low.y, kMaxVoxelHeight);
      warnedHeight = true;
      return TileResult::Failed;
    }

    rcContext context(false);
    rcConfig config{};
    config.cs = voxel;
    config.ch = voxelHeight;
    config.walkableSlopeAngle = settings.maxSlope;
    config.walkableHeight = walkableHeight;
    config.walkableClimb = walkableClimb;
    config.walkableRadius = walkableRadius;
    config.maxEdgeLen = static_cast<int>(12.0f / voxel);
    config.maxSimplificationError = 1.3f;
    // Islands are judged by area over the whole map afterwards (markIslands): here only specks of a few voxels go
    config.minRegionArea = 9;
    config.mergeRegionArea = 400;
    config.maxVertsPerPoly = 6;
    config.tileSize = kTileVoxels;
    config.borderSize = borderSize;
    config.width = kTileVoxels + borderSize * 2;
    config.height = kTileVoxels + borderSize * 2;
    config.detailSampleDist = voxel * 1.5f;
    config.detailSampleMaxError = voxel * 0.25f;
    rcVcopy(config.bmin, glm::value_ptr(low));
    rcVcopy(config.bmax, glm::value_ptr(high));
    config.bmin[0] = origin.x + static_cast<float>(tx) * tileWorld - border;
    config.bmin[2] = origin.z + static_cast<float>(tz) * tileWorld - border;
    config.bmax[0] = config.bmin[0] + static_cast<float>(config.width) * voxel;
    config.bmax[2] = config.bmin[2] + static_cast<float>(config.height) * voxel;

    const int triangleCount = static_cast<int>(indices.size() / 3);
    RecastPtr<rcHeightfield> heightfield(rcAllocHeightfield());
    if (!heightfield || !rcCreateHeightfield(&context, *heightfield, config.width, config.height, config.bmin, config.bmax,
                                             config.cs, config.ch))
      return TileResult::Failed;
    std::vector<unsigned char> areas(static_cast<size_t>(triangleCount), 0);
    rcMarkWalkableTriangles(&context, config.walkableSlopeAngle, glm::value_ptr(vertices[0]),
                            static_cast<int>(vertices.size()), indices.data(), triangleCount, areas.data());
    if (!rcRasterizeTriangles(&context, glm::value_ptr(vertices[0]), static_cast<int>(vertices.size()), indices.data(),
                              areas.data(), triangleCount, *heightfield, config.walkableClimb))
      return TileResult::Failed;
    vertices.clear();
    vertices.shrink_to_fit();

    rcFilterLowHangingWalkableObstacles(&context, config.walkableClimb, *heightfield);
    rcFilterLedgeSpans(&context, config.walkableHeight, config.walkableClimb, *heightfield);
    rcFilterWalkableLowHeightSpans(&context, config.walkableHeight, *heightfield);

    RecastPtr<rcCompactHeightfield> compact(rcAllocCompactHeightfield());
    if (!compact || !rcBuildCompactHeightfield(&context, config.walkableHeight, config.walkableClimb, *heightfield, *compact))
      return TileResult::Failed;
    heightfield.reset();
    if (!rcErodeWalkableArea(&context, config.walkableRadius, *compact) || !rcBuildDistanceField(&context, *compact) ||
        !rcBuildRegions(&context, *compact, config.borderSize, config.minRegionArea, config.mergeRegionArea))
      return TileResult::Failed;

    RecastPtr<rcContourSet> contours(rcAllocContourSet());
    if (!contours || !rcBuildContours(&context, *compact, config.maxSimplificationError, config.maxEdgeLen, *contours))
      return TileResult::Failed;
    if (contours->nconts == 0)
      return TileResult::Empty;
    RecastPtr<rcPolyMesh> polyMesh(rcAllocPolyMesh());
    if (!polyMesh || !rcBuildPolyMesh(&context, *contours, config.maxVertsPerPoly, *polyMesh))
      return TileResult::Failed;
    RecastPtr<rcPolyMeshDetail> detail(rcAllocPolyMeshDetail());
    if (!detail || !rcBuildPolyMeshDetail(&context, *polyMesh, *compact, config.detailSampleDist,
                                          config.detailSampleMaxError, *detail))
      return TileResult::Failed;
    if (polyMesh->npolys == 0)
      return TileResult::Empty;
    if (polyMesh->nverts >= 0xffff || polyMesh->npolys > (1 << (22 - tileBitsOf()))) {
      LOG_W("[Navigation] Tile (%d, %d) holds too many polygons: leaving it out; use a bigger cell size", tx, tz);
      return TileResult::Failed;
    }
    for (int i = 0; i < polyMesh->npolys; ++i) {
      polyMesh->areas[i] = 0;
      polyMesh->flags[i] = kFlagWalk;
    }
    // A voxel column's top is its surface rounded up to the next voxel, so every height is too high by half a voxel
    // on average: the whole tile moves down by that
    const float lower = voxelHeight * 0.5f;
    polyMesh->bmin[1] -= lower;
    polyMesh->bmax[1] -= lower;
    for (int i = 0; i < detail->nverts; ++i)
      detail->verts[i * 3 + 1] -= lower;

    dtNavMeshCreateParams params{};
    params.verts = polyMesh->verts;
    params.vertCount = polyMesh->nverts;
    params.polys = polyMesh->polys;
    params.polyAreas = polyMesh->areas;
    params.polyFlags = polyMesh->flags;
    params.polyCount = polyMesh->npolys;
    params.nvp = polyMesh->nvp;
    params.detailMeshes = detail->meshes;
    params.detailVerts = detail->verts;
    params.detailVertsCount = detail->nverts;
    params.detailTris = detail->tris;
    params.detailTriCount = detail->ntris;
    params.walkableHeight = settings.agentHeight;
    params.walkableRadius = settings.agentRadius;
    params.walkableClimb = static_cast<float>(walkableClimb) * voxelHeight;
    params.tileX = tx;
    params.tileY = tz;
    params.tileLayer = 0;
    rcVcopy(params.bmin, polyMesh->bmin);
    rcVcopy(params.bmax, polyMesh->bmax);
    params.cs = config.cs;
    params.ch = config.ch;
    params.buildBvTree = true;

    unsigned char *data = nullptr;
    int dataSize = 0;
    if (!dtCreateNavMeshData(&params, &data, &dataSize))
      return TileResult::Failed;
    if (dtStatusFailed(mesh->addTile(data, dataSize, DT_TILE_FREE_DATA, 0, nullptr))) {
      dtFree(data);
      return TileResult::Failed;
    }
    return TileResult::Built;
  }

  int tileBitsOf() const {
    int bits = 0;
    while ((1 << bits) < mesh->getMaxTiles())
      ++bits;
    return bits;
  }

  size_t polygonCount() const {
    size_t count = 0;
    for (int i = 0; i < mesh->getMaxTiles(); ++i)
      if (const dtMeshTile *tile = view().getTile(i); tile && tile->header)
        count += static_cast<size_t>(tile->header->polyCount);
    return count;
  }

  // Takes the polygons of every island (polygons joined by their links, across tiles too) of less than `minRegionArea`
  // square units out of the queries: the top of a tall box has no step up to it
  void markIslands() {
    const int maxTiles = mesh->getMaxTiles();
    std::vector<size_t> offset(static_cast<size_t>(maxTiles) + 1, 0);
    for (int i = 0; i < maxTiles; ++i) {
      const dtMeshTile *tile = view().getTile(i);
      offset[static_cast<size_t>(i) + 1] =
          offset[static_cast<size_t>(i)] + (tile && tile->header ? static_cast<size_t>(tile->header->polyCount) : 0);
    }
    std::vector<size_t> parent(offset.back());
    std::iota(parent.begin(), parent.end(), size_t(0));
    const auto find = [&parent](size_t x) {
      while (parent[x] != x) {
        parent[x] = parent[parent[x]];
        x = parent[x];
      }
      return x;
    };
    std::vector<double> area(offset.back(), 0.0);
    for (int i = 0; i < maxTiles; ++i) {
      const dtMeshTile *tile = view().getTile(i);
      if (!tile || !tile->header)
        continue;
      for (int p = 0; p < tile->header->polyCount; ++p) {
        const dtPoly &poly = tile->polys[p];
        const size_t id = offset[static_cast<size_t>(i)] + static_cast<size_t>(p);
        double twice = 0.0;
        for (int v = 0; v < poly.vertCount; ++v) {
          const float *a = &tile->verts[poly.verts[v] * 3];
          const float *b = &tile->verts[poly.verts[(v + 1) % poly.vertCount] * 3];
          twice += static_cast<double>(a[0]) * b[2] - static_cast<double>(b[0]) * a[2];
        }
        area[id] = std::abs(twice) * 0.5;
        for (unsigned int link = poly.firstLink; link != DT_NULL_LINK; link = tile->links[link].next) {
          unsigned int salt, otherTile, otherPoly;
          mesh->decodePolyId(tile->links[link].ref, salt, otherTile, otherPoly);
          const size_t other = offset[otherTile] + otherPoly;
          const size_t a = find(id), b = find(other);
          if (a != b)
            parent[std::max(a, b)] = std::min(a, b); // the lowest index is the root: the same on every run
        }
      }
    }
    std::vector<double> total(offset.back(), 0.0);
    for (size_t id = 0; id < area.size(); ++id)
      total[find(id)] += area[id];
    for (int i = 0; i < maxTiles; ++i) {
      const dtMeshTile *tile = view().getTile(i);
      if (!tile || !tile->header)
        continue;
      const dtPolyRef base = mesh->getPolyRefBase(tile);
      for (int p = 0; p < tile->header->polyCount; ++p) {
        const size_t id = offset[static_cast<size_t>(i)] + static_cast<size_t>(p);
        const bool keep = total[find(id)] >= static_cast<double>(settings.minRegionArea);
        mesh->setPolyFlags(base | static_cast<dtPolyRef>(p), keep ? kFlagWalk : 0);
      }
    }
  }

  struct Located {
    dtPolyRef ref = 0;
    glm::vec3 point{0.0f};
    bool over = false; // the point is straight above or below the polygon, not beside it
  };

  // The walkable ground at `p`: a polygon surface from `stepHeight` above it down to `agentHeight` below, within
  // `snap` sideways. The one straight over or under the point wins (the nearest in height); with none, the nearest.
  bool locate(const glm::vec3 &p, float snap, bool mustBeOver, Located &result) const {
    const float climb = static_cast<float>(walkableClimb) * voxelHeight;
    const float below = settings.agentHeight, above = climb;
    const float tolerance = voxelHeight * 0.5f;
    const float center[3] = {p.x, p.y - (below - above) * 0.5f, p.z};
    const float half[3] = {snap, (below + above) * 0.5f + tolerance, snap};
    dtPolyRef polys[64];
    int count = 0;
    if (dtStatusFailed(query->queryPolygons(center, half, &filter, polys, &count, 64)))
      return false;
    float best = std::numeric_limits<float>::max();
    bool found = false;
    for (int i = 0; i < count; ++i) {
      float closest[3];
      bool over = false;
      if (dtStatusFailed(query->closestPointOnPoly(polys[i], glm::value_ptr(p), closest, &over)))
        continue;
      if (mustBeOver && !over)
        continue;
      const float dx = closest[0] - p.x, dz = closest[2] - p.z, dy = closest[1] - p.y;
      if (dx * dx + dz * dz > snap * snap + 1e-6f || dy < -below - tolerance || dy > above + tolerance)
        continue;
      const float cost = over ? std::abs(dy) : 1000.0f + dx * dx + dz * dz + dy * dy;
      if (cost < best) {
        best = cost;
        found = true;
        result.ref = polys[i];
        result.point = glm::vec3(closest[0], closest[1], closest[2]);
        result.over = over;
      }
    }
    return found;
  }
};

NavigationSystem::NavigationSystem(ComponentManager &componentManager, PhysicsSystem &physicsSystem)
    : m_componentManager(componentManager), m_physics(physicsSystem), m_impl(std::make_unique<Impl>(physicsSystem)) {}

NavigationSystem::~NavigationSystem() = default;

bool NavigationSystem::build(const glm::vec3 &min, const glm::vec3 &max, const NavigationSettings &settings) {
  const auto started = std::chrono::steady_clock::now();
  m_built = false;
  m_dirty = false;
  m_lastPath.clear();
  m_impl->release();
  m_settings = settings;
  m_settings.cellSize = std::max(settings.cellSize, 0.02f);
  m_settings.agentRadius = std::max(settings.agentRadius, 0.0f);
  m_settings.agentHeight = std::max(settings.agentHeight, 0.1f);
  m_settings.minRegionArea = std::max(settings.minRegionArea, 0.0f);
  m_min = glm::min(min, max);
  m_max = glm::max(min, max);
  const glm::vec3 size = m_max - m_min;
  if (!std::isfinite(size.x) || !std::isfinite(size.y) || !std::isfinite(size.z) || !std::isfinite(m_min.x) ||
      !std::isfinite(m_min.y) || !std::isfinite(m_min.z)) {
    LOG_W("[Navigation] Not building: the bounds are not finite numbers");
    return false;
  }
  if (size.x <= 0.0f || size.z <= 0.0f) {
    LOG_W("[Navigation] Not building: the bounds are empty (%.2f x %.2f units on the ground)", size.x, size.z);
    return false;
  }
  if (!m_impl->setup(m_settings, m_min, m_max)) {
    const double tiles = std::ceil(static_cast<double>(size.x) / m_impl->tileWorld) *
                         std::ceil(static_cast<double>(size.z) / m_impl->tileWorld);
    LOG_W("[Navigation] Not building: the bounds hold %.0f tiles of %.1f units (at most %d); use a smaller area or a "
          "bigger cell size",
          tiles, m_impl->tileWorld, kMaxTiles);
    return false;
  }

  m_physics.syncBodies(); // a map built in a game's first frame should see the level
  if (!m_impl->createMesh()) {
    LOG_W("[Navigation] Not building: the navigation mesh could not be allocated");
    return false;
  }
  m_impl->warnedHeight = false;
  size_t withGeometry = 0;
  for (int tz = 0; tz < m_impl->tilesZ; ++tz)
    for (int tx = 0; tx < m_impl->tilesX; ++tx)
      if (m_impl->buildTile(tx, tz) != Impl::TileResult::Empty)
        ++withGeometry;
  if (m_impl->polygonCount() == 0) {
    LOG_W("[Navigation] Not building: nothing to walk on in the bounds (%s)",
          withGeometry == 0 ? "no static or kinematic bodies" : "every surface is too steep, narrow or low");
    m_impl->release();
    return false;
  }
  m_impl->markIslands();

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

bool NavigationSystem::rebuild(const glm::vec3 &min, const glm::vec3 &max) {
  if (!m_built || !std::isfinite(min.x + min.y + min.z + max.x + max.y + max.z))
    return false;
  Impl &impl = *m_impl;
  // A tile's border sees what stands beside it, so a change near an edge is the neighbor's too
  const float reach = static_cast<float>(impl.borderSize) * impl.voxel;
  const glm::vec3 low = glm::min(min, max) - glm::vec3(reach, 0.0f, reach);
  const glm::vec3 high = glm::max(min, max) + glm::vec3(reach, 0.0f, reach);
  const auto tileRange = [&](float lowValue, float highValue, float originValue, int count, int &first, int &last) {
    const double a = std::floor(static_cast<double>(lowValue - originValue) / impl.tileWorld);
    const double b = std::floor(static_cast<double>(highValue - originValue) / impl.tileWorld);
    first = static_cast<int>(std::max(a, 0.0));
    last = static_cast<int>(std::min(b, static_cast<double>(count - 1)));
    return a <= count - 1 && b >= 0 && first <= last;
  };
  int firstX, lastX, firstZ, lastZ;
  if (!tileRange(low.x, high.x, impl.origin.x, impl.tilesX, firstX, lastX) ||
      !tileRange(low.z, high.z, impl.origin.z, impl.tilesZ, firstZ, lastZ))
    return false;

  m_physics.syncBodies();
  for (int tz = firstZ; tz <= lastZ; ++tz)
    for (int tx = firstX; tx <= lastX; ++tx)
      impl.buildTile(tx, tz);
  impl.markIslands();
  m_lastPath.clear();
  return true;
}

void NavigationSystem::update() {
  if (m_dirty && m_built)
    build(m_min, m_max, m_settings);
}

void NavigationSystem::clear() {
  m_built = false;
  m_dirty = false;
  m_lastPath.clear();
  m_impl->release();
}

bool NavigationSystem::isWalkable(const glm::vec3 &point) const {
  if (!m_built)
    return false;
  Impl::Located found;
  return m_impl->locate(point, 0.01f, true, found);
}

std::optional<glm::vec3> NavigationSystem::getNearestWalkable(const glm::vec3 &point, float maxDistance) const {
  if (!m_built)
    return std::nullopt;
  Impl::Located found;
  if (!m_impl->locate(point, std::max(maxDistance, 0.01f), false, found))
    return std::nullopt;
  return found.point;
}

std::vector<glm::vec3> NavigationSystem::findPath(const glm::vec3 &from, const glm::vec3 &to) {
  m_lastPath.clear();
  if (!m_built)
    return m_lastPath;
  Impl &impl = *m_impl;
  Impl::Located start, goal;
  if (!impl.locate(from, kSnapDistance, false, start) || !impl.locate(to, kSnapDistance, false, goal))
    return m_lastPath;

  dtPolyRef corridor[kMaxPathPolys];
  int corridorSize = 0;
  const dtStatus status = impl.query->findPath(start.ref, goal.ref, glm::value_ptr(start.point),
                                               glm::value_ptr(goal.point), &impl.filter, corridor, &corridorSize,
                                               kMaxPathPolys);
  if (dtStatusFailed(status) || corridorSize == 0 || corridor[corridorSize - 1] != goal.ref)
    return m_lastPath; // unreachable: never a part of the way

  std::vector<float> points(static_cast<size_t>(kMaxStraightPoints) * 3);
  std::vector<unsigned char> flags(kMaxStraightPoints);
  std::vector<dtPolyRef> refs(kMaxStraightPoints);
  int count = 0;
  impl.query->findStraightPath(glm::value_ptr(start.point), glm::value_ptr(goal.point), corridor, corridorSize,
                               points.data(), flags.data(), refs.data(), &count, kMaxStraightPoints,
                               DT_STRAIGHTPATH_ALL_CROSSINGS);
  if (count == 0)
    return m_lastPath;

  // Between two points the path runs inside one polygon (every crossing is a point): its height there is the detail
  // mesh's, which a straight line from end to end misses over a step or a bump. Sampled a voxel apart.
  std::vector<glm::vec3> path;
  path.reserve(static_cast<size_t>(count));
  const auto pointAt = [&](int i) {
    return glm::vec3(points[static_cast<size_t>(i) * 3], points[static_cast<size_t>(i) * 3 + 1],
                     points[static_cast<size_t>(i) * 3 + 2]);
  };
  path.push_back(pointAt(0));
  for (int i = 1; i < count; ++i) {
    const glm::vec3 from = path.back(), to = pointAt(i);
    const dtPolyRef inside = refs[static_cast<size_t>(i - 1)];
    const int samples = static_cast<int>(std::floor(glm::distance(glm::vec2(from.x, from.z), glm::vec2(to.x, to.z)) / impl.voxel));
    for (int s = 1; s < samples && inside; ++s) {
      glm::vec3 sample = glm::mix(from, to, static_cast<float>(s) / static_cast<float>(samples));
      float closest[3];
      bool over = false;
      if (dtStatusSucceed(impl.query->closestPointOnPoly(inside, glm::value_ptr(sample), closest, &over)) && over)
        sample.y = closest[1];
      path.push_back(sample);
    }
    path.push_back(to);
  }

  // Douglas-Peucker in 3D: what lies on the line between the points kept (flat or on a plane slope) is not needed
  std::vector<uint8_t> keep(path.size(), 0);
  keep.front() = keep.back() = 1;
  std::vector<std::pair<size_t, size_t>> ranges{{0, path.size() - 1}};
  while (!ranges.empty()) {
    const auto [first, last] = ranges.back();
    ranges.pop_back();
    float worst = kStraightTolerance;
    size_t at = 0;
    for (size_t i = first + 1; i < last; ++i) {
      const float d = distanceToSegment(path[i], path[first], path[last]);
      if (d > worst) {
        worst = d;
        at = i;
      }
    }
    if (at == 0)
      continue;
    keep[at] = 1;
    ranges.push_back({first, at});
    ranges.push_back({at, last});
  }
  for (size_t i = 0; i < path.size(); ++i)
    if (keep[i])
      m_lastPath.push_back(path[i]);
  return m_lastPath;
}

void NavigationSystem::forEachPolygon(const glm::vec3 &center, float radius,
                                      const std::function<void(const glm::vec3 *corners, int count)> &visit) const {
  if (!m_built)
    return;
  const dtNavMesh &mesh = *m_impl->mesh;
  glm::vec3 corners[DT_VERTS_PER_POLYGON];
  for (int i = 0; i < mesh.getMaxTiles(); ++i) {
    const dtMeshTile *tile = mesh.getTile(i);
    if (!tile || !tile->header)
      continue;
    const dtMeshHeader &header = *tile->header;
    // Whole tiles away are skipped by their box
    const float nearX = std::max({header.bmin[0] - center.x, 0.0f, center.x - header.bmax[0]});
    const float nearZ = std::max({header.bmin[2] - center.z, 0.0f, center.z - header.bmax[2]});
    if (nearX * nearX + nearZ * nearZ > radius * radius)
      continue;
    for (int p = 0; p < header.polyCount; ++p) {
      const dtPoly &poly = tile->polys[p];
      if (!(poly.flags & kFlagWalk))
        continue;
      float lowX = 1e30f, highX = -1e30f, lowZ = 1e30f, highZ = -1e30f;
      for (int v = 0; v < poly.vertCount; ++v) {
        const float *c = &tile->verts[poly.verts[v] * 3];
        corners[v] = glm::vec3(c[0], c[1], c[2]);
        lowX = std::min(lowX, c[0]);
        highX = std::max(highX, c[0]);
        lowZ = std::min(lowZ, c[2]);
        highZ = std::max(highZ, c[2]);
      }
      const float dx = std::max({lowX - center.x, 0.0f, center.x - highX});
      const float dz = std::max({lowZ - center.z, 0.0f, center.z - highZ});
      if (dx * dx + dz * dz <= radius * radius)
        visit(corners, poly.vertCount);
    }
  }
}
