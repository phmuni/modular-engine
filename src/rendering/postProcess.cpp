// HDR target (multisampled when MSAA is on), bloom mip chain, FXAA and FSR passes and the final composite pass.

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

GLuint createDepthTarget(int width, int height) {
  GLuint texture = 0;
  glGenTextures(1, &texture);
  glBindTexture(GL_TEXTURE_2D, texture);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, width, height, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  return texture;
}

// 0 (off) or a power of two from 2 up to what the GPU offers
int effectiveSamples(int requested, int maxSamples) {
  int samples = std::min(requested, maxSamples);
  if (samples < 2)
    return 0;
  while (samples & (samples - 1)) // down to a power of two
    samples &= samples - 1;
  return samples;
}
} // namespace

int PostProcess::maxSamples() {
  static int maxSamples = -1;
  if (maxSamples < 0) {
    GLint value = 0;
    glGetIntegerv(GL_MAX_SAMPLES, &value);
    maxSamples = std::max(static_cast<int>(value), 0);
  }
  return maxSamples;
}

void PostProcess::resize(int width, int height, int displayWidth, int displayHeight, int samples) {
  width = std::max(width, 1);
  height = std::max(height, 1);
  displayWidth = std::max(displayWidth, 1);
  displayHeight = std::max(displayHeight, 1);
  samples = effectiveSamples(samples, maxSamples());
  if (width == m_width && height == m_height && samples == m_samples && m_sceneFBO) {
    // Only the screen changed (never the targets, which follow the render size)
    if (displayWidth != m_displayWidth || displayHeight != m_displayHeight)
      releaseScreenTargets();
    m_displayWidth = displayWidth;
    m_displayHeight = displayHeight;
    return;
  }
  release();
  m_width = width;
  m_height = height;
  m_displayWidth = displayWidth;
  m_displayHeight = displayHeight;

  // Scene: 16-bit float color (alpha too, for blending) and depth. With MSAA these are what the multisampled buffers
  // resolve into; without it the lit passes draw into them directly.
  m_sceneColor = createColorTarget(m_width, m_height, GL_RGBA16F);
  m_sceneDepth = createDepthTarget(m_width, m_height);
  glGenFramebuffers(1, &m_sceneFBO);
  glBindFramebuffer(GL_FRAMEBUFFER, m_sceneFBO);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_sceneColor, 0);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, m_sceneDepth, 0);
  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    LOG_E("[PostProcess] The HDR scene target is incomplete");
  if (samples >= 2 && !createMultisampleTargets(samples))
    LOG_W("[PostProcess] %dx MSAA is not available here: rendering without it", samples);

  // Display: 8-bit color with the scene's depth, so particles drawn into it are hidden behind geometry
  glGenTextures(1, &m_displayColor);
  glBindTexture(GL_TEXTURE_2D, m_displayColor);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, m_width, m_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
  // Linear, since the final pass stretches it onto the screen when the render scale is not 1 (at 1 every sample lands
  // on a texel's center, so the image is untouched)
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glGenFramebuffers(1, &m_displayFBO);
  glBindFramebuffer(GL_FRAMEBUFFER, m_displayFBO);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_displayColor, 0);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, m_sceneDepth, 0);
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

bool PostProcess::createMultisampleTargets(int samples) {
  glGenRenderbuffers(1, &m_renderColor);
  glBindRenderbuffer(GL_RENDERBUFFER, m_renderColor);
  glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_RGBA16F, m_width, m_height);
  glGenRenderbuffers(1, &m_renderDepth);
  glBindRenderbuffer(GL_RENDERBUFFER, m_renderDepth);
  glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_DEPTH_COMPONENT24, m_width, m_height);
  glGenFramebuffers(1, &m_renderFBO);
  glBindFramebuffer(GL_FRAMEBUFFER, m_renderFBO);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, m_renderColor);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_renderDepth);
  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) {
    m_samples = samples;
    return true;
  }
  // Fall back to the single-sample scene target
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glDeleteFramebuffers(1, &m_renderFBO);
  glDeleteRenderbuffers(1, &m_renderColor);
  glDeleteRenderbuffers(1, &m_renderDepth);
  m_renderFBO = m_renderColor = m_renderDepth = 0;
  m_samples = 0;
  return false;
}

// The multisampled scene averaged into the single-sample one: color and depth, for everything that comes after
void PostProcess::resolveMultisample() {
  if (!m_renderFBO)
    return;
  const GLboolean scissor = glIsEnabled(GL_SCISSOR_TEST);
  glDisable(GL_SCISSOR_TEST);
  glBindFramebuffer(GL_READ_FRAMEBUFFER, m_renderFBO);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_sceneFBO);
  glBlitFramebuffer(0, 0, m_width, m_height, 0, 0, m_width, m_height, GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT,
                    GL_NEAREST);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  if (scissor)
    glEnable(GL_SCISSOR_TEST);
}

void PostProcess::beginScene(const glm::vec3 &clearColor) {
  glBindFramebuffer(GL_FRAMEBUFFER, sceneFramebuffer());
  glViewport(0, 0, m_width, m_height);
  glClearColor(clearColor.r, clearColor.g, clearColor.b, 1.0f);
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
  resolveMultisample();
  const SavedState state = beginFullscreenPasses();
  glBindFramebuffer(GL_FRAMEBUFFER, m_displayFBO);
  glViewport(0, 0, m_width, m_height);
  tonemap.use();
  tonemap.setTex("sceneTexture", m_sceneColor, 0);
  tonemap.setInt("gammaCorrect", settings.gammaCorrect ? 1 : 0);
  drawFullscreen();
  endFullscreenPasses(state);
}

