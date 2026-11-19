#pragma once
// Renderer class for managing OpenGL context, drawing meshes, and handling shadow/particle passes.

#include "foundation/core/config.h"
#include "rendering/postProcess.h"
#include <SDL3/SDL.h>
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <string>

class Mesh;
struct Submesh;

// Per-instance data for instanced mesh drawing: vertex attributes 3-6 (model) and 7-9 (normal matrix)
struct InstanceData {
  glm::mat4 model;
  glm::mat3 normal;
};

// Interleaved vertex data for particle GPU upload
struct ParticleVertex {
  glm::vec3 position;
  glm::vec4 color;
  float size;
};

// Counters for the current frame, reset by beginFrame()
struct RenderStats {
  int drawCalls = 0;
  int meshesDrawn = 0; // mesh or submesh instances, shadow pass included
  int particles = 0;
};

class Renderer {
private:
  mutable RenderStats m_stats;
  mutable bool m_faceCulling = true;

  SDL_Window *m_window = nullptr;

  // Sun shadow cascades: one layer of a depth texture array per cascade
  GLuint m_depthMapFBO = 0;
  GLuint m_depthMap = 0;

  // Point light shadows: one depth cube map per slot, created the first time a light uses the slot. Unused slots
  // read m_emptyCubeMap, since a cube sampler must never share a texture unit with a 2D texture.
  GLuint m_pointShadowFBO = 0;
  GLuint m_pointShadowMaps[EngineConfig::MAX_POINT_SHADOWS] = {};
  mutable GLuint m_emptyCubeMap = 0;
  static GLuint createDepthCubeMap(int size);

  int m_screenWidth = 1280;
  int m_screenHeight = 720;

  // The scene renders into an HDR target; finishScene brings it to the screen
  PostProcess m_postProcess;

  GLuint m_instanceVBO = 0;
  void bindInstanceAttributes(const Mesh &mesh) const;

  // Particle GPU resources
  GLuint m_particleVAO = 0;
  GLuint m_particleVBO = 0;
  GLsizeiptr m_particleCapacity = 0; // bytes allocated in m_particleVBO

  void initShadowMapping();
  void initParticleBuffers();

public:
  void init(SDL_Window *window);

  void beginFrame() const;
  // The scene's passes draw into the HDR target between these; finishScene adds bloom and tone maps onto the screen
  void beginScene();
  // Lit passes done: tone maps into the display target, where particles draw next
  void resolveScene(Shader &tonemap, const PostProcess::Settings &settings);
  void finishScene(Shader &bloomDownsample, Shader &bloomUpsample, Shader &composite,
                   const PostProcess::Settings &settings);
  const RenderStats &getStats() const { return m_stats; }
  void endFrame() const;

  // Mesh drawing
  void drawMesh(const Mesh &mesh) const;
  void drawSubmesh(const Mesh &mesh, const Submesh &submesh) const;
  void drawIndexRange(const Mesh &mesh, uint32_t indexStart, uint32_t indexCount) const;

  // Instanced drawing: upload the instances, then draw them in one call
  void uploadInstances(const InstanceData *data, int count);
  void drawMeshInstanced(const Mesh &mesh, int count) const;
  void drawSubmeshInstanced(const Mesh &mesh, const Submesh &submesh, int count) const;
  void drawIndexRangeInstanced(const Mesh &mesh, uint32_t indexStart, uint32_t indexCount, int count) const;

  // Shadow pass: renders into one sun cascade (a layer of getDepthMap)
  void beginShadowPass(int cascade);
  void endShadowPass();
  // Renders into one face (0-5: +X, -X, +Y, -Y, +Z, -Z) of a point shadow slot; end with endShadowPass()
  void beginPointShadowFace(int slot, int face);
  // The slot's cube map, or an empty one (everything lit) if the slot was never used
  GLuint getPointShadowMap(int slot) const;

  // Particle / transparent pass
  void beginTransparentPass();
  void endTransparentPass();
  void beginSoftTransparentPass();
  void endSoftTransparentPass();
  void beginParticlePass(bool additiveBlending);
  void uploadParticles(const ParticleVertex *data, int count);
  // `images` > 1 draws each particle that many times (gl_InstanceID picks the image, e.g. for lensing)
  void drawParticles(int count, int images = 1);
  void endParticlePass();

  // The sun's cascades, a GL_TEXTURE_2D_ARRAY of EngineConfig::SHADOW_CASCADES layers
  GLuint getDepthMap() const;

  // Saves the current back buffer as a BMP; call before endFrame()
  bool saveScreenshot(const std::string &path) const;

  void setWireframe(bool enabled);
  // Back-face culling, on by default; double-sided materials turn it off while they draw
  void setFaceCulling(bool enabled) const;
  void setViewportSize(int width, int height);
  void shutdown();
};
