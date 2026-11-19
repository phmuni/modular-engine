#pragma once
// PBR material with diffuse, specular, normal, and emission textures.

#include "rendering/resources/alphaMode.h"
#include "rendering/resources/decodedImage.h"
#include <filesystem>
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <array>
#include <string>
#include <cstdint>

class Material {
public:
  Material();
  Material(GLuint diffuse, GLuint specular, GLuint normal, GLuint emission, float shininess = 16.0f);

  static GLuint createSolidColorTexture(const std::array<unsigned char, 3> &color);
  static GLuint loadTexture(const std::filesystem::path &path, bool compress = true);
  // loadTexture in two steps: decoding is thread-safe, uploading needs the GL context (main thread).
  // Decoding also builds mipmaps and, with `compress`, converts to BC1/BC3 (cached; see TextureProcessing).
  // Uploading returns at once; large textures finish over the next frames (TextureUploader).
  // An empty image uploads as a 1x1 magenta texture, so missing textures stand out.
  static DecodedImage decodeTexture(const std::filesystem::path &path, bool compress = true);
  static GLuint uploadTexture(DecodedImage image);
  static bool hasAlphaTexture(const std::filesystem::path &path);

  GLuint getDiffuse() const;
  GLuint getSpecular() const;
  GLuint getNormal() const;
  GLuint getEmission() const;
  float getShininess() const;
  uint32_t getShaderHandle() const;
  bool hasDiffuseTexture() const;
  glm::vec3 getDiffuseColor() const;
  glm::vec3 getEmissionColor() const;
  float getEmissionStrength() const;
  glm::vec4 getBaseColorFactor() const { return m_baseColorFactor; }
  AlphaMode getAlphaMode() const { return m_alphaMode; }
  float getAlphaCutoff() const { return m_alphaCutoff; }
  bool isDoubleSided() const { return m_doubleSided; }
  bool isUnlit() const { return m_unlit; }

  void setDiffuseTexture(GLuint texture);
  void setSpecularTexture(GLuint texture);
  void setNormalTexture(GLuint texture);
  void setEmissionTexture(GLuint texture);

  void setShininess(float shine);
  void setShaderHandle(uint32_t handle);
  void setDiffuseColor(const glm::vec3 &color);
  void setHasDiffuseTexture(bool value);
  void setEmissionColor(const glm::vec3 &color);
  void setEmissionStrength(float strength);
  void setBaseColorFactor(const glm::vec4 &factor) { m_baseColorFactor = factor; }
  void setAlphaMode(AlphaMode mode) { m_alphaMode = mode; }
  void setAlphaCutoff(float cutoff) { m_alphaCutoff = cutoff; }
  void setDoubleSided(bool doubleSided) { m_doubleSided = doubleSided; }
  void setUnlit(bool unlit) { m_unlit = unlit; }

private:
  GLuint m_diffuse = 0;
  GLuint m_specular = 0;
  GLuint m_normal = 0;
  GLuint m_emission = 0;
  float m_shininess = 16.0f;
  uint32_t m_shaderHandle = 0;
  bool m_hasDiffuseTexture = false;
  glm::vec3 m_diffuseColor{0.8f};
  glm::vec3 m_emissionColor{0.0f};
  float m_emissionStrength = 0.0f;
  glm::vec4 m_baseColorFactor{1.0f};
  AlphaMode m_alphaMode = AlphaMode::Opaque;
  float m_alphaCutoff = 0.5f;
  bool m_doubleSided = false;
  bool m_unlit = false;
};
