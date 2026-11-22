// Image-based lighting: the environment, irradiance and pre-filtered cube maps and the BRDF table (see ibl.h).

#include "rendering/ibl.h"

#include "foundation/core/config.h"
#include "foundation/core/logger.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <stb_image/stb_image.h>
#include <vector>

namespace {
constexpr const char *kFullscreenVertex = "../assets/shaders/fullscreen.vert";
constexpr const char *kEquirectFragment = "../assets/shaders/iblEquirect.frag";
constexpr const char *kIrradianceFragment = "../assets/shaders/iblIrradiance.frag";
constexpr const char *kPrefilterFragment = "../assets/shaders/iblPrefilter.frag";
constexpr const char *kBrdfFragment = "../assets/shaders/iblBrdf.frag";

using Clock = std::chrono::steady_clock;
double millisecondsSince(Clock::time_point start) {
  return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

// Fullscreen passes run without depth, culling or blending, filled even in wireframe mode; the state comes back after
struct PassState {
  GLint polygonMode[2] = {GL_FILL, GL_FILL};
  GLboolean depthTest, cullFace, blend, scissor;
  GLint framebuffer = 0;
  GLint viewport[4] = {};
  GLint activeTexture = GL_TEXTURE0;
  GLint boundTexture2D = 0;
  GLint boundCube = 0;
  PassState() {
    glGetIntegerv(GL_POLYGON_MODE, polygonMode);
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &framebuffer);
    glGetIntegerv(GL_VIEWPORT, viewport);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &activeTexture);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &boundTexture2D);
    glGetIntegerv(GL_TEXTURE_BINDING_CUBE_MAP, &boundCube);
    depthTest = glIsEnabled(GL_DEPTH_TEST);
    cullFace = glIsEnabled(GL_CULL_FACE);
    blend = glIsEnabled(GL_BLEND);
    scissor = glIsEnabled(GL_SCISSOR_TEST);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);
  }
  ~PassState() {
    glPolygonMode(GL_FRONT_AND_BACK, polygonMode[0]);
    if (depthTest)
      glEnable(GL_DEPTH_TEST);
    if (cullFace)
      glEnable(GL_CULL_FACE);
    if (blend)
      glEnable(GL_BLEND);
    if (scissor)
      glEnable(GL_SCISSOR_TEST);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(boundTexture2D));
    glBindTexture(GL_TEXTURE_CUBE_MAP, static_cast<GLuint>(boundCube));
    glActiveTexture(static_cast<GLenum>(activeTexture));
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(framebuffer));
    glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
  }
};

void setLinearClamped(GLenum target, GLenum minFilter) {
  glTexParameteri(target, GL_TEXTURE_MIN_FILTER, minFilter);
  glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

bool isHdrFile(const std::string &path) { return stbi_is_hdr(path.c_str()) != 0; }
} // namespace

GLuint Ibl::makeCube(int size, int levels) const {
  GLuint cube = 0;
  glGenTextures(1, &cube);
  glBindTexture(GL_TEXTURE_CUBE_MAP, cube);
  for (int level = 0; level < levels; ++level) {
    const int levelSize = std::max(size >> level, 1);
    for (int face = 0; face < 6; ++face)
      glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, level, GL_RGBA16F, levelSize, levelSize, 0, GL_RGBA,
                   GL_FLOAT, nullptr);
  }
  setLinearClamped(GL_TEXTURE_CUBE_MAP, levels > 1 ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_BASE_LEVEL, 0);
  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAX_LEVEL, levels - 1);
  return cube;
}

void Ibl::ensureResources() {
  if (m_equirectShader)
    return;
  const std::string vertex = EngineConfig::resolvePath(kFullscreenVertex);
  m_equirectShader.emplace(vertex, EngineConfig::resolvePath(kEquirectFragment));
  m_irradianceShader.emplace(vertex, EngineConfig::resolvePath(kIrradianceFragment));
  m_prefilterShader.emplace(vertex, EngineConfig::resolvePath(kPrefilterFragment));
  m_brdfShader.emplace(vertex, EngineConfig::resolvePath(kBrdfFragment));
  glGenVertexArrays(1, &m_vao);
  glGenFramebuffers(1, &m_fbo);
}

