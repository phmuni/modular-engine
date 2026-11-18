#pragma once
// Renderer class for managing OpenGL context, drawing meshes, and handling shadow/particle passes.

#include "foundation/core/config.h"
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

class Renderer {
private:
  SDL_Window *m_window = nullptr;

  // Shadow mapping
  GLuint m_depthMapFBO = 0;
  GLuint m_depthMap = 0;
  const int m_shadowWidth = EngineConfig::SHADOW_MAP_WIDTH;
  const int m_shadowHeight = EngineConfig::SHADOW_MAP_HEIGHT;

  int m_screenWidth = 1280;
  int m_screenHeight = 720;

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
  void endFrame() const;

  // Mesh drawing
  void drawMesh(const Mesh &mesh) const;
  void drawSubmesh(const Mesh &mesh, const Submesh &submesh) const;

  // Instanced drawing: upload the instances, then draw them in one call
  void uploadInstances(const InstanceData *data, int count);
  void drawMeshInstanced(const Mesh &mesh, int count) const;
  void drawSubmeshInstanced(const Mesh &mesh, const Submesh &submesh, int count) const;

  // Shadow pass
  void beginShadowPass();
  void endShadowPass();

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

  GLuint getDepthMap() const;
  GLuint getDepthMapFBO() const;

  // Saves the current back buffer as a BMP; call before endFrame()
  bool saveScreenshot(const std::string &path) const;

  void setWireframe(bool enabled);
  void setViewportSize(int width, int height);
  void shutdown();
};
