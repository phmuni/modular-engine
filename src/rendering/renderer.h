#pragma once
// Renderer class for managing OpenGL context, drawing meshes, and handling shadow/particle passes.

#include "foundation/core/config.h"
#include "rendering/graphicsSettings.h"
#include "rendering/postProcess.h"
#include "rendering/viewRect.h"
#include <SDL3/SDL.h>
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <string>

class Mesh;
struct Submesh;

// Per-instance data for instanced mesh drawing: vertex attributes 3-6 (model) and 7-9 (normal matrix)
struct InstanceData {
  glm::mat4 model;
  glm::mat3 normal;
};

// Interleaved vertex data for particle GPU upload
struct ParticleVertex {
  glm::vec3 position;
  glm::vec4 color;
  float size;
};

// Counters for the current frame, reset by beginFrame()
struct RenderStats {
  int drawCalls = 0;
  int meshesDrawn = 0; // mesh or submesh instances, shadow pass included
  int particles = 0;
};

class Renderer {
private:
  mutable RenderStats m_stats;
  mutable bool m_faceCulling = true;

  SDL_Window *m_window = nullptr;

  // Sun shadow cascades: one layer of a depth texture array per cascade
  GLuint m_depthMapFBO = 0;
  GLuint m_depthMap = 0;

  // Point light shadows: one depth cube map array, a cube per slot (layer = slot * 6 + face), created the first time
  // a light casts a shadow. Until then the shader reads m_emptyCubeMap, since a cube array sampler must never share
  // a texture unit with another kind of texture.
  GLuint m_pointShadowFBO = 0;
  GLuint m_pointShadowMaps = 0;
  mutable GLuint m_emptyCubeMap = 0;
  static GLuint createDepthCubeArray(int size, int cubes);

  // The window's framebuffer in pixels, which the editor, the game UI and the final pass draw at
  int m_screenWidth = 1280;
  int m_screenHeight = 720;
  // Where in the window the game is drawn (the editor's Game panel), or the whole window
  ViewRect m_view;
  // The 3D scene's own size: the screen's times the render scale
  float m_renderScale = 1.0f;
  int m_renderWidth = 1280;
  int m_renderHeight = 720;
  void updateTargets();
  AntialiasingSettings m_antialiasing;

  // The scene renders into an HDR target; finishScene brings it to the screen
  PostProcess m_postProcess;

  GLuint m_instanceVBO = 0;
  GLuint m_skyboxVAO = 0; // empty: the skybox triangle comes from gl_VertexID
  void bindInstanceAttributes(const Mesh &mesh) const;

  // Particle GPU resources
  GLuint m_particleVAO = 0;
  GLuint m_particleVBO = 0;
  GLsizeiptr m_particleCapacity = 0; // bytes allocated in m_particleVBO

  void initShadowMapping();
  void initParticleBuffers();

public:
  void init(SDL_Window *window);

  void beginFrame() const;
  // The scene's passes draw into the HDR target between these; finishScene adds bloom and tone maps onto the screen
  // Clears it to `clearColor` first, the sky color (linear)
  void beginScene(const glm::vec3 &clearColor);
  // Lit passes done: tone maps into the display target, where particles draw next
  void resolveScene(Shader &tonemap, const PostProcess::Settings &settings);
  void finishScene(const PostProcess::Shaders &shaders, const PostProcess::Settings &settings);
  // Binds the display target the scene was tone mapped into (after resolveScene), at the render size
  void bindDisplayTarget() { m_postProcess.bindDisplay(); }
  const RenderStats &getStats() const { return m_stats; }
  void endFrame() const;

  // Mesh drawing
  void drawMesh(const Mesh &mesh) const;
  void drawSubmesh(const Mesh &mesh, const Submesh &submesh) const;
  void drawIndexRange(const Mesh &mesh, uint32_t indexStart, uint32_t indexCount) const;

  // Instanced drawing: upload the instances, then draw them in one call
  void uploadInstances(const InstanceData *data, int count);
  void drawMeshInstanced(const Mesh &mesh, int count) const;
  void drawSubmeshInstanced(const Mesh &mesh, const Submesh &submesh, int count) const;
  void drawIndexRangeInstanced(const Mesh &mesh, uint32_t indexStart, uint32_t indexCount, int count) const;

