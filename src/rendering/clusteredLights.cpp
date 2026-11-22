// Clustered forward lighting: light ranges, the clusters each light touches, and the uniform buffers the lit shader reads.

#include "rendering/clusteredLights.h"
#include "foundation/core/logger.h"
#include "rendering/resources/shader.h"
#include <algorithm>
#include <chrono>
#include <cmath>

namespace {
constexpr int kDirectionalBinding = 1, kLightBinding = 2, kGridBinding = 3, kIndexBinding = 4; // the index blocks take 4 and up
constexpr int kMaxIndexBlocks = 8;                  // the most the shader declares (fragmentShader.frag)
constexpr int kIndexEntriesTarget = 262144;         // what the index list aims to hold, in blocks of up to 64 KB
constexpr int kVec4PerLight = 5;
constexpr int kClusterCount = EngineConfig::CLUSTER_GRID_X * EngineConfig::CLUSTER_GRID_Y * EngineConfig::CLUSTER_GRID_Z;
// A cluster entry holds the offset into the index list above this many bits of count (up to 256 lights: 9 bits)
constexpr uint32_t kCountBits = 9;
static_assert(EngineConfig::MAX_CLUSTERED_LIGHTS <= 256, "a light's index is one byte");
static_assert(kClusterCount % 4 == 0, "the grid is an array of uvec4");
static_assert(sizeof(GpuLight) == kVec4PerLight * 16, "GpuLight is 5 vec4");

int roundUp16(int bytes) { return (bytes + 15) & ~15; }
} // namespace

const LightingLimits &lightingLimits() {
  static const LightingLimits limits = [] {
    LightingLimits result;
    GLint blockSize = 0;
    glGetIntegerv(GL_MAX_UNIFORM_BLOCK_SIZE, &blockSize);
    result.maxUniformBlockSize = blockSize;
    const int lightSize = kVec4PerLight * 16;
    result.maxLights = std::max(1, std::min(EngineConfig::MAX_CLUSTERED_LIGHTS, blockSize / lightSize));
    result.lightBytes = result.maxLights * lightSize;
    result.gridBytes = kClusterCount * 4;
    // The index list takes a block of up to 64 KB (one entry per byte, 4 to a uint)
    result.indexBlockBytes = std::max(4096, std::min(blockSize, 65536)) & ~15;
    GLint fragmentBlocks = 0, combinedComponents = 0;
    glGetIntegerv(GL_MAX_FRAGMENT_UNIFORM_BLOCKS, &fragmentBlocks);
    // The index list is spread over several blocks (one entry per byte): as many as hold the target, but the program
    // also needs its 3 other blocks and a few spare
    const int wanted = (kIndexEntriesTarget + result.indexBlockBytes - 1) / result.indexBlockBytes;
    result.indexBlocks = std::clamp(std::min(wanted, static_cast<int>(fragmentBlocks) - 6), 1, kMaxIndexBlocks);
    result.indexBytes = result.indexBlocks * result.indexBlockBytes;
    glGetIntegerv(GL_MAX_COMBINED_FRAGMENT_UNIFORM_COMPONENTS, &combinedComponents);
    LOG_I("[Lights] Clustered lighting: GL_MAX_UNIFORM_BLOCK_SIZE %d bytes -> up to %d point/spot lights (%d bytes), "
          "grid %dx%dx%d (%d bytes), %d index entries in %d blocks of %d bytes; %d fragment uniform blocks, %d combined fragment components",
          blockSize, result.maxLights, result.lightBytes, EngineConfig::CLUSTER_GRID_X, EngineConfig::CLUSTER_GRID_Y,
          EngineConfig::CLUSTER_GRID_Z, result.gridBytes, result.indexBytes, result.indexBlocks, result.indexBlockBytes, fragmentBlocks, combinedComponents);
    if (blockSize < result.gridBytes)
      LOG_W("[Lights] The driver's uniform blocks (%d bytes) are smaller than the cluster grid (%d bytes)", blockSize,
            result.gridBytes);
    return result;
  }();
  return limits;
}

std::string lightingShaderDefines() {
  const LightingLimits &limits = lightingLimits();
  return "#define MAX_CLUSTER_LIGHTS " + std::to_string(limits.maxLights) + "\n#define CLUSTER_INDEX_BLOCKS " +
         std::to_string(limits.indexBlocks) + "\n#define CLUSTER_INDEX_BLOCK_UINTS " +
         std::to_string(limits.indexBlockBytes / 4) + "\n#define MAX_DIRECTIONAL_LIGHTS " +
         std::to_string(EngineConfig::MAX_DIRECTIONAL_LIGHTS) + "\n#define CLUSTER_GRID_X " +
         std::to_string(EngineConfig::CLUSTER_GRID_X) + "\n#define CLUSTER_GRID_Y " +
         std::to_string(EngineConfig::CLUSTER_GRID_Y) + "\n#define CLUSTER_GRID_Z " +
         std::to_string(EngineConfig::CLUSTER_GRID_Z) + "\n";
}

