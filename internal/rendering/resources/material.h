#pragma once
// PBR material with diffuse, specular, normal, and emission textures.

#include <filesystem>
#include <glad/glad.h>
#include <glm/glm.hpp>

class Material {
public:
  Material();
  Material(GLuint diffuse, GLuint specular, GLuint normal, GLuint emission, float shininess = 16.0f);

  static GLuint createSolidColorTexture(const std::array<unsigned char, 3> &color);
  static GLuint loadTexture(const std::filesystem::path &path);
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

  void setDiffuseTexture(GLuint texture);
  void setSpecularTexture(GLuint texture);
  void setNormalTexture(GLuint texture);
  void setEmissionTexture(GLuint texture);

  void setDiffuse(std::string path);
  void setSpecular(std::string path);
  void setNormal(std::string path);
  void setEmission(std::string path);

  void setShininess(float shine);
  void setShaderHandle(uint32_t handle);
  void setDiffuseColor(const glm::vec3 &color);
  void setHasDiffuseTexture(bool value);
  void setEmissionColor(const glm::vec3 &color);
  void setEmissionStrength(float strength);

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
};
