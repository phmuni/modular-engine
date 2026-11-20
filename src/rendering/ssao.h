#pragma once
// Screen-space ambient occlusion (SSAO): how much each visible point is shut in by the geometry around it, which
// darkens the ambient and indirect light (never a direct light or emission) in creases, under objects and where they
// meet the floor.
//
// The pass owns a single-sample depth texture of its own, at the render size: the scene's opaque geometry is drawn into
// it first (a depth prepass, see RenderSystem), so the AO needs nothing from the scene target and works whatever that
// target's sample count is. The AO is then computed from that depth (normals rebuilt from it) at half or full
// resolution, and a depth-aware blur brings it to the render size, where the lit shader reads it.

#include "rendering/resources/shader.h"
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <optional>
#include <string_view>

enum class SsaoQuality { Low, Medium, High };

// How many samples around each point a quality takes (the kernel the shader runs)
int ssaoSampleCount(SsaoQuality quality);
const char *ssaoQualityName(SsaoQuality quality);
// "low", "medium" or "high" (any case); nothing for another word
std::optional<SsaoQuality> ssaoQualityFromName(std::string_view name);

struct SsaoSettings {
  float radius = 0.5f;    // how far from a point (world units) geometry can shade it
  float intensity = 1.0f; // the AO's power: 0 turns it off, 1 is the plain occlusion, more is darker
  SsaoQuality quality = SsaoQuality::Medium;
  bool halfResolution = true; // computed at half the render size (then blurred up to it): about 4x cheaper

  static constexpr float kMinRadius = 0.05f;
  static constexpr float kMaxRadius = 10.0f;
  static constexpr float kMaxIntensity = 4.0f;
  // The same settings within their limits
  SsaoSettings clamped() const;
};

class Ssao {
public:
  // Binds the depth target (made or resized to width x height) and clears it: draw the opaque geometry into it, then
  // call compute
  void beginDepthPrepass(int width, int height);
  // The AO from the depth that was drawn, by the camera whose projection this is
  void compute(const glm::mat4 &projection, const SsaoSettings &settings);
  // Shows the AO alone (white where nothing occludes) in the bound target
  void drawDebug();
  // The AO at the render size, 1 for unoccluded: a single-channel texture (0 before the first compute)
  GLuint texture() const { return m_final; }
  void shutdown();

private:
  int m_width = 0;
  int m_height = 0;
  GLuint m_depth = 0;
  GLuint m_depthFBO = 0;
  GLuint m_raw = 0; // the AO before the blur, at half or full size
  GLuint m_rawFBO = 0;
  bool m_rawHalf = true;
  GLuint m_final = 0;
  GLuint m_finalFBO = 0;
  GLuint m_noise = 0;
  GLuint m_vao = 0;
  std::optional<Shader> m_aoShader, m_blurShader, m_debugShader;
  int m_kernelSamples = 0;
  glm::vec3 m_kernel[32];

  void ensureResources();
  void resizeTargets(int width, int height, bool half);
  void releaseTargets();
  void buildKernel(int samples);
};