void bindLightingBlocks(GLuint program) {
  const struct {
    const char *name;
    int binding;
  } blocks[] = {{"DirectionalLights", kDirectionalBinding},
                {"PointLights", kLightBinding},
                {"ClusterGrid", kGridBinding}};
  for (const auto &block : blocks) {
    const GLuint index = glGetUniformBlockIndex(program, block.name);
    if (index != GL_INVALID_INDEX)
      glUniformBlockBinding(program, index, block.binding);
  }
  for (int i = 0; i < lightingLimits().indexBlocks; ++i) {
    const GLuint index = glGetUniformBlockIndex(program, ("ClusterIndices" + std::to_string(i)).c_str());
    if (index != GL_INVALID_INDEX)
      glUniformBlockBinding(program, index, kIndexBinding + i);
  }
}

float lightRange(float constant, float linear, float quadratic, float brightness) {
  // brightness / (c + l d + q d^2) = threshold  ->  q d^2 + l d - k = 0 with k = brightness / threshold - c
  const float k = std::max(brightness, 0.0f) / EngineConfig::LIGHT_RANGE_THRESHOLD - constant;
  if (k <= 0.0f)
    return 0.0f;
  linear = std::max(linear, 0.0f);
  quadratic = std::max(quadratic, 0.0f);
  const float denominator = linear + std::sqrt(linear * linear + 4.0f * quadratic * k);
  if (denominator < 1e-9f)
    return EngineConfig::LIGHT_MAX_RANGE;
  return std::min(2.0f * k / denominator, EngineConfig::LIGHT_MAX_RANGE); // the root, in its stable form
}

float lightWindow(float distance, float range) {
  if (range <= 0.0f || distance >= range)
    return 0.0f;
  const float ratio = distance / range;
  const float falloff = 1.0f - ratio * ratio * ratio * ratio;
  return falloff * falloff;
}

void ClusteredLights::ensureBuffers() {
  if (m_lightBuffer)
    return;
  const LightingLimits &limits = lightingLimits();
  auto make = [](GLuint &buffer, int binding, int bytes) {
    glGenBuffers(1, &buffer);
    glBindBuffer(GL_UNIFORM_BUFFER, buffer);
    glBufferData(GL_UNIFORM_BUFFER, roundUp16(bytes), nullptr, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER, binding, buffer);
  };
  make(m_directionalBuffer, kDirectionalBinding, EngineConfig::MAX_DIRECTIONAL_LIGHTS * kVec4PerLight * 16);
  make(m_lightBuffer, kLightBinding, limits.lightBytes);
  make(m_gridBuffer, kGridBinding, limits.gridBytes);
  m_indexBuffers.assign(limits.indexBlocks, 0);
  for (int i = 0; i < limits.indexBlocks; ++i)
    make(m_indexBuffers[i], kIndexBinding + i, limits.indexBlockBytes);
  glBindBuffer(GL_UNIFORM_BUFFER, 0);
  m_grid.assign(kClusterCount, 0);
  m_indices.assign(limits.indexBytes, 0);
}

void ClusteredLights::shutdown() {
  for (GLuint *buffer : {&m_directionalBuffer, &m_lightBuffer, &m_gridBuffer})
    if (*buffer) {
      glDeleteBuffers(1, buffer);
      *buffer = 0;
    }
  for (GLuint &buffer : m_indexBuffers)
    if (buffer)
      glDeleteBuffers(1, &buffer);
  m_indexBuffers.clear();
}

int ClusteredLights::sliceOf(float depth) const {
  const int slice = static_cast<int>(std::floor(std::log2(std::max(depth, 1e-6f)) * m_zScale + m_zBias));
  return std::clamp(slice, 0, EngineConfig::CLUSTER_GRID_Z - 1);
}