  // Shadow pass: renders into one sun cascade (a layer of getDepthMap)
  void beginShadowPass(int cascade);
  void endShadowPass();
  // Renders into one face (0-5: +X, -X, +Y, -Y, +Z, -Z) of a point shadow slot; end with endShadowPass()
  void beginPointShadowFace(int slot, int face);
  // The cube map array of all the slots (GL_TEXTURE_CUBE_MAP_ARRAY), or an empty one (everything lit) before any use
  GLuint getPointShadowMap() const;

  // Skybox: one screen-covering triangle at the far plane with the bound skybox shader, behind what was drawn
  void drawSkybox() const;

  // Particle / transparent pass
  void beginTransparentPass();
  void endTransparentPass();
  void beginSoftTransparentPass();
  void endSoftTransparentPass();
  void beginParticlePass(bool additiveBlending);
  void uploadParticles(const ParticleVertex *data, int count);
  // `images` > 1 draws each particle that many times (gl_InstanceID picks the image, e.g. for lensing)
  void drawParticles(int count, int images = 1);
  void endParticlePass();

  // The sun's cascades, a GL_TEXTURE_2D_ARRAY of EngineConfig::SHADOW_CASCADES layers
  GLuint getDepthMap() const;

  // Saves the current back buffer as a BMP; call before endFrame()
  bool saveScreenshot(const std::string &path) const;

  void setWireframe(bool enabled);
  // Back-face culling, on by default; double-sided materials turn it off while they draw
  void setFaceCulling(bool enabled) const;
  // The framebuffer's size in pixels (the window's, with its pixel density)
  void setViewportSize(int width, int height);
  // Draws the game into a part of the window (the editor's Game panel) instead of all of it: the scene's targets, the
  // render scale and FSR follow that rectangle's size. Cheap when nothing changed. clearViewRect() goes back to the
  // whole window.
  void setViewRect(const ViewRect &rect);
  void clearViewRect() { setViewRect(ViewRect()); }
  // The part of the window the game is drawn in, in framebuffer pixels (the whole window when not active)
  ViewRect viewRect() const;
  // The fraction of the screen's pixels the 3D scene is drawn at before it is stretched onto the screen, within
  // kMinRenderScale to kMaxRenderScale: below 1 trades sharpness for speed, above 1 supersamples. The editor and the
  // game UI always draw at the screen's own size.
  static constexpr float kMinRenderScale = 0.5f;
  static constexpr float kMaxRenderScale = 2.0f;
  void setRenderScale(float scale);
  float getRenderScale() const { return m_renderScale; }
  int getRenderWidth() const { return m_renderWidth; }
  int getRenderHeight() const { return m_renderHeight; }
  // Antialiasing and upscaling (see graphicsSettings.h). The sample count is 0 (off), 2, 4 or 8, capped by the GPU:
  // changing it remakes the scene targets. msaa() is what is in use. Before init() the choice waits for the targets.
  void setMsaa(int samples);
  int getMsaa() const { return m_antialiasing.msaa; }
  void setFxaa(bool enabled) { m_antialiasing.fxaa = enabled; }
  bool getFxaa() const { return m_antialiasing.fxaa; }
  void setFsr(bool enabled) { m_antialiasing.fsr = enabled; }
  bool getFsr() const { return m_antialiasing.fsr; }
  void setFsrSharpness(float stops);
  float getFsrSharpness() const { return m_antialiasing.fsrSharpness; }
  // The filters' settings for PostProcess
  void applyAntialiasing(PostProcess::Settings &settings) const;
  // Alpha-to-coverage for the draws that follow (cut-out materials); only does anything while MSAA is on
  void setAlphaToCoverage(bool enabled) const;
  bool alphaToCoverageAvailable() const { return m_antialiasing.msaa >= 2; }
  // The resolved single-sample scene depth (DEPTH_COMPONENT24, nearest) and HDR color, valid after resolveScene()
  GLuint sceneDepthTexture() const { return m_postProcess.sceneDepthTexture(); }
  GLuint sceneColorTexture() const { return m_postProcess.sceneColorTexture(); }
  // The size of the picture the game is drawn at: the view's, or the window's
  int getViewWidth() const { return m_view.active ? m_view.width : m_screenWidth; }
  int getViewHeight() const { return m_view.active ? m_view.height : m_screenHeight; }
  int getScreenWidth() const { return m_screenWidth; }
  int getScreenHeight() const { return m_screenHeight; }
  void shutdown();
};
