#pragma once
#include <SDL3/SDL.h>
#include <glad/glad.h>
#include "foundation/core/config.h"

class Mesh;
struct Submesh;

class Renderer {
private:
  SDL_Window *m_window = nullptr;

  GLuint m_depthMapFBO = 0;
  GLuint m_depthMap = 0;
  const int m_shadowWidth = EngineConfig::SHADOW_MAP_WIDTH;
  const int m_shadowHeight = EngineConfig::SHADOW_MAP_HEIGHT;

  int m_screenWidth = 1280;
  int m_screenHeight = 720;

  void initShadowMapping();

public:
  void init(SDL_Window *window);

  void beginFrame() const;
  void endFrame() const;

  void drawMesh(const Mesh &mesh) const;
  void drawSubmesh(const Mesh &mesh, const Submesh &submesh) const;

  void beginShadowPass();
  void endShadowPass();

  GLuint getDepthMap() const;
  GLuint getDepthMapFBO() const;

  void setViewportSize(int width, int height);
  void shutdown();
};
