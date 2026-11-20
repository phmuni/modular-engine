#pragma once
// PBR material with diffuse, specular, normal, and emission textures.

#include "rendering/resources/alphaMode.h"
#include "rendering/resources/customShader.h"
#include "rendering/resources/decodedImage.h"
#include <filesystem>
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <array>
#include <map>
#include <string>
#include <cstdint>

// A value a material gives a custom shader's uniform; a texture value also holds the loaded texture
struct ShaderParam {
  ShaderValue value;
  GLuint texture = 0;
};

class Material {
public:
  Material();
  Material(GLuint diffuse, GLuint specular, GLuint normal, GLuint emission, float shininess = 16.0f);

  static GLuint createSolidColorTexture(const std::array<unsigned char, 3> &color);
  static GLuint loadTexture(const std::filesystem::path &path, TextureKind kind = TextureKind::Color);
  // loadTexture in two steps: decoding is thread-safe, uploading needs the GL context (main thread).
  // Decoding also builds mipmaps and converts to BC1/BC3, or BC5 for normal maps (cached; see TextureProcessing).
  // Uploading returns at once; large textures finish over the next frames (TextureUploader).
  // An empty color image uploads as a 1x1 magenta texture, so missing textures stand out; an empty normal map
  // uploads as nothing (0).
  static DecodedImage decodeTexture(const std::filesystem::path &path, TextureKind kind = TextureKind::Color);
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
  // A custom shader that asks for blending (`#pragma blend`) makes the material blend, whatever its file said
  AlphaMode getAlphaMode() const { return m_shaderBlend ? AlphaMode::Blend : m_alphaMode; }
  // The alpha mode the model file gave, without a shader's blending
  AlphaMode getBaseAlphaMode() const { return m_alphaMode; }
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

  // Custom surface shader (see customShader.h): the asset path of its file ("" for the standard shader; the shader
  // handle then runs it, or the standard shader when the file did not compile) and the values for its uniforms.
  // Set through ResourceSystem::setMaterialShader and setMaterialShaderParam, which keep the handle in step.
  const std::string &getShaderPath() const { return m_shaderPath; }
  void setShaderPath(std::string path) { m_shaderPath = std::move(path); }
  void setShaderBlend(bool blend) { m_shaderBlend = blend; }
  const std::map<std::string, ShaderParam> &getShaderParams() const { return m_shaderParams; }
  std::map<std::string, ShaderParam> &shaderParams() { return m_shaderParams; }

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
  std::string m_shaderPath;
  bool m_shaderBlend = false;
  std::map<std::string, ShaderParam> m_shaderParams;
};
