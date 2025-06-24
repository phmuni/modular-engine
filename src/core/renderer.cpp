#include "core/renderer.h"

void Renderer::initialize(SDL_Window *window) {
  m_window = window;
  initShadowMapping(); // ✅ mover pra cá pode ser melhor
}

void Renderer::beginFrame() const { glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT); }

void Renderer::endFrame() const { SDL_GL_SwapWindow(m_window); }

void Renderer::drawMesh(const Mesh &mesh) const {
  glBindVertexArray(mesh.getVAO());
  glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(mesh.getIndexCount()), GL_UNSIGNED_INT, 0);
  glBindVertexArray(0);
}

// ==================== Shadow Mapping ====================

void Renderer::beginShadowPass() {
  glViewport(0, 0, shadowWidth, shadowHeight);
  glBindFramebuffer(GL_FRAMEBUFFER, depthMapFBO);
  glClear(GL_DEPTH_BUFFER_BIT);
}

void Renderer::endShadowPass() {
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glViewport(0, 0, screenWidth, screenHeight);
}

void Renderer::initShadowMapping() {
  glGenFramebuffers(1, &depthMapFBO);

  glGenTextures(1, &depthMap);
  glBindTexture(GL_TEXTURE_2D, depthMap);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, shadowWidth, shadowHeight, 0, GL_DEPTH_COMPONENT, GL_FLOAT,
               nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);

  float borderColor[] = {1.0f, 1.0f, 1.0f, 1.0f};
  glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, borderColor);

  glBindFramebuffer(GL_FRAMEBUFFER, depthMapFBO);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depthMap, 0);
  glDrawBuffer(GL_NONE);
  glReadBuffer(GL_NONE);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

// ==================== Getters ====================

GLuint Renderer::getDepthMap() const { return depthMap; }

GLuint Renderer::getDepthMapFBO() const { return depthMapFBO; }
