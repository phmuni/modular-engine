
// Renderer implementation: frame management, shadow mapping, and particle rendering.

#include "rendering/renderer.h"
#include "foundation/core/logger.h"
#include "rendering/resources/mesh.h"
#include <algorithm>
#include <cmath>
#include <vector>

void Renderer::init(SDL_Window *window) {
  m_window = window;
  SDL_GetWindowSizeInPixels(window, &m_screenWidth, &m_screenHeight);
  updateTargets();
  initShadowMapping();
  initParticleBuffers();
  // Point shadows are filtered across cube faces instead of showing seams at the edges
  glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
  glGenBuffers(1, &m_instanceVBO);
  glGenVertexArrays(1, &m_skyboxVAO);
}

void Renderer::drawSkybox() const {
  // At depth 1 (the cleared value), so LEQUAL keeps it behind everything; it writes no depth of its own
  GLint polygonMode[2] = {GL_FILL, GL_FILL};
  glGetIntegerv(GL_POLYGON_MODE, polygonMode);
  const GLboolean culling = glIsEnabled(GL_CULL_FACE);
  glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
  glDepthFunc(GL_LEQUAL);
  glDepthMask(GL_FALSE);
  glDisable(GL_CULL_FACE);
  glBindVertexArray(m_skyboxVAO);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  glBindVertexArray(0);
  ++m_stats.drawCalls;
  if (culling)
    glEnable(GL_CULL_FACE);
  glDepthMask(GL_TRUE);
  glDepthFunc(GL_LESS);
  glPolygonMode(GL_FRONT_AND_BACK, polygonMode[0]);
}

void Renderer::beginFrame() const {
  m_stats = {};
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void Renderer::endFrame() const { SDL_GL_SwapWindow(m_window); }

// The whole index buffer in one call: the parts are laid out back to back
void Renderer::drawMesh(const Mesh &mesh) const { drawIndexRange(mesh, 0, mesh.getIndexCount()); }

void Renderer::drawSubmesh(const Mesh &mesh, const Submesh &submesh) const {
  drawIndexRange(mesh, submesh.indexStart, submesh.indexCount);
}

void Renderer::drawIndexRange(const Mesh &mesh, uint32_t indexStart, uint32_t indexCount) const {
  m_stats.drawCalls += 1;
  m_stats.meshesDrawn += 1;
  glBindVertexArray(mesh.getVAO());
  glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(indexCount), GL_UNSIGNED_INT,
                 (void *)(static_cast<size_t>(indexStart) * sizeof(uint32_t)));
  glBindVertexArray(0);
}

void Renderer::uploadInstances(const InstanceData *data, int count) {
  glBindBuffer(GL_ARRAY_BUFFER, m_instanceVBO);
  glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(count * sizeof(InstanceData)), data, GL_STREAM_DRAW);
}

// Points the mesh's VAO at the instance buffer. Cheap enough to redo per draw, and it stays correct if a VAO name
// is reused by a newly loaded mesh.
void Renderer::bindInstanceAttributes(const Mesh &mesh) const {
  glBindVertexArray(mesh.getVAO());
  glBindBuffer(GL_ARRAY_BUFFER, m_instanceVBO);

  constexpr GLsizei stride = sizeof(InstanceData);
  for (int column = 0; column < 4; ++column) {
    GLuint location = 3 + column;
    glEnableVertexAttribArray(location);
    glVertexAttribPointer(location, 4, GL_FLOAT, GL_FALSE, stride,
                          (void *)(offsetof(InstanceData, model) + sizeof(glm::vec4) * column));
    glVertexAttribDivisor(location, 1);
  }
  for (int column = 0; column < 3; ++column) {
    GLuint location = 7 + column;
    glEnableVertexAttribArray(location);
    glVertexAttribPointer(location, 3, GL_FLOAT, GL_FALSE, stride,
                          (void *)(offsetof(InstanceData, normal) + sizeof(glm::vec3) * column));
    glVertexAttribDivisor(location, 1);
  }
}

void Renderer::drawMeshInstanced(const Mesh &mesh, int count) const {
  drawIndexRangeInstanced(mesh, 0, mesh.getIndexCount(), count);
}

void Renderer::drawSubmeshInstanced(const Mesh &mesh, const Submesh &submesh, int count) const {
  drawIndexRangeInstanced(mesh, submesh.indexStart, submesh.indexCount, count);
}