void ClusteredLights::updateClusterBounds(const glm::mat4 &projection) {
  const float p22 = projection[2][2], p32 = projection[3][2];
  const glm::vec4 key(projection[0][0], projection[1][1], p32 / (p22 - 1.0f), p32 / (p22 + 1.0f));
  m_p00 = key.x;
  m_p11 = key.y;
  m_near = std::max(key.z, 1e-4f);
  m_far = std::max(key.w, m_near * 1.001f);
  m_zScale = static_cast<float>(EngineConfig::CLUSTER_GRID_Z) / std::log2(m_far / m_near);
  m_zBias = -std::log2(m_near) * m_zScale;
  if (key == m_boundsKey && !m_bounds.empty())
    return;
  m_boundsKey = key;
  constexpr int gx = EngineConfig::CLUSTER_GRID_X, gy = EngineConfig::CLUSTER_GRID_Y, gz = EngineConfig::CLUSTER_GRID_Z;
  m_bounds.resize(kClusterCount);
  for (int z = 0; z < gz; ++z) {
    const float d0 = m_near * std::pow(m_far / m_near, static_cast<float>(z) / gz);
    const float d1 = m_near * std::pow(m_far / m_near, static_cast<float>(z + 1) / gz);
    for (int y = 0; y < gy; ++y) {
      const float y0 = -1.0f + 2.0f * y / gy, y1 = -1.0f + 2.0f * (y + 1) / gy;
      for (int x = 0; x < gx; ++x) {
        const float x0 = -1.0f + 2.0f * x / gx, x1 = -1.0f + 2.0f * (x + 1) / gx;
        // A point at depth d and screen position n is at n * d / P in view space: the extremes are at the corners
        const float xs[4] = {x0 * d0, x0 * d1, x1 * d0, x1 * d1};
        const float ys[4] = {y0 * d0, y0 * d1, y1 * d0, y1 * d1};
        Bounds &box = m_bounds[(z * gy + y) * gx + x];
        box.min = glm::vec3(*std::min_element(xs, xs + 4) / m_p00, *std::min_element(ys, ys + 4) / m_p11, -d1);
        box.max = glm::vec3(*std::max_element(xs, xs + 4) / m_p00, *std::max_element(ys, ys + 4) / m_p11, -d0);
      }
    }
  }
}

namespace {
// The tiles along one screen axis a sphere can touch: its center's offset and depth in view space, radius, the
// projection's scale for the axis and the tile count. Seen from the camera the sphere is a cone between two tangent
// directions; false when it is entirely off one side.
bool tileRange(float offset, float depth, float radius, float scale, int tiles, int &first, int &last) {
  const float distance = std::sqrt(offset * offset + depth * depth);
  if (depth <= radius || distance <= radius) { // the camera is inside the sphere or it reaches behind: all tiles
    first = 0;
    last = tiles - 1;
    return true;
  }
  const float center = std::atan2(offset, depth);
  const float half = std::asin(radius / distance);
  const float low = std::tan(center - half) * scale;
  const float high = std::tan(center + half) * scale;
  if (high < -1.0f || low > 1.0f)
    return false;
  first = std::clamp(static_cast<int>(std::floor((low * 0.5f + 0.5f) * tiles)), 0, tiles - 1);
  last = std::clamp(static_cast<int>(std::floor((high * 0.5f + 0.5f) * tiles)), 0, tiles - 1);
  return true;
}
} // namespace

