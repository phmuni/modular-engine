#pragma once
// Diffuse light as order-2 spherical harmonics: nine RGB coefficients describe the cosine-weighted average of an
// environment around every direction (irradiance divided by PI, which the lit shader multiplies by the albedo), in
// place of an irradiance cube map. Smooth by nature, so it fits what diffuse light is, and small enough to pass as
// uniforms with each reflection probe.
//
// The coefficients are stored ready to use: the cosine lobe's convolution (A_l / PI) and the basis functions' constants
// are folded in, so the shader only evaluates the polynomials
//   [1, y, z, x, x*y, y*z, 3*z*z - 1, x*z, x*x - y*y]
// of the normal and sums coefficient * polynomial (assets/shaders/fragmentShader.frag, shIrradiance).

#include <array>
#include <glm/glm.hpp>

using Sh9 = std::array<glm::vec3, 9>;

namespace sh {

// The irradiance coefficients of a cube map level read back as six faces of `size` x `size` RGBA floats (faces in GL
// order, +X -X +Y -Y +Z -Z, rows from t = 0, as glGetTexImage returns them). Each texel counts by the solid angle it
// covers.
inline Sh9 fromCubeFaces(const float *const faces[6], int size) {
  // Radiance projected on the basis (constants included), summed over the sphere
  Sh9 radiance;
  radiance.fill(glm::vec3(0.0f));
  for (int face = 0; face < 6; ++face) {
    for (int y = 0; y < size; ++y) {
      for (int x = 0; x < size; ++x) {
        const float u = 2.0f * (static_cast<float>(x) + 0.5f) / static_cast<float>(size) - 1.0f;
        const float v = 2.0f * (static_cast<float>(y) + 0.5f) / static_cast<float>(size) - 1.0f;
        glm::vec3 d;
        switch (face) {
        case 0: d = glm::vec3(1.0f, -v, -u); break;
        case 1: d = glm::vec3(-1.0f, -v, u); break;
        case 2: d = glm::vec3(u, 1.0f, v); break;
        case 3: d = glm::vec3(u, -1.0f, -v); break;
        case 4: d = glm::vec3(u, -v, 1.0f); break;
        default: d = glm::vec3(-u, -v, -1.0f); break;
        }
        const float lengthSquared = u * u + v * v + 1.0f;
        const float solidAngle = (2.0f / static_cast<float>(size)) * (2.0f / static_cast<float>(size)) /
                                 (lengthSquared * glm::sqrt(lengthSquared));
        d = glm::normalize(d);
        const float *texel = faces[face] + (static_cast<size_t>(y) * size + x) * 4;
        glm::vec3 color(texel[0], texel[1], texel[2]);
        if (!(glm::all(glm::lessThan(glm::abs(color), glm::vec3(1e6f))))) // NaN or infinity: leave it out
          continue;
        const float basis[9] = {0.282095f,
                                0.488603f * d.y,
                                0.488603f * d.z,
                                0.488603f * d.x,
                                1.092548f * d.x * d.y,
                                1.092548f * d.y * d.z,
                                0.315392f * (3.0f * d.z * d.z - 1.0f),
                                1.092548f * d.x * d.z,
                                0.546274f * (d.x * d.x - d.y * d.y)};
        for (int i = 0; i < 9; ++i)
          radiance[i] += color * (basis[i] * solidAngle);
      }
    }
  }
  // Convolve with the cosine lobe (A_0 = PI, A_1 = 2 PI / 3, A_2 = PI / 4), divide by PI, and fold in the constants
  // the shader's polynomials leave out
  constexpr float kBandScale[9] = {1.0f, 2.0f / 3.0f, 2.0f / 3.0f, 2.0f / 3.0f, 0.25f, 0.25f, 0.25f, 0.25f, 0.25f};
  constexpr float kBasisConstant[9] = {0.282095f, 0.488603f, 0.488603f, 0.488603f, 1.092548f,
                                       1.092548f, 0.315392f, 1.092548f, 0.546274f};
  Sh9 result;
  for (int i = 0; i < 9; ++i)
    result[i] = radiance[i] * (kBandScale[i] * kBasisConstant[i]);
  return result;
}

// What the shader computes: the irradiance divided by PI in direction `n` (unit length), not below 0
inline glm::vec3 evaluate(const Sh9 &c, const glm::vec3 &n) {
  const glm::vec3 sum = c[0] + c[1] * n.y + c[2] * n.z + c[3] * n.x + c[4] * (n.x * n.y) + c[5] * (n.y * n.z) +
                        c[6] * (3.0f * n.z * n.z - 1.0f) + c[7] * (n.x * n.z) + c[8] * (n.x * n.x - n.y * n.y);
  return glm::max(sum, glm::vec3(0.0f));
}

} // namespace sh