void Renderer::drawIndexRangeInstanced(const Mesh &mesh, uint32_t indexStart, uint32_t indexCount, int count) const {
  m_stats.drawCalls += 1;
  m_stats.meshesDrawn += count;
  bindInstanceAttributes(mesh);
  glDrawElementsInstanced(GL_TRIANGLES, static_cast<GLsizei>(indexCount), GL_UNSIGNED_INT,
                          (void *)(static_cast<size_t>(indexStart) * sizeof(uint32_t)), count);
  glBindVertexArray(0);
}

void Renderer::beginShadowPass(int cascade) {
  glViewport(0, 0, EngineConfig::SHADOW_CASCADE_SIZE, EngineConfig::SHADOW_CASCADE_SIZE);
  glBindFramebuffer(GL_FRAMEBUFFER, m_depthMapFBO);
  glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, m_depthMap, 0, cascade);
  glClear(GL_DEPTH_BUFFER_BIT);
}

void Renderer::beginScene(const glm::vec3 &clearColor) { m_postProcess.beginScene(clearColor); }

void Renderer::resolveScene(Shader &tonemap, const PostProcess::Settings &settings) {
  m_postProcess.resolveToDisplay(tonemap, settings);
}

void Renderer::finishScene(const PostProcess::Shaders &shaders, const PostProcess::Settings &settings) {
  m_postProcess.finish(shaders, settings);
}

namespace {
// 0, 2, 4 or 8: other numbers snap down (1 and below mean off)
int snapSamples(int samples) {
  if (samples >= 8)
    return 8;
  if (samples >= 4)
    return 4;
  return samples >= 2 ? 2 : 0;
}
} // namespace

void Renderer::setMsaa(int samples) {
  m_antialiasing.msaa = snapSamples(samples);
  if (!m_window) // before init() the choice waits for the targets to be made (and for GL_MAX_SAMPLES)
    return;
  m_antialiasing.msaa = snapSamples(std::min(m_antialiasing.msaa, PostProcess::maxSamples()));
  updateTargets();
}

void Renderer::setFsrSharpness(float stops) {
  m_antialiasing.fsrSharpness = std::isfinite(stops) ? std::clamp(stops, kMinFsrSharpness, kMaxFsrSharpness)
                                                     : AntialiasingSettings().fsrSharpness;
}

void Renderer::applyAntialiasing(PostProcess::Settings &settings) const {
  settings.fxaa = m_antialiasing.fxaa;
  settings.fsr = m_antialiasing.fsr;
  settings.fsrSharpness = m_antialiasing.fsrSharpness;
}

void Renderer::setAlphaToCoverage(bool enabled) const {
  if (enabled && alphaToCoverageAvailable())
    glEnable(GL_SAMPLE_ALPHA_TO_COVERAGE);
  else
    glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);
}

void Renderer::endShadowPass() {
  // Back to the scene target, which the shadow passes interrupted
  glBindFramebuffer(GL_FRAMEBUFFER, m_postProcess.sceneFramebuffer());
  glViewport(0, 0, m_renderWidth, m_renderHeight);
}

void Renderer::initShadowMapping() {
  glGenFramebuffers(1, &m_depthMapFBO);
  glGenTextures(1, &m_depthMap);

  glBindTexture(GL_TEXTURE_2D_ARRAY, m_depthMap);
  glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_DEPTH_COMPONENT24, EngineConfig::SHADOW_CASCADE_SIZE,
               EngineConfig::SHADOW_CASCADE_SIZE, EngineConfig::SHADOW_CASCADES, 0, GL_DEPTH_COMPONENT, GL_FLOAT,
               nullptr);

  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
  float borderColor[] = {1.0f, 1.0f, 1.0f, 1.0f};
  glTexParameterfv(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_BORDER_COLOR, borderColor);
  // Sampled through sampler2DArrayShadow: the hardware compares depths and filters the results
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);

  glBindFramebuffer(GL_FRAMEBUFFER, m_depthMapFBO);
  glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, m_depthMap, 0, 0);
  glDrawBuffer(GL_NONE);
  glReadBuffer(GL_NONE);

  GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
  if (status != GL_FRAMEBUFFER_COMPLETE) {
    LOG_E("Shadow map FBO incomplete! Status: 0x%x", status);
  }

  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