void ClusteredLights::build(const glm::mat4 &view, const glm::mat4 &projection, int width, int height,
                            const std::vector<GpuLight> &directional, const std::vector<GpuLight> &lights) {
  const auto start = std::chrono::steady_clock::now();
  ensureBuffers();
  updateClusterBounds(projection);
  constexpr int gx = EngineConfig::CLUSTER_GRID_X, gy = EngineConfig::CLUSTER_GRID_Y;
  m_tileScale = glm::vec2(static_cast<float>(gx) / std::max(width, 1), static_cast<float>(gy) / std::max(height, 1));

  const LightingLimits &limits = lightingLimits();
  const size_t lightCount = std::min(lights.size(), static_cast<size_t>(limits.maxLights));
  m_directionalCount =
      static_cast<int>(std::min(directional.size(), static_cast<size_t>(EngineConfig::MAX_DIRECTIONAL_LIGHTS)));

  m_pairs.clear();
  m_counts.assign(kClusterCount, 0);
  for (size_t i = 0; i < lightCount; ++i) {
    const glm::vec4 &sphere = lights[i].data[0];
    const float radius = sphere.w;
    const glm::vec3 center = glm::vec3(view * glm::vec4(sphere.x, sphere.y, sphere.z, 1.0f));
    const float depth = -center.z;
    if (depth + radius < m_near || depth - radius > m_far)
      continue;
    int x0, x1, y0, y1;
    if (!tileRange(center.x, depth, radius, m_p00, gx, x0, x1) || !tileRange(center.y, depth, radius, m_p11, gy, y0, y1))
      continue;
    const int z0 = sliceOf(std::max(depth - radius, m_near));
    const int z1 = sliceOf(std::min(depth + radius, m_far));
    const float radiusSquared = radius * radius;
    for (int z = z0; z <= z1; ++z)
      for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) {
          const int cluster = (z * gy + y) * gx + x;
          const Bounds &box = m_bounds[cluster];
          const glm::vec3 nearest = glm::clamp(center, box.min, box.max) - center;
          if (glm::dot(nearest, nearest) > radiusSquared)
            continue;
          m_pairs.push_back((static_cast<uint32_t>(cluster) << 8) | static_cast<uint32_t>(i));
          ++m_counts[cluster];
        }
  }

  // Each cluster's list starts where the previous one ends. When they do not all fit the index list, every cluster is
  // limited to the same number of lights (the most important, as the lights come in that order): the largest that fits
  const uint32_t capacity = static_cast<uint32_t>(limits.indexBytes);
  uint32_t perCluster = 256;
  if (m_pairs.size() > capacity) {
    uint32_t low = 0, high = 256; // the largest limit whose total fits
    while (low < high) {
      const uint32_t middle = (low + high + 1) / 2;
      uint64_t total = 0;
      for (int c = 0; c < kClusterCount; ++c)
        total += std::min(m_counts[c], middle);
      if (total <= capacity)
        low = middle;
      else
        high = middle - 1;
    }
    perCluster = low;
  }
  m_offsets.resize(kClusterCount);
  uint32_t used = 0;
  for (int c = 0; c < kClusterCount; ++c) {
    const uint32_t count = std::min(m_counts[c], perCluster);
    m_offsets[c] = used;
    m_grid[c] = (used << kCountBits) | count;
    m_counts[c] = count; // from here on, the room the cluster has
    used += count;
  }
  m_fill.assign(kClusterCount, 0);
  for (const uint32_t pair : m_pairs) {
    const uint32_t cluster = pair >> 8;
    if (m_fill[cluster] < m_counts[cluster])
      m_indices[m_offsets[cluster] + m_fill[cluster]++] = static_cast<uint8_t>(pair & 255u);
  }
  m_stats.lights = static_cast<int>(lightCount);
  m_stats.directional = m_directionalCount;
  m_stats.pairs = static_cast<int>(m_pairs.size());
  m_stats.indexEntries = static_cast<int>(used);
  m_stats.dropped = m_stats.pairs - m_stats.indexEntries;
  static bool warned = false; // once per run, not every frame
  if (m_stats.dropped > 0 && !warned) {
    warned = true;
    LOG_W("[Lights] The cluster light list is full (%d entries): %d light-cluster pairs of the least important lights "
          "are left out",
          limits.indexBytes, m_stats.dropped);
  }

  // Upload: the lights and the part of the index list that is used
  auto upload = [](GLuint buffer, const void *data, size_t bytes) {
    if (bytes == 0)
      return;
    glBindBuffer(GL_UNIFORM_BUFFER, buffer);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, static_cast<GLsizeiptr>(bytes), data);
  };
  upload(m_directionalBuffer, directional.data(), static_cast<size_t>(m_directionalCount) * sizeof(GpuLight));
  upload(m_lightBuffer, lights.data(), lightCount * sizeof(GpuLight));
  upload(m_gridBuffer, m_grid.data(), m_grid.size() * sizeof(uint32_t));
  // The used part of the index list, block by block
  for (int i = 0; i < limits.indexBlocks; ++i) {
    const size_t first = static_cast<size_t>(i) * limits.indexBlockBytes;
    if (used > first)
      upload(m_indexBuffers[i], m_indices.data() + first,
             std::min<size_t>(limits.indexBlockBytes, (used - first + 15) & ~static_cast<size_t>(15)));
  }
  glBindBuffer(GL_UNIFORM_BUFFER, 0);
  glBindBufferBase(GL_UNIFORM_BUFFER, kDirectionalBinding, m_directionalBuffer);
  glBindBufferBase(GL_UNIFORM_BUFFER, kLightBinding, m_lightBuffer);
  glBindBufferBase(GL_UNIFORM_BUFFER, kGridBinding, m_gridBuffer);
  for (int i = 0; i < limits.indexBlocks; ++i)
    glBindBufferBase(GL_UNIFORM_BUFFER, kIndexBinding + i, m_indexBuffers[i]);
  m_stats.buildMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

void ClusteredLights::setUniforms(const Shader &shader) const {
  shader.setVec2("clusterTileScale", m_tileScale);
  shader.setVec2("clusterZ", glm::vec2(m_zScale, m_zBias));
  shader.setInt("directionalCount", m_directionalCount);
}
