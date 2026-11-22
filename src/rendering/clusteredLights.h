#pragma once
// Clustered forward lighting. Point and spot lights are not looped over by every pixel: the view frustum is cut into
// CLUSTER_GRID_X x CLUSTER_GRID_Y screen tiles and CLUSTER_GRID_Z depth slices (exponential, so the slices stay about as
// deep as they are wide), the CPU finds the clusters each light's sphere of influence touches, and the lit shader loops
// over the lights of its pixel's cluster only.
//
// Everything lives in std140 uniform buffers (no texture units; GL 4.1 has no shader storage buffers):
//   DirectionalLights (binding 1)  up to MAX_DIRECTIONAL_LIGHTS lights, 5 vec4 each, always applied
//   PointLights       (binding 2)  up to lightingLimits().maxLights lights, 5 vec4 each (see GpuLight)
//   ClusterGrid       (binding 3)  one uint per cluster: (offset into the index list << 9) | light count
//   ClusterIndices0.. (bindings 4..) the lights' indices as bytes, 4 to a uint, each cluster's lights one after another;
//                                  the list continues from one block into the next (up to 8 blocks, sized from the driver)
// Light ranges are automatic (lightRange) and the attenuation is windowed to reach exactly 0 at the range.

#include "foundation/core/config.h"
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <string>
#include <vector>

class Shader;

// What the driver's uniform blocks allow for the buffers above. 64 KB blocks (the usual size) give the full 256 lights
// and 262144 index entries (4 blocks); the GL minimum of 16 KB gives 204 lights and, with the fragment blocks the
// driver must have (12), 98304 entries (6 blocks).
struct LightingLimits {
  int maxUniformBlockSize = 0; // GL_MAX_UNIFORM_BLOCK_SIZE
  int maxLights = 0;           // point and spot lights at once: min(MAX_CLUSTERED_LIGHTS, what the block holds)
  int indexBlocks = 0;         // the light index list is spread over this many uniform blocks...
  int indexBlockBytes = 0;     // ...of this size (one entry, a byte, per light per cluster)
  int indexBytes = 0;          // so it holds this many entries: 262144 with 64 KB blocks, as far as the driver allows
  int gridBytes = 0;
  int lightBytes = 0;
};
// Queried from the GL context the first time (it must be current) and logged
const LightingLimits &lightingLimits();
// "#define"s for every shader's source: the grid, MAX_CLUSTER_LIGHTS and CLUSTER_INDEX_BLOCKS and CLUSTER_INDEX_BLOCK_UINTS
std::string lightingShaderDefines();
// Points a linked program's light uniform blocks at the buffers' binding points
void bindLightingBlocks(GLuint program);

// The distance a light reaches: where its brightness (the larger of its color's channels times intensity + ambient)
// times its attenuation 1 / (constant + linear d + quadratic d^2) falls to EngineConfig::LIGHT_RANGE_THRESHOLD, at most
// LIGHT_MAX_RANGE. 0 for a light that is below the threshold even at its center.
float lightRange(float constant, float linear, float quadratic, float brightness);
// The smooth window that takes the attenuation to exactly 0 at the range: (1 - (d/r)^4)^2, 0 from r on (the lit
// shader does the same). Close to the light it is 1 to within 0.02% (d < r / 10).
float lightWindow(float distance, float range);

// One light as the shader reads it, 5 vec4:
//   0: position.xyz (world), range
//   1: color.xyz (as authored), intensity
//   2: direction.xyz, ambient
//   3: constant, linear, quadratic, type (0 directional, 1 point, 2 spot)
//   4: cutOff, outerCutOff, shadowFar, shadow slot (-1 for none)
struct GpuLight {
  glm::vec4 data[5];
};

class ClusteredLights {
public:
  struct Stats {
    int lights = 0;       // point and spot lights uploaded
    int directional = 0;
    int pairs = 0;        // (light, cluster) pairs found
    int indexEntries = 0; // of those, what fit the index list
    int dropped = 0;      // pairs that did not fit
    double buildMs = 0.0; // the CPU time of the last build, uploads included
  };

  // Finds the clusters of `lights` (most important first) for a view of the given render size, and uploads everything.
  // When the index list is too small for all the (light, cluster) pairs, each cluster keeps its first lights, the same
  // number for every cluster (the largest that fits).
  void build(const glm::mat4 &view, const glm::mat4 &projection, int width, int height,
             const std::vector<GpuLight> &directional, const std::vector<GpuLight> &lights);
  // The per-view uniforms of the lit shader (it must be in use)
  void setUniforms(const Shader &shader) const;
  const Stats &stats() const { return m_stats; }
  void shutdown();

private:
  void ensureBuffers();
  void updateClusterBounds(const glm::mat4 &projection);
  int sliceOf(float depth) const;

  GLuint m_directionalBuffer = 0, m_lightBuffer = 0, m_gridBuffer = 0;
  std::vector<GLuint> m_indexBuffers;

  // The view-space box of every cluster, made again when the projection changes
  struct Bounds {
    glm::vec3 min, max;
  };
  std::vector<Bounds> m_bounds;
  glm::vec4 m_boundsKey{0.0f}; // P[0][0], P[1][1], near, far the bounds were made for
  float m_near = 0.1f, m_far = 100.0f, m_p00 = 1.0f, m_p11 = 1.0f;
  float m_zScale = 1.0f, m_zBias = 0.0f;
  glm::vec2 m_tileScale{0.0f};
  int m_directionalCount = 0;

  // Scratch, kept between frames
  std::vector<uint32_t> m_pairs, m_counts, m_offsets, m_fill, m_grid;
  std::vector<uint8_t> m_indices;
  Stats m_stats;
};