void PostProcess::ensureFilterTarget(FilterTarget &target, int width, int height) {
  if (target.fbo && target.width == width && target.height == height)
    return;
  releaseFilterTarget(target);
  target.width = width;
  target.height = height;
  glGenTextures(1, &target.texture);
  glBindTexture(GL_TEXTURE_2D, target.texture);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glGenFramebuffers(1, &target.fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, target.fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target.texture, 0);
  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    LOG_E("[PostProcess] A filter target is incomplete");
}

void PostProcess::releaseFilterTarget(FilterTarget &target) {
  if (target.fbo)
    glDeleteFramebuffers(1, &target.fbo);
  if (target.texture)
    glDeleteTextures(1, &target.texture);
  target = FilterTarget();
}

void PostProcess::releaseScreenTargets() {
  releaseFilterTarget(m_upscaleTarget);
  releaseFilterTarget(m_sharpenTarget);
}

void PostProcess::finish(const Shaders &shaders, const Settings &settings) {
  Shader &downsample = *shaders.bloomDownsample;
  Shader &upsample = *shaders.bloomUpsample;
  Shader &composite = *shaders.composite;
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

  // The display image through the filters. FXAA first, at the render size, so the upscale gets a clean image.
  GLuint image = m_displayColor;
  if (settings.fxaa && shaders.fxaa) {
    ensureFilterTarget(m_fxaaTarget, m_width, m_height);
    glBindFramebuffer(GL_FRAMEBUFFER, m_fxaaTarget.fbo);
    glViewport(0, 0, m_width, m_height);
    shaders.fxaa->use();
    shaders.fxaa->setTex("sourceTexture", image, 0);
    shaders.fxaa->setVec2("texel", glm::vec2(1.0f / m_width, 1.0f / m_height));
    drawFullscreen();
    image = m_fxaaTarget.texture;
  }
  // FSR 1: EASU upscales render size -> screen size, RCAS sharpens. Only while the render size is below the screen's;
  // at 1 or above there is nothing to reconstruct, so the image goes straight on (above 1 it is a plain downsample).
  const bool upscale = settings.fsr && shaders.easu && shaders.rcas && (m_width < m_displayWidth || m_height < m_displayHeight);
  if (upscale) {
    ensureFilterTarget(m_upscaleTarget, m_displayWidth, m_displayHeight);
    ensureFilterTarget(m_sharpenTarget, m_displayWidth, m_displayHeight);
    glBindFramebuffer(GL_FRAMEBUFFER, m_upscaleTarget.fbo);
    glViewport(0, 0, m_displayWidth, m_displayHeight);
    shaders.easu->use();
    shaders.easu->setTex("sourceTexture", image, 0);
    shaders.easu->setVec2("inputSize", glm::vec2(m_width, m_height));
    shaders.easu->setVec2("outputSize", glm::vec2(m_displayWidth, m_displayHeight));
    drawFullscreen();

    glBindFramebuffer(GL_FRAMEBUFFER, m_sharpenTarget.fbo);
    shaders.rcas->use();
    shaders.rcas->setTex("sourceTexture", m_upscaleTarget.texture, 0);
    shaders.rcas->setFloat("sharpness", std::clamp(settings.fsrSharpness, 0.0f, 2.0f));
    drawFullscreen();
    image = m_sharpenTarget.texture;
  }

  // Onto the screen, at its own size: this is where the render scale is undone (by the stretch, if FSR did not)
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glViewport(m_outputX, m_outputY, m_displayWidth, m_displayHeight);
  composite.use();
  composite.setTex("displayTexture", image, 0);
  composite.setTex("bloomTexture", bloom ? m_bloom[0].texture : image, 1);
  composite.setInt("bloomEnabled", bloom ? 1 : 0);
  composite.setFloat("bloomStrength", settings.bloomStrength);
  composite.setInt("gammaCorrect", settings.gammaCorrect ? 1 : 0);
  drawFullscreen();
  endFullscreenPasses(state);
}

void PostProcess::release() {
  releaseScreenTargets();
  releaseFilterTarget(m_fxaaTarget);
  if (m_renderFBO)
    glDeleteFramebuffers(1, &m_renderFBO);
  if (m_renderColor)
    glDeleteRenderbuffers(1, &m_renderColor);
  if (m_renderDepth)
    glDeleteRenderbuffers(1, &m_renderDepth);
  m_renderFBO = m_renderColor = m_renderDepth = 0;
  m_samples = 0;
  if (m_sceneFBO)
    glDeleteFramebuffers(1, &m_sceneFBO);
  if (m_sceneColor)
    glDeleteTextures(1, &m_sceneColor);
  if (m_sceneDepth)
    glDeleteTextures(1, &m_sceneDepth);
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
  m_displayWidth = m_displayHeight = 0;
}

void PostProcess::shutdown() {
  release();
  if (m_emptyVAO) {
    glDeleteVertexArrays(1, &m_emptyVAO);
    m_emptyVAO = 0;
  }
}
