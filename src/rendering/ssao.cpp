// SSAO: the depth prepass target, the AO pass and its depth-aware blur (see ssao.h).

#include "rendering/ssao.h"

#include "foundation/core/config.h"
#include "foundation/core/logger.h"
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <vector>

int ssaoSampleCount(SsaoQuality quality) {
  switch (quality) {
  case SsaoQuality::Low: return 8;
  case SsaoQuality::High: return 32;
  default: return 16;
  }
}

const char *ssaoQualityName(SsaoQuality quality) {
  switch (quality) {
  case SsaoQuality::Low: return "low";
  case SsaoQuality::High: return "high";
  default: return "medium";
  }
}

std::optional<SsaoQuality> ssaoQualityFromName(std::string_view name) {
  std::string lower(name);
  std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });
  if (lower == "low")
    return SsaoQuality::Low;
  if (lower == "medium")
    return SsaoQuality::Medium;
  if (lower == "high")
    return SsaoQuality::High;
  return std::nullopt;
}

SsaoSettings SsaoSettings::clamped() const {
  SsaoSettings result = *this;
  result.radius = std::clamp(radius, kMinRadius, kMaxRadius);
  result.intensity = std::clamp(intensity, 0.0f, kMaxIntensity);
  return result;
}

namespace {
constexpr const char *kFullscreenVertex = "../assets/shaders/fullscreen.vert";
constexpr const char *kAoFragment = "../assets/shaders/ssao.frag";
constexpr const char *kBlurFragment = "../assets/shaders/ssaoBlur.frag";
constexpr const char *kDebugFragment = "../assets/shaders/ssaoDebug.frag";

GLuint createTarget(GLenum internalFormat, GLenum format, GLenum type, int width, int height, GLenum filter) {
  GLuint texture = 0;
  glGenTextures(1, &texture);
  glBindTexture(GL_TEXTURE_2D, texture);
  glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, width, height, 0, format, type, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  return texture;
}

GLuint createColorFramebuffer(GLuint texture) {
  GLuint framebuffer = 0;
  glGenFramebuffers(1, &framebuffer);
  glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    LOG_E("[Ssao] The AO target is incomplete");
  return framebuffer;
}

// Fullscreen passes run without depth, culling or blending, filled even in wireframe mode
struct PassState {
  GLint polygonMode[2] = {GL_FILL, GL_FILL};
  GLboolean depthTest, cullFace, blend;
  GLint framebuffer = 0;
  GLint viewport[4] = {};
  PassState() {
    glGetIntegerv(GL_POLYGON_MODE, polygonMode);
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &framebuffer);
    glGetIntegerv(GL_VIEWPORT, viewport);
    depthTest = glIsEnabled(GL_DEPTH_TEST);
    cullFace = glIsEnabled(GL_CULL_FACE);
    blend = glIsEnabled(GL_BLEND);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
  }
  ~PassState() {
    glPolygonMode(GL_FRONT_AND_BACK, polygonMode[0]);
    if (depthTest)
      glEnable(GL_DEPTH_TEST);
    if (cullFace)
      glEnable(GL_CULL_FACE);
    if (blend)
      glEnable(GL_BLEND);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
  }
};

// A small deterministic generator, so the kernel and noise are the same every run
struct Random {
  uint32_t state = 12345u;
  float next() { // 0 to 1
    state = state * 1664525u + 1013904223u;
    return static_cast<float>(state >> 8) / 16777216.0f;
  }
};
} // namespace

void Ssao::ensureResources() {
  if (m_aoShader)
    return;
  const std::string vertex = EngineConfig::resolvePath(kFullscreenVertex);
  m_aoShader.emplace(vertex, EngineConfig::resolvePath(kAoFragment));
  m_blurShader.emplace(vertex, EngineConfig::resolvePath(kBlurFragment));
  m_debugShader.emplace(vertex, EngineConfig::resolvePath(kDebugFragment));
  glGenVertexArrays(1, &m_vao);

  // 4x4 rotations around the normal (x and y; the shader keeps z at 0)
  Random random;
  std::vector<float> noise;
  for (int i = 0; i < 16; ++i) {
    noise.push_back(random.next() * 2.0f - 1.0f);
    noise.push_back(random.next() * 2.0f - 1.0f);
  }
  glGenTextures(1, &m_noise);
  glBindTexture(GL_TEXTURE_2D, m_noise);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RG16F, 4, 4, 0, GL_RG, GL_FLOAT, noise.data());
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
}

// Samples in the +Z hemisphere, more of them close to the point: occlusion nearby matters most
void Ssao::buildKernel(int samples) {
  if (samples == m_kernelSamples)
    return;
  m_kernelSamples = samples;
  Random random;
  for (int i = 0; i < samples; ++i) {
    glm::vec3 direction(random.next() * 2.0f - 1.0f, random.next() * 2.0f - 1.0f, random.next() * 0.9f + 0.1f);
    direction = glm::normalize(direction);
    const float t = static_cast<float>(i + 1) / static_cast<float>(samples);
    m_kernel[i] = direction * (0.1f + 0.9f * t * t);
  }
}