GLuint Renderer::createDepthCubeArray(int size, int cubes) {
  GLuint array = 0;
  glGenTextures(1, &array);
  glBindTexture(GL_TEXTURE_CUBE_MAP_ARRAY, array);
  // Layer = cube * 6 + face (+X, -X, +Y, -Y, +Z, -Z)
  glTexImage3D(GL_TEXTURE_CUBE_MAP_ARRAY, 0, GL_DEPTH_COMPONENT24, size, size, cubes * 6, 0, GL_DEPTH_COMPONENT,
               GL_FLOAT, nullptr);
  glTexParameteri(GL_TEXTURE_CUBE_MAP_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_CUBE_MAP_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_CUBE_MAP_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_CUBE_MAP_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_CUBE_MAP_ARRAY, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
  // Sampled through samplerCubeArrayShadow, like the directional map: hardware comparison with bilinear filtering
  glTexParameteri(GL_TEXTURE_CUBE_MAP_ARRAY, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
  glTexParameteri(GL_TEXTURE_CUBE_MAP_ARRAY, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
  // Start every layer at the far plane: every comparison passes, so nothing is shadowed
  GLuint fbo = 0;
  glGenFramebuffers(1, &fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  glDrawBuffer(GL_NONE);
  glReadBuffer(GL_NONE);
  glDepthMask(GL_TRUE);
  for (int layer = 0; layer < cubes * 6; ++layer) {
    glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, array, 0, layer);
    glClear(GL_DEPTH_BUFFER_BIT);
  }
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glDeleteFramebuffers(1, &fbo);
  return array;
}

void Renderer::beginPointShadowFace(int slot, int face) {
  if (!m_pointShadowFBO) {
    glGenFramebuffers(1, &m_pointShadowFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, m_pointShadowFBO);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
  }
  if (!m_pointShadowMaps)
    m_pointShadowMaps = createDepthCubeArray(EngineConfig::POINT_SHADOW_SIZE, EngineConfig::MAX_POINT_SHADOWS);

  glViewport(0, 0, EngineConfig::POINT_SHADOW_SIZE, EngineConfig::POINT_SHADOW_SIZE);
  glBindFramebuffer(GL_FRAMEBUFFER, m_pointShadowFBO);
  glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, m_pointShadowMaps, 0, slot * 6 + face);
  glClear(GL_DEPTH_BUFFER_BIT);
}

GLuint Renderer::getPointShadowMap() const {
  if (m_pointShadowMaps)
    return m_pointShadowMaps;
  if (!m_emptyCubeMap)
    m_emptyCubeMap = createDepthCubeArray(1, 1); // no light has cast a shadow yet: a 1x1 array with nothing shadowed
  return m_emptyCubeMap;
}

GLuint Renderer::getDepthMap() const { return m_depthMap; }

bool Renderer::saveScreenshot(const std::string &path) const {
  const int width = m_screenWidth;
  const int height = m_screenHeight;
  const int stride = width * 4;
  std::vector<uint8_t> pixels(static_cast<size_t>(stride) * height);

  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glBindFramebuffer(GL_READ_FRAMEBUFFER, 0); // the screen, where the scene was composited
  glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

  // The back buffer's alpha is not meaningful (blending writes into it); screenshots are opaque
  for (size_t i = 3; i < pixels.size(); i += 4)
    pixels[i] = 255;

  // OpenGL rows start at the bottom; images start at the top
  std::vector<uint8_t> flipped(pixels.size());
  for (int y = 0; y < height; ++y)
    std::copy_n(pixels.begin() + static_cast<size_t>(height - 1 - y) * stride, stride,
                flipped.begin() + static_cast<size_t>(y) * stride);

  SDL_Surface *surface = SDL_CreateSurfaceFrom(width, height, SDL_PIXELFORMAT_RGBA32, flipped.data(), stride);
  if (!surface) {
    LOG_E("[Renderer] Failed to create screenshot surface: %s", SDL_GetError());
    return false;
  }

  bool saved = SDL_SaveBMP(surface, path.c_str());
  SDL_DestroySurface(surface);
  if (!saved) {
    LOG_E("[Renderer] Failed to save screenshot to %s: %s", path.c_str(), SDL_GetError());
    return false;
  }

  LOG_I("[Renderer] Screenshot saved to %s", path.c_str());
  return true;
}

void Renderer::setFaceCulling(bool enabled) const {
  if (enabled == m_faceCulling)
    return;
  m_faceCulling = enabled;
  if (enabled)
    glEnable(GL_CULL_FACE);
  else
    glDisable(GL_CULL_FACE);
}

void Renderer::setWireframe(bool enabled) { glPolygonMode(GL_FRONT_AND_BACK, enabled ? GL_LINE : GL_FILL); }

void Renderer::setViewportSize(int width, int height) {
  m_screenWidth = width;
  m_screenHeight = height;
  if (m_view.active) { // the view stays inside the new size (the editor sets it again next frame)
    ViewRect keep = m_view;
    m_view = ViewRect();
    setViewRect(keep);
    return;
  }
  updateTargets();
  glViewport(0, 0, m_screenWidth, m_screenHeight);
}

void Renderer::setViewRect(const ViewRect &rect) {
  ViewRect wanted = rect;
  if (wanted.active) {
    // Inside the window, at least a pixel
    wanted.x = std::clamp(wanted.x, 0, std::max(m_screenWidth - 1, 0));
    wanted.y = std::clamp(wanted.y, 0, std::max(m_screenHeight - 1, 0));
    wanted.width = std::clamp(wanted.width, 1, std::max(m_screenWidth - wanted.x, 1));
    wanted.height = std::clamp(wanted.height, 1, std::max(m_screenHeight - wanted.y, 1));
  }
  if (wanted == m_view)
    return;
  m_view = wanted;
  if (m_window)
    updateTargets();
}

ViewRect Renderer::viewRect() const {
  if (m_view.active)
    return m_view;
  return ViewRect{0, 0, m_screenWidth, m_screenHeight, false};
}

void Renderer::setRenderScale(float scale) {
  m_renderScale = std::clamp(scale, kMinRenderScale, kMaxRenderScale);
  if (m_window) // before init() the scale waits for the targets to be made
    updateTargets();
}

// The scene's targets follow the screen's size times the render scale; the screen is only drawn onto at the end
void Renderer::updateTargets() {
  // A target larger than the GPU allows would fail to be created: a 4K screen at 200 % is already 7680 pixels wide
  static GLint maxSize = 0;
  if (!maxSize)
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxSize);
  const auto scaled = [&](int pixels) {
    return std::clamp(static_cast<int>(std::lround(static_cast<double>(pixels) * m_renderScale)), 1,
                      std::max(static_cast<int>(maxSize), 1));
  };
  m_renderWidth = scaled(getViewWidth());
  m_renderHeight = scaled(getViewHeight());
  m_postProcess.resize(m_renderWidth, m_renderHeight, getViewWidth(), getViewHeight(), m_antialiasing.msaa);
  // The final pass fills the view; GL measures from the bottom
  m_postProcess.setOutputOrigin(m_view.active ? m_view.x : 0, m_view.active ? m_screenHeight - m_view.y - m_view.height : 0);
  m_antialiasing.msaa = m_postProcess.samples(); // what the GPU gave (a multisampled target that failed is off)
}

void Renderer::beginTransparentPass() {
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glDepthMask(GL_FALSE);
}

void Renderer::endTransparentPass() {
  glDepthMask(GL_TRUE);
  glDisable(GL_BLEND);
}

void Renderer::beginSoftTransparentPass() {
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glDepthMask(GL_TRUE);
}

void Renderer::endSoftTransparentPass() { glDisable(GL_BLEND); }

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
  const auto size = static_cast<GLsizeiptr>(count * sizeof(ParticleVertex));
  glBindBuffer(GL_ARRAY_BUFFER, m_particleVBO);

  // Grow geometrically; otherwise orphan the old storage so the driver does not wait for the GPU to finish with it
  if (size > m_particleCapacity)
    m_particleCapacity = std::max(size, m_particleCapacity * 2);
  glBufferData(GL_ARRAY_BUFFER, m_particleCapacity, nullptr, GL_STREAM_DRAW);
  glBufferSubData(GL_ARRAY_BUFFER, 0, size, data);
}