GLuint Ibl::irradiance() const {
  if (m_irradiance)
    return m_irradiance;
  if (!m_blackCube) {
    m_blackCube = makeCube(1, 1);
    const float black[4] = {0, 0, 0, 1};
    for (int face = 0; face < 6; ++face)
      glTexSubImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, 0, 0, 0, 1, 1, GL_RGBA, GL_FLOAT, black);
  }
  return m_blackCube;
}

GLuint Ibl::prefiltered() const { return m_prefiltered ? m_prefiltered : irradiance(); }

void Ibl::ensureBrdf() {
  if (m_brdf)
    return;
  ensureResources();
  PassState state;
  glGenTextures(1, &m_brdf);
  glBindTexture(GL_TEXTURE_2D, m_brdf);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RG16F, kBrdfSize, kBrdfSize, 0, GL_RG, GL_FLOAT, nullptr);
  setLinearClamped(GL_TEXTURE_2D, GL_LINEAR);
  glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_brdf, 0);
  glViewport(0, 0, kBrdfSize, kBrdfSize);
  m_brdfShader->use();
  glBindVertexArray(m_vao);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  glBindVertexArray(0);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
}

GLuint Ibl::brdfLut() const {
  if (m_brdf)
    return m_brdf;
  if (!m_blackTexture) {
    glGenTextures(1, &m_blackTexture);
    glBindTexture(GL_TEXTURE_2D, m_blackTexture);
    const float black[4] = {0, 0, 0, 1};
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, 1, 1, 0, GL_RGBA, GL_FLOAT, black);
    setLinearClamped(GL_TEXTURE_2D, GL_LINEAR);
  }
  return m_blackTexture;
}

void Ibl::release() {
  for (GLuint *texture : {&m_environment, &m_irradiance, &m_prefiltered})
    if (*texture) {
      glDeleteTextures(1, texture);
      *texture = 0;
    }
}

void Ibl::shutdown() {
  release();
  for (GLuint *texture : {&m_brdf, &m_blackCube, &m_blackTexture})
    if (*texture) {
      glDeleteTextures(1, texture);
      *texture = 0;
    }
  if (m_fbo) {
    glDeleteFramebuffers(1, &m_fbo);
    m_fbo = 0;
  }
  if (m_vao) {
    glDeleteVertexArrays(1, &m_vao);
    m_vao = 0;
  }
  m_equirectShader.reset();
  m_irradianceShader.reset();
  m_prefilterShader.reset();
  m_brdfShader.reset();
  m_image.clear();
  m_failed = false;
}

void Ibl::update(const std::string &imagePath, bool gammaCorrect) {
  if (imagePath.empty()) {
    if (!m_image.empty() || hasMaps()) {
      release();
      m_image.clear();
      m_failed = false;
    }
    return;
  }
  if (imagePath == m_image && gammaCorrect == m_gamma && (hasMaps() || m_failed))
    return;
  m_image = imagePath;
  m_gamma = gammaCorrect;
  m_failed = !generate(imagePath, gammaCorrect);
  if (m_failed)
    release();
}

