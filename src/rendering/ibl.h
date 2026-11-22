#pragma once
// Image-based lighting (IBL): the scene's sky lights what is in it. From the Skybox's equirectangular panorama this
// makes, with render passes (fragment shaders into framebuffers; no compute):
//   - an environment cube map (RGBA16F, kEnvironmentSize per face, mipmapped), the panorama as a cube,
//   - the diffuse light as spherical harmonics (see sphericalHarmonics.h), read back from a small mip level of it: the
//     cosine-weighted average of the environment around each direction, which the lit shader multiplies by the albedo,
//   - a pre-filtered specular cube map (kPrefilterSize base, kPrefilterLevels mip levels): the environment blurred by
//     the GGX lobe of each roughness (mip level = roughness * (kPrefilterLevels - 1)), and
//   - the split-sum BRDF lookup table (RG16F, kBrdfSize squared; made once per run).
// They are made again only when the image changes (not every frame). The panorama's `rotation` is not baked in: the
// lit shader rotates its lookup vectors instead. Without an image the environment is one color; the lit shader then
// needs no cube maps (see RenderSystem), so this class holds none.
//
// The environment cube map is only an intermediate: it is deleted once the other two are made. The two steps that turn
// an environment into the lighting (irradianceSh, prefilterCube) are public because a reflection probe's capture goes
// through them as well (see reflectionProbes.h).

#include "rendering/resources/shader.h"
#include "rendering/sphericalHarmonics.h"
#include <glad/glad.h>
#include <optional>
#include <string>

class Ibl {
public:
  static constexpr int kEnvironmentSize = 512;
  static constexpr int kIrradianceSize = 32; // the mip level of the environment the spherical harmonics are read from
  static constexpr int kPrefilterSize = 128;
  static constexpr int kPrefilterLevels = 5;
  static constexpr int kBrdfSize = 256;

  // Makes the cube maps match the sky: `imagePath` is the panorama's file (empty: no image, the maps are freed), and
  // `gammaCorrect` how the scene is lit (it decides how an 8-bit image is read). Cheap when nothing changed.
  // Needs the GL context; call it outside a render pass (it restores the state it touches).
  void update(const std::string &imagePath, bool gammaCorrect);
  // Whether there are maps made from an image
  bool hasMaps() const { return m_prefiltered != 0; }
  // The sky's diffuse light (valid while hasMaps)
  const Sh9 &sh() const { return m_sh; }
  // The map to bind (a black cube stands in when there is none, so a sampler never reads the wrong kind of texture)
  GLuint prefiltered() const;
  // The BRDF lookup table (a black texture until ensureBrdf made it)
  GLuint brdfLut() const;
  // Makes the table if it was not (once per run, a fraction of a millisecond; needs the GL context)
  void ensureBrdf();
  void shutdown();

  // The spherical harmonics of the diffuse light of an environment cube map (RGBA16F, `size` per face, with mip levels
  // down to at least kIrradianceSize: reads one back, a few KB). Needs the GL context.
  Sh9 irradianceSh(GLuint environment, int size);
  // The pre-filtered specular cube map of an environment cube map with its mip levels: kPrefilterSize base and
  // kPrefilterLevels levels. The caller owns the texture (glDeleteTextures). Needs the GL context; restores the state it
  // touches.
  GLuint prefilterCube(GLuint environment, int size);

  // How many times the maps were made, and how long the last time took (decoding the image, and the GPU passes)
  int generations() const { return m_generations; }
  double lastDecodeMs() const { return m_decodeMs; }
  double lastGpuMs() const { return m_gpuMs; }

private:
  std::string m_image; // what the maps show
  bool m_gamma = true;
  bool m_failed = false; // the image could not be read: do not try again until it changes
  GLuint m_environment = 0;
  Sh9 m_sh{};
  GLuint m_prefiltered = 0;
  GLuint m_brdf = 0;
  GLuint m_fbo = 0;
  GLuint m_vao = 0;
  mutable GLuint m_blackCube = 0;
  mutable GLuint m_blackTexture = 0;
  int m_generations = 0;
  double m_decodeMs = 0.0;
  double m_gpuMs = 0.0;
  std::optional<Shader> m_equirectShader, m_prefilterShader, m_brdfShader;

  void ensureResources();
  void release();
  bool generate(const std::string &imagePath, bool gammaCorrect);
  GLuint makeCube(int size, int levels) const;
  void drawFaces(GLuint cube, int level, int size, Shader &shader);
};