void Ssao::releaseTargets() {
  if (m_depthFBO)
    glDeleteFramebuffers(1, &m_depthFBO);
  if (m_rawFBO)
    glDeleteFramebuffers(1, &m_rawFBO);
  if (m_finalFBO)
    glDeleteFramebuffers(1, &m_finalFBO);
  if (m_depth)
    glDeleteTextures(1, &m_depth);
  if (m_raw)
    glDeleteTextures(1, &m_raw);
  if (m_final)
    glDeleteTextures(1, &m_final);
  m_depthFBO = m_rawFBO = m_finalFBO = m_depth = m_raw = m_final = 0;
  m_width = m_height = 0;
}

void Ssao::resizeTargets(int width, int height, bool half) {
  width = std::max(width, 1);
  height = std::max(height, 1);
  if (width != m_width || height != m_height || !m_depth) {
    releaseTargets();
    m_width = width;
    m_height = height;
    m_depth = createTarget(GL_DEPTH_COMPONENT32F, GL_DEPTH_COMPONENT, GL_FLOAT, width, height, GL_NEAREST);
    glGenFramebuffers(1, &m_depthFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, m_depthFBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, m_depth, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
      LOG_E("[Ssao] The depth target is incomplete");
    m_final = createTarget(GL_R8, GL_RED, GL_UNSIGNED_BYTE, width, height, GL_NEAREST);
    m_finalFBO = createColorFramebuffer(m_final);
    m_raw = 0; // made below
  }
  if (!m_raw || half != m_rawHalf) {
    if (m_rawFBO)
      glDeleteFramebuffers(1, &m_rawFBO);
    if (m_raw)
      glDeleteTextures(1, &m_raw);
    m_rawHalf = half;
    m_raw = createTarget(GL_R8, GL_RED, GL_UNSIGNED_BYTE, half ? std::max(width / 2, 1) : width,
                         half ? std::max(height / 2, 1) : height, GL_NEAREST);
    m_rawFBO = createColorFramebuffer(m_raw);
  }
}

void Ssao::beginDepthPrepass(int width, int height) {
  ensureResources();
  resizeTargets(width, height, m_rawHalf);
  glBindFramebuffer(GL_FRAMEBUFFER, m_depthFBO);
  glViewport(0, 0, m_width, m_height);
  glEnable(GL_DEPTH_TEST);
  glDepthMask(GL_TRUE);
  glDepthFunc(GL_LESS);
  glClear(GL_DEPTH_BUFFER_BIT);
}

void Ssao::compute(const glm::mat4 &projection, const SsaoSettings &requested) {
  if (!m_depth)
    return;
  const SsaoSettings settings = requested.clamped();
  const bool half = settings.halfResolution;
  resizeTargets(m_width, m_height, half);
  const int scale = half ? 2 : 1;
  const int rawWidth = half ? std::max(m_width / 2, 1) : m_width;
  const int rawHeight = half ? std::max(m_height / 2, 1) : m_height;
  const int samples = ssaoSampleCount(settings.quality);
  buildKernel(samples);
  const glm::vec4 projInfo(1.0f / projection[0][0], 1.0f / projection[1][1], projection[2][2], projection[3][2]);

  PassState state;
  glBindVertexArray(m_vao);

  glBindFramebuffer(GL_FRAMEBUFFER, m_rawFBO);
  glViewport(0, 0, rawWidth, rawHeight);
  m_aoShader->use();
  m_aoShader->setTex("depthTexture", m_depth, 0);
  m_aoShader->setTex("noiseTexture", m_noise, 1);
  glUniform3fv(glGetUniformLocation(m_aoShader->getShaderID(), "kernel"), samples, &m_kernel[0].x);
  m_aoShader->setInt("kernelSize", samples);
  m_aoShader->setVec4("projInfo", projInfo);
  m_aoShader->setMat4("projection", projection);
  m_aoShader->setInt("depthScale", scale);
  m_aoShader->setFloat("radius", settings.radius);
  m_aoShader->setFloat("intensity", settings.intensity);
  glDrawArrays(GL_TRIANGLES, 0, 3);

  glBindFramebuffer(GL_FRAMEBUFFER, m_finalFBO);
  glViewport(0, 0, m_width, m_height);
  m_blurShader->use();
  m_blurShader->setTex("aoTexture", m_raw, 0);
  m_blurShader->setTex("depthTexture", m_depth, 1);
  m_blurShader->setVec4("projInfo", projInfo);
  m_blurShader->setInt("depthScale", scale);
  glDrawArrays(GL_TRIANGLES, 0, 3);

  glBindVertexArray(0);
}

void Ssao::drawDebug() {
  if (!m_final)
    return;
  PassState state;
  glBindVertexArray(m_vao);
  m_debugShader->use();
  m_debugShader->setTex("aoTexture", m_final, 0);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  glBindVertexArray(0);
}

void Ssao::shutdown() {
  releaseTargets();
  if (m_noise)
    glDeleteTextures(1, &m_noise);
  if (m_vao)
    glDeleteVertexArrays(1, &m_vao);
  m_noise = m_vao = 0;
  m_aoShader.reset();
  m_blurShader.reset();
  m_debugShader.reset();
}
