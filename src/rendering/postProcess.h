#pragma once
// HDR rendering and its post-processing. Lit geometry renders into a floating-point target, so lighting can go past
// 1.0. resolveToDisplay tone maps it into a display target that shares its depth, where particles draw in display
// space (their colors and additive blending were tuned there); finish adds the bloom of the HDR scene onto the screen.

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
  };

  // (Re)creates the targets: the scene, depth and bloom at width x height (the render size), and the final pass onto
  // the screen at displayWidth x displayHeight, which is only different when the render scale is not 1. Cheap when
  // neither size has changed.
  void resize(int width, int height, int displayWidth, int displayHeight);
  // Binds the HDR target and clears it to `clearColor` (linear); the lit passes draw into it
  void beginScene(const glm::vec3 &clearColor);
  // Tone maps the HDR scene into the display target and binds it, depth included, for the passes that follow
  void resolveToDisplay(Shader &tonemap, const Settings &settings);
  // The framebuffer the scene draws into (0 before resize): passes that bind their own targets come back to it
  GLuint sceneFramebuffer() const { return m_sceneFBO; }
  // Bloom of the HDR scene, added to the display image onto the screen (framebuffer 0)
  void finish(Shader &downsample, Shader &upsample, Shader &composite, const Settings &settings);
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
  GLuint m_sceneFBO = 0;
  GLuint m_sceneColor = 0;
  GLuint m_sceneDepth = 0;
  GLuint m_displayFBO = 0;
  GLuint m_displayColor = 0; // 8-bit, display values; shares m_sceneDepth
  GLuint m_bloomFBO = 0;
  std::vector<BloomLevel> m_bloom; // half size, quarter size...
  GLuint m_emptyVAO = 0;           // the fullscreen triangle has no vertex data, but core OpenGL needs a VAO

  void release();
  void drawFullscreen();

  // Fullscreen passes run without depth, culling or blending and filled even in wireframe mode
  struct SavedState {
    GLint polygonMode = GL_FILL;
    GLboolean depthTest, cullFace, blend;
  };
  SavedState beginFullscreenPasses();
  void endFullscreenPasses(const SavedState &state);
};
