#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <glad/glad.h>
#include <string>

class Material {
private:
  GLuint m_diffuse = 0;
  GLuint m_specular = 0;
  GLuint m_normal = 0;
  GLuint m_emission = 0;
  float m_shininess = 16.0f;
  uint32_t m_shaderHandle = 0;

public:
  Material();
  Material(GLuint diffuse, GLuint specular, GLuint normal, GLuint emission, float shininess = 16.0f);

  static GLuint createFallbackTexture(const std::array<unsigned char, 3> &color);
  static GLuint loadTexture(const std::filesystem::path &path);

  GLuint getDiffuse() const;
  GLuint getSpecular() const;
  GLuint getNormal() const;
  GLuint getEmission() const;
  float getShininess() const;
  uint32_t getShaderHandle() const;

  void setDiffuseTexture(GLuint texture);
  void setSpecularTexture(GLuint texture);
  void setNormalTexture(GLuint texture);
  void setEmissionTexture(GLuint texture);

  void setDiffuse(const std::string &path);
  void setSpecular(const std::string &path);
  void setNormal(const std::string &path);
  void setEmission(const std::string &path);

  void setShininess(float shine);
  void setShaderHandle(uint32_t handle);
};
