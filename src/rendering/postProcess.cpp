// HDR target, bloom mip chain and the final composite pass.

#include "rendering/postProcess.h"

#include "foundation/core/logger.h"
#include "rendering/resources/shader.h"
#include <algorithm>
#include <glm/glm.hpp>

namespace {
// Bloom levels from half size down. 5 spread a highlight widely; a 6th would be a few dozen texels across, and its
// blocky shape shows as a rectangle around bright objects.
constexpr int kBloomLevels = 5;

GLuint createColorTarget(int width, int height, GLenum format) {
  GLuint texture = 0;
  glGenTextures(1, &texture);
  glBindTexture(GL_TEXTURE_2D, texture);
  glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, GL_RGBA, GL_FLOAT, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  return texture;
}
} // namespace

void PostProcess::resize(int width, int height) {
  if (width == m_width && height == m_height && m_sceneFBO)
    return;
  release();
  m_width = std::max(width, 1);
  m_height = std::max(height, 1);

  // Scene: 16-bit float color (alpha too, for blending) and depth
  m_sceneColor = createColorTarget(m_width, m_height, GL_RGBA16F);
  glGenRenderbuffers(1, &m_sceneDepth);
  glBindRenderbuffer(GL_RENDERBUFFER, m_sceneDepth);
  glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, m_width, m_height);
  glGenFramebuffers(1, &m_sceneFBO);
  glBindFramebuffer(GL_FRAMEBUFFER, m_sceneFBO);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_sceneColor, 0);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_sceneDepth);
  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    LOG_E("[PostProcess] The HDR scene target is incomplete");

  // Display: 8-bit color with the scene's depth, so particles drawn into it are hidden behind geometry
  glGenTextures(1, &m_displayColor);
  glBindTexture(GL_TEXTURE_2D, m_displayColor);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, m_width, m_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glGenFramebuffers(1, &m_displayFBO);
  glBindFramebuffer(GL_FRAMEBUFFER, m_displayFBO);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_displayColor, 0);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_sceneDepth);
  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    LOG_E("[PostProcess] The display target is incomplete");

  // Bloom: a chain of half-size levels, 11/11/10-bit float (no alpha needed, half the memory of RGBA16F)
  int levelWidth = m_width, levelHeight = m_height;
  for (int i = 0; i < kBloomLevels && levelWidth > 2 && levelHeight > 2; ++i) {
    levelWidth = std::max(levelWidth / 2, 1);
    levelHeight = std::max(levelHeight / 2, 1);
    m_bloom.push_back({createColorTarget(levelWidth, levelHeight, GL_R11F_G11F_B10F), levelWidth, levelHeight});
  }
  glGenFramebuffers(1, &m_bloomFBO);

  if (!m_emptyVAO)
    glGenVertexArrays(1, &m_emptyVAO);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void PostProcess::beginScene() {
  glBindFramebuffer(GL_FRAMEBUFFER, m_sceneFBO);
  glViewport(0, 0, m_width, m_height);
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void PostProcess::drawFullscreen() {
  glBindVertexArray(m_emptyVAO);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  glBindVertexArray(0);
}

PostProcess::SavedState PostProcess::beginFullscreenPasses() {
  SavedState state;
  GLint polygonMode[2] = {GL_FILL, GL_FILL};
  glGetIntegerv(GL_POLYGON_MODE, polygonMode);
  state.polygonMode = polygonMode[0];
  state.depthTest = glIsEnabled(GL_DEPTH_TEST);
  state.cullFace = glIsEnabled(GL_CULL_FACE);
  state.blend = glIsEnabled(GL_BLEND);
  glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  glDisable(GL_BLEND);
  return state;
}

void PostProcess::endFullscreenPasses(const SavedState &state) {
  glPolygonMode(GL_FRONT_AND_BACK, state.polygonMode);
  if (state.depthTest)
    glEnable(GL_DEPTH_TEST);
  if (state.cullFace)
    glEnable(GL_CULL_FACE);
  if (state.blend)
    glEnable(GL_BLEND);
}

void PostProcess::resolveToDisplay(Shader &tonemap, const Settings &settings) {
  const SavedState state = beginFullscreenPasses();
  glBindFramebuffer(GL_FRAMEBUFFER, m_displayFBO);
  glViewport(0, 0, m_width, m_height);
  tonemap.use();
  tonemap.setTex("sceneTexture", m_sceneColor, 0);
  tonemap.setInt("gammaCorrect", settings.gammaCorrect ? 1 : 0);
  drawFullscreen();
  endFullscreenPasses(state);
}

void PostProcess::finish(Shader &downsample, Shader &upsample, Shader &composite, const Settings &settings) {
  const SavedState state = beginFullscreenPasses();

  const bool bloom = settings.bloom && !m_bloom.empty();
  if (bloom) {
    glBindFramebuffer(GL_FRAMEBUFFER, m_bloomFBO);

    // Down: scene -> half -> quarter..., the first pass keeping only highlights
    downsample.use();
    downsample.setFloat("threshold", settings.bloomThreshold);
    downsample.setFloat("knee", settings.bloomThreshold * 0.25f);
    GLuint source = m_sceneColor;
    glm::vec2 sourceTexel(1.0f / m_width, 1.0f / m_height);
    for (size_t i = 0; i < m_bloom.size(); ++i) {
      const BloomLevel &level = m_bloom[i];
      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, level.texture, 0);
      glViewport(0, 0, level.width, level.height);
      downsample.setTex("sourceTexture", source, 0);
      downsample.setVec2("sourceTexel", sourceTexel);
      downsample.setInt("prefilter", i == 0 ? 1 : 0);
      drawFullscreen();
      source = level.texture;
      sourceTexel = glm::vec2(1.0f / level.width, 1.0f / level.height);
    }

    // Up: each level added onto the next larger one, ending in the half-size level
    upsample.use();
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    for (size_t i = m_bloom.size() - 1; i > 0; --i) {
      const BloomLevel &smaller = m_bloom[i];
      const BloomLevel &larger = m_bloom[i - 1];
      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, larger.texture, 0);
      glViewport(0, 0, larger.width, larger.height);
      upsample.setTex("sourceTexture", smaller.texture, 0);
      upsample.setVec2("sourceTexel", glm::vec2(1.0f / smaller.width, 1.0f / smaller.height));
      drawFullscreen();
    }
    glDisable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  }

  // Onto the screen
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glViewport(0, 0, m_width, m_height);
  composite.use();
  composite.setTex("displayTexture", m_displayColor, 0);
  composite.setTex("bloomTexture", bloom ? m_bloom[0].texture : m_displayColor, 1);
  composite.setInt("bloomEnabled", bloom ? 1 : 0);
  composite.setFloat("bloomStrength", settings.bloomStrength);
  composite.setInt("gammaCorrect", settings.gammaCorrect ? 1 : 0);
  drawFullscreen();
  endFullscreenPasses(state);
}

void PostProcess::release() {
  if (m_sceneFBO)
    glDeleteFramebuffers(1, &m_sceneFBO);
  if (m_sceneColor)
    glDeleteTextures(1, &m_sceneColor);
  if (m_sceneDepth)
    glDeleteRenderbuffers(1, &m_sceneDepth);
  if (m_displayFBO)
    glDeleteFramebuffers(1, &m_displayFBO);
  if (m_displayColor)
    glDeleteTextures(1, &m_displayColor);
  if (m_bloomFBO)
    glDeleteFramebuffers(1, &m_bloomFBO);
  for (BloomLevel &level : m_bloom)
    glDeleteTextures(1, &level.texture);
  m_bloom.clear();
  m_sceneFBO = m_sceneColor = m_sceneDepth = m_displayFBO = m_displayColor = m_bloomFBO = 0;
  m_width = m_height = 0;
}

void PostProcess::shutdown() {
  release();
  if (m_emptyVAO) {
    glDeleteVertexArrays(1, &m_emptyVAO);
    m_emptyVAO = 0;
  }
}
