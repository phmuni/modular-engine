#pragma once
// Low-level OpenGL renderer: draw calls, shadow maps, and particle buffers.

#include "foundation/core/config.h"
#include <SDL3/SDL.h>
#include <glad/glad.h>
#include <glm/glm.hpp>

class Mesh;
struct Submesh;

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

  // Particle GPU resources
  GLuint m_particleVAO = 0;
  GLuint m_particleVBO = 0;

  void initShadowMapping();
  void initParticleBuffers();

public:
  void init(SDL_Window *window);

  void beginFrame() const;
  void endFrame() const;

  // Mesh drawing
  void drawMesh(const Mesh &mesh) const;
  void drawSubmesh(const Mesh &mesh, const Submesh &submesh) const;

  // Shadow pass
  void beginShadowPass();
  void endShadowPass();

  // Particle / transparent pass
  void beginParticlePass(bool additiveBlending);
  void uploadParticles(const ParticleVertex *data, int count);
  void drawParticles(int count);
  void endParticlePass();

  GLuint getDepthMap() const;
  GLuint getDepthMapFBO() const;

  void setWireframe(bool enabled);
  void setViewportSize(int width, int height);
  void shutdown();
};