bool Ibl::generate(const std::string &imagePath, bool gammaCorrect) {
  // The panorama, decoded here and now rather than streamed in: the maps need all of it at once. A .hdr file is read as
  // floats, to keep its range; any other image stays 8-bit (smaller to decode and upload) and the shader turns it to
  // linear light.
  const auto decodeStart = Clock::now();
  stbi_set_flip_vertically_on_load_thread(1); // bottom row first, like the other textures
  const bool hdr = isHdrFile(imagePath);
  int width = 0, height = 0, channels = 0;
  float *floatPixels = nullptr;
  unsigned char *bytePixels = nullptr;
  if (hdr) {
    floatPixels = stbi_loadf(imagePath.c_str(), &width, &height, &channels, 3);
    if (floatPixels) {
      const size_t count = static_cast<size_t>(width) * height * 3;
      for (size_t i = 0; i < count; ++i) // a half float holds up to 65504; NaN would spread in the blurs
        floatPixels[i] = std::isfinite(floatPixels[i]) ? std::clamp(floatPixels[i], 0.0f, 60000.0f) : 0.0f;
    }
  } else {
    bytePixels = stbi_load(imagePath.c_str(), &width, &height, &channels, 3);
  }
  if (!floatPixels && !bytePixels) {
    LOG_W("[Ibl] Cannot read the sky image %s: %s", imagePath.c_str(), stbi_failure_reason());
    return false;
  }
  m_decodeMs = millisecondsSince(decodeStart);

  const auto gpuStart = Clock::now();
  ensureResources();
  PassState state;
  release();

  GLuint panorama = 0;
  glGenTextures(1, &panorama);
  glBindTexture(GL_TEXTURE_2D, panorama);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  if (hdr)
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, width, height, 0, GL_RGB, GL_FLOAT, floatPixels);
  else
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, bytePixels);
  stbi_image_free(hdr ? static_cast<void *>(floatPixels) : static_cast<void *>(bytePixels));
  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  setLinearClamped(GL_TEXTURE_2D, GL_LINEAR_MIPMAP_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT); // around, but not past the poles
  glGenerateMipmap(GL_TEXTURE_2D);

  glBindVertexArray(m_vao);
  glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);

  auto drawFaces = [&](GLuint cube, int level, int size, Shader &shader) {
    glViewport(0, 0, size, size);
    for (int face = 0; face < 6; ++face) {
      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, cube, level);
      shader.setInt("face", face);
      glDrawArrays(GL_TRIANGLES, 0, 3);
    }
  };

  // 1. The environment: the panorama as a cube, with its mip chain (the blurs below sample those levels)
  m_environment = makeCube(kEnvironmentSize, static_cast<int>(std::log2(kEnvironmentSize)) + 1);
  m_equirectShader->use();
  m_equirectShader->setTex("panorama", panorama, 0, GL_TEXTURE_2D);
  // 0: already linear light (.hdr), 1: 8-bit used as written (gamma-correct lighting off), 2: 8-bit to linear light
  m_equirectShader->setInt("sourceEncoding", hdr ? 0 : (gammaCorrect ? 2 : 1));
  drawFaces(m_environment, 0, kEnvironmentSize, *m_equirectShader);
  glBindTexture(GL_TEXTURE_CUBE_MAP, m_environment);
  glGenerateMipmap(GL_TEXTURE_CUBE_MAP);
  glDeleteTextures(1, &panorama);

  // 2. Diffuse irradiance
  m_irradiance = makeCube(kIrradianceSize, 1);
  m_irradianceShader->use();
  m_irradianceShader->setTex("environment", m_environment, 0, GL_TEXTURE_CUBE_MAP);
  m_irradianceShader->setFloat("environmentSize", static_cast<float>(kEnvironmentSize));
  drawFaces(m_irradiance, 0, kIrradianceSize, *m_irradianceShader);

  // 3. Pre-filtered specular: level i is roughness i / (levels - 1)
  m_prefiltered = makeCube(kPrefilterSize, kPrefilterLevels);
  m_prefilterShader->use();
  m_prefilterShader->setTex("environment", m_environment, 0, GL_TEXTURE_CUBE_MAP);
  m_prefilterShader->setFloat("environmentSize", static_cast<float>(kEnvironmentSize));
  for (int level = 0; level < kPrefilterLevels; ++level) {
    const int size = std::max(kPrefilterSize >> level, 1);
    m_prefilterShader->setFloat("targetSize", static_cast<float>(size));
    m_prefilterShader->setFloat("roughness", static_cast<float>(level) / static_cast<float>(kPrefilterLevels - 1));
    drawFaces(m_prefiltered, level, size, *m_prefilterShader);
  }

  // The environment was only the source of the other two
  glDeleteTextures(1, &m_environment);
  m_environment = 0;

  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
  glBindVertexArray(0);

  glFinish(); // so the time below is the passes', not just their submission
  m_gpuMs = millisecondsSince(gpuStart);
  ++m_generations;
  LOG_I("[Ibl] Sky lighting from %s: decoded %dx%d in %.1f ms, maps made in %.1f ms", imagePath.c_str(), width, height,
        m_decodeMs, m_gpuMs);
  return true;
}
