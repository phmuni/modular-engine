
// Renderer implementation: frame management, shadow mapping, and particle rendering.

#include "rendering/renderer.h"
#include "rendering/resources/mesh.h"

void Renderer::init(SDL_Window *window) {
  m_window = window;
  initShadowMapping();
  initParticleBuffers();
}

void Renderer::beginFrame() const { glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT); }

void Renderer::endFrame() const { SDL_GL_SwapWindow(m_window); }

void Renderer::drawMesh(const Mesh &mesh) const {
  glBindVertexArray(mesh.getVAO());

  const auto &submeshes = mesh.getSubmeshes();
  for (const auto &submesh : submeshes) {
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(submesh.indexCount), GL_UNSIGNED_INT,
                   (void *)(submesh.indexStart * sizeof(uint32_t)));
  }

  glBindVertexArray(0);
}

void Renderer::drawSubmesh(const Mesh &mesh, const Submesh &submesh) const {
  glBindVertexArray(mesh.getVAO());
  glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(submesh.indexCount), GL_UNSIGNED_INT,
                 (void *)(submesh.indexStart * sizeof(uint32_t)));
  glBindVertexArray(0);
}

void Renderer::beginShadowPass() {
  glViewport(0, 0, m_shadowWidth, m_shadowHeight);
  glBindFramebuffer(GL_FRAMEBUFFER, m_depthMapFBO);
  glClear(GL_DEPTH_BUFFER_BIT);
}

void Renderer::endShadowPass() {
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glViewport(0, 0, m_screenWidth, m_screenHeight);
}

void Renderer::initShadowMapping() {
  glGenFramebuffers(1, &m_depthMapFBO);
  glGenTextures(1, &m_depthMap);

  glBindTexture(GL_TEXTURE_2D, m_depthMap);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, m_shadowWidth, m_shadowHeight, 0, GL_DEPTH_COMPONENT, GL_FLOAT,
               nullptr);

  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
  float borderColor[] = {1.0f, 1.0f, 1.0f, 1.0f};
  glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, borderColor);

  glBindFramebuffer(GL_FRAMEBUFFER, m_depthMapFBO);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, m_depthMap, 0);
  glDrawBuffer(GL_NONE);
  glReadBuffer(GL_NONE);

  // Verify FBO
  GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
  if (status != GL_FRAMEBUFFER_COMPLETE) {
    SDL_Log("Shadow map FBO incomplete! Status: 0x%x", status);
  }

  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

GLuint Renderer::getDepthMap() const { return m_depthMap; }

GLuint Renderer::getDepthMapFBO() const { return m_depthMapFBO; }

void Renderer::setWireframe(bool enabled) { glPolygonMode(GL_FRONT_AND_BACK, enabled ? GL_LINE : GL_FILL); }

void Renderer::setViewportSize(int width, int height) {
  m_screenWidth = width;
  m_screenHeight = height;
}

// Particle buffers

void Renderer::initParticleBuffers() {
  glGenVertexArrays(1, &m_particleVAO);
  glGenBuffers(1, &m_particleVBO);

  glBindVertexArray(m_particleVAO);
  glBindBuffer(GL_ARRAY_BUFFER, m_particleVBO);

  constexpr GLsizei stride = sizeof(ParticleVertex);

  // location 0: position (vec3)
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void *)offsetof(ParticleVertex, position));

  // location 1: color (vec4)
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, stride, (void *)offsetof(ParticleVertex, color));

  // location 2: size (float)
  glEnableVertexAttribArray(2);
  glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, stride, (void *)offsetof(ParticleVertex, size));

  glBindVertexArray(0);
}

void Renderer::beginParticlePass(bool additiveBlending) {
  glEnable(GL_PROGRAM_POINT_SIZE);
  glDepthMask(GL_FALSE);
  glEnable(GL_BLEND);

  if (additiveBlending) {
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    return;
  }
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void Renderer::uploadParticles(const ParticleVertex *data, int count) {
  glBindBuffer(GL_ARRAY_BUFFER, m_particleVBO);
  glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(count * sizeof(ParticleVertex)), data, GL_DYNAMIC_DRAW);
}

void Renderer::drawParticles(int count) {
  glBindVertexArray(m_particleVAO);
  glDrawArrays(GL_POINTS, 0, static_cast<GLsizei>(count));
  glBindVertexArray(0);
}

void Renderer::endParticlePass() {
  glDepthMask(GL_TRUE);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glDisable(GL_PROGRAM_POINT_SIZE);
}

void Renderer::shutdown() {
  if (m_depthMapFBO) {
    glDeleteFramebuffers(1, &m_depthMapFBO);
    m_depthMapFBO = 0;
  }
  if (m_depthMap) {
    glDeleteTextures(1, &m_depthMap);
    m_depthMap = 0;
  }
  if (m_particleVAO) {
    glDeleteVertexArrays(1, &m_particleVAO);
    m_particleVAO = 0;
  }
  if (m_particleVBO) {
    glDeleteBuffers(1, &m_particleVBO);
    m_particleVBO = 0;
  }
}
