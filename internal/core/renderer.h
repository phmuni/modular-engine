#pragma once

// External Includes
#include <SDL3/SDL.h>
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

// Internal Includes
#include "model/mesh.h"

class Renderer {
public:
  void initialize(SDL_Window *window);

  void beginFrame() const;
  void endFrame() const;

  void beginShadowPass();
  void endShadowPass();

  void drawMesh(const Mesh &mesh) const;

  GLuint getDepthMap() const;
  GLuint getDepthMapFBO() const;

private:
  // SDL window to swap buffers
  SDL_Window *m_window = nullptr;

  // Shadow map
  GLuint depthMap = 0;
  GLuint depthMapFBO = 0;
  const int shadowWidth = 1024;
  const int shadowHeight = 1024;

  const int screenWidth = 1280;
  const int screenHeight = 720;

  void initShadowMapping();
};