void Renderer::drawParticles(int count, int images) {
  m_stats.drawCalls += 1;
  m_stats.particles += count;
  glBindVertexArray(m_particleVAO);
  glDrawArraysInstanced(GL_POINTS, 0, static_cast<GLsizei>(count), static_cast<GLsizei>(images));
  glBindVertexArray(0);
}

void Renderer::endParticlePass() {
  glDepthMask(GL_TRUE);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glDisable(GL_PROGRAM_POINT_SIZE);
}

void Renderer::shutdown() {
  m_postProcess.shutdown();
  if (m_skyboxVAO) {
    glDeleteVertexArrays(1, &m_skyboxVAO);
    m_skyboxVAO = 0;
  }
  if (m_instanceVBO) {
    glDeleteBuffers(1, &m_instanceVBO);
    m_instanceVBO = 0;
  }
  if (m_depthMapFBO) {
    glDeleteFramebuffers(1, &m_depthMapFBO);
    m_depthMapFBO = 0;
  }
  if (m_depthMap) {
    glDeleteTextures(1, &m_depthMap);
    m_depthMap = 0;
  }
  if (m_pointShadowMaps) {
    glDeleteTextures(1, &m_pointShadowMaps);
    m_pointShadowMaps = 0;
  }
  if (m_emptyCubeMap) {
    glDeleteTextures(1, &m_emptyCubeMap);
    m_emptyCubeMap = 0;
  }
  if (m_pointShadowFBO) {
    glDeleteFramebuffers(1, &m_pointShadowFBO);
    m_pointShadowFBO = 0;
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
