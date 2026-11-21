#pragma once
// HDR rendering and its post-processing. Lit geometry renders into a floating-point target, so lighting can go past
// 1.0. resolveToDisplay tone maps it into a display target that shares its depth, where particles draw in display
// space (their colors and additive blending were tuned there); finish adds the bloom of the HDR scene onto the screen.
//
// Pipeline of a frame:
//   scene (multisampled when MSAA is on) -> resolve (glBlitFramebuffer: color into sceneColor, depth into sceneDepth)
//   -> tone map into the display target -> particles (display space) -> [FXAA] -> [FSR: EASU, RCAS] ->
//   composite with the bloom of the resolved HDR scene, onto the screen.
// The bloom is added after the upscale, at screen size: it is soft light that the sharpening should not touch.

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <vector>

class Shader;

class PostProcess {
public:
  struct Settings {
    bool bloom = true;
    float bloomStrength = 0.06f;
    float bloomThreshold = 1.0f;
    bool gammaCorrect = true; // tone map and encode for display; off for the old, non-linear look
    bool fxaa = false;
    bool fsr = false;           // EASU + RCAS instead of the linear stretch, when the render size is below the screen's
    float fsrSharpness = 0.2f;  // RCAS, in stops (0 sharpest)
  };
  // The fullscreen shaders finish() runs; the filters are only used when their setting is on
  struct Shaders {
    Shader *bloomDownsample = nullptr;
    Shader *bloomUpsample = nullptr;
    Shader *composite = nullptr;
    Shader *fxaa = nullptr;
    Shader *easu = nullptr;
    Shader *rcas = nullptr;
  };

  // (Re)creates the targets: the scene, depth and bloom at width x height (the render size), and the final pass onto
  // the screen at displayWidth x displayHeight, which is only different when the render scale is not 1. Cheap when
  // neither size nor the sample count has changed. `samples` is the MSAA sample count (0 or 1 for none), clamped to
  // what the GPU offers; samples() tells what was used.
  void resize(int width, int height, int displayWidth, int displayHeight, int samples);
  // Where the final pass draws on the screen (framebuffer 0): the lower-left corner of the displayWidth x
  // displayHeight rectangle, in pixels. 0, 0 for the whole window.
  void setOutputOrigin(int x, int y) {
    m_outputX = x;
    m_outputY = y;
  }
  // The most samples the GPU's multisampled color and depth buffers take (0 if none); needs a current context
  static int maxSamples();
  // The MSAA sample count in use (0: the scene renders single-sampled)
  int samples() const { return m_samples; }
  // Binds the HDR target and clears it to `clearColor` (linear); the lit passes draw into it
  void beginScene(const glm::vec3 &clearColor);
  // Resolves MSAA (when on), then tone maps the HDR scene into the display target and binds it, depth included, for
  // the passes that follow
  void resolveToDisplay(Shader &tonemap, const Settings &settings);
  // The framebuffer the scene draws into (0 before resize): passes that bind their own targets come back to it
  // (the multisampled one while MSAA is on)
  GLuint sceneFramebuffer() const { return m_renderFBO ? m_renderFBO : m_sceneFBO; }
  // The single-sample, resolved HDR scene color (RGBA16F) and depth (DEPTH_COMPONENT24, GL_NEAREST, no compare mode),
  // valid from resolveToDisplay() on, at the render size. Passes that sample the scene or its depth (SSAO...) read
  // these, never the multisampled buffers. 0 before resize().
  GLuint sceneColorTexture() const { return m_sceneColor; }
  GLuint sceneDepthTexture() const { return m_sceneDepth; }
  // Bloom of the HDR scene and the display image's filters (FXAA, FSR), composed onto the screen (framebuffer 0)
  void finish(const Shaders &shaders, const Settings &settings);
  // Binds the display target (render size), where the tone mapped image is: for passes that draw over it
  void bindDisplay() {
    glBindFramebuffer(GL_FRAMEBUFFER, m_displayFBO);
    glViewport(0, 0, m_width, m_height);
  }
  void shutdown();

private:
  struct BloomLevel {
    GLuint texture = 0;
    int width = 0;
    int height = 0;
  };

  int m_width = 0;
  int m_height = 0;
  int m_displayWidth = 0; // the screen's pixels, which the final pass fills
  int m_displayHeight = 0;
  int m_outputX = 0; // where the final pass draws on the screen, from its lower-left corner
  int m_outputY = 0;
  int m_samples = 0;
  // Single-sample scene: the target itself without MSAA, the resolve target with it
  GLuint m_sceneFBO = 0;
  GLuint m_sceneColor = 0;
  GLuint m_sceneDepth = 0; // a texture, so later passes can sample it
  // With MSAA: the multisampled buffers the lit passes draw into
  GLuint m_renderFBO = 0;
  GLuint m_renderColor = 0;
  GLuint m_renderDepth = 0;
  GLuint m_displayFBO = 0;
  GLuint m_displayColor = 0; // 8-bit, display values; shares m_sceneDepth
  // Filters' targets, made when first needed: FXAA at the render size, EASU and RCAS at the screen's
  struct FilterTarget {
    GLuint fbo = 0;
    GLuint texture = 0;
    int width = 0;
    int height = 0;
  };
  FilterTarget m_fxaaTarget, m_upscaleTarget, m_sharpenTarget;
  GLuint m_bloomFBO = 0;
  std::vector<BloomLevel> m_bloom; // half size, quarter size...
  GLuint m_emptyVAO = 0;           // the fullscreen triangle has no vertex data, but core OpenGL needs a VAO

  void release();
  void releaseScreenTargets();
  void ensureFilterTarget(FilterTarget &target, int width, int height);
  static void releaseFilterTarget(FilterTarget &target);
  bool createMultisampleTargets(int samples);
  void resolveMultisample();
  void drawFullscreen();

  // Fullscreen passes run without depth, culling or blending and filled even in wireframe mode
  struct SavedState {
    GLint polygonMode = GL_FILL;
    GLboolean depthTest, cullFace, blend;
  };
  SavedState beginFullscreenPasses();
  void endFullscreenPasses(const SavedState &state);
};
