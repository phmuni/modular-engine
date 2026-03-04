// Material implementation: texture loading with stb_image and fallback generation.
#define STB_IMAGE_IMPLEMENTATION
#include "rendering/resources/material.h"
#include "foundation/core/config.h"
#include <iostream>
#include <stb_image/stb_image.h>
#include <vector>

static const std::vector<std::string> kSupportedExtensions = {".png", ".jpg", ".jpeg", ".bmp", ".tga"};

Material::Material() {
  std::array<unsigned char, 3> diffuseGray = {128, 128, 128};
  std::array<unsigned char, 3> specularGray = {64, 64, 64};
  std::array<unsigned char, 3> black = {0, 0, 0};

  m_diffuse = createFallbackTexture(diffuseGray);
  m_specular = createFallbackTexture(specularGray);
  m_normal = 0; // disabled until TBN is implemented
  m_emission = createFallbackTexture(black);
  m_shininess = 16.0f;
  m_shaderHandle = 0;
}

Material::Material(GLuint diffuse, GLuint specular, GLuint normal, GLuint emission, float shininess)
    : m_diffuse(diffuse), m_specular(specular), m_normal(normal), m_emission(emission), m_shininess(shininess) {}

GLuint Material::getDiffuse() const { return m_diffuse; }
GLuint Material::getSpecular() const { return m_specular; }
GLuint Material::getNormal() const { return m_normal; }
GLuint Material::getEmission() const { return m_emission; }
float Material::getShininess() const { return m_shininess; }
uint32_t Material::getShaderHandle() const { return m_shaderHandle; }

void Material::setDiffuseTexture(GLuint texture) { m_diffuse = texture; }
void Material::setSpecularTexture(GLuint texture) { m_specular = texture; }
void Material::setNormalTexture(GLuint texture) { m_normal = texture; }
void Material::setEmissionTexture(GLuint texture) { m_emission = texture; }

void Material::setDiffuse(const std::string &path) {
  GLuint tex = loadTexture(path);
  if (tex != 0)
    m_diffuse = tex;
}

void Material::setSpecular(const std::string &path) {
  GLuint tex = loadTexture(path);
  if (tex != 0)
    m_specular = tex;
}

void Material::setNormal(const std::string &path) {
  GLuint tex = loadTexture(path);
  if (tex != 0)
    m_normal = tex;
}

void Material::setEmission(const std::string &path) {
  GLuint tex = loadTexture(path);
  if (tex != 0)
    m_emission = tex;
}

void Material::setShininess(float shine) { m_shininess = shine; }
void Material::setShaderHandle(uint32_t handle) { m_shaderHandle = handle; }

glm::vec3 Material::getEmissionColor() const { return m_emissionColor; }
float Material::getEmissionStrength() const { return m_emissionStrength; }
void Material::setEmissionColor(const glm::vec3 &color) { m_emissionColor = color; }
void Material::setEmissionStrength(float strength) { m_emissionStrength = strength; }

GLuint Material::createFallbackTexture(const std::array<unsigned char, 3> &color) {
  GLuint textureID;
  glGenTextures(1, &textureID);
  glBindTexture(GL_TEXTURE_2D, textureID);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 1, 1, 0, GL_RGB, GL_UNSIGNED_BYTE, color.data());
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
  return textureID;
}

GLuint Material::loadTexture(const std::filesystem::path &path) {
  std::string normalized = PathUtils::normalize(path.string());
  std::string basePath = PathUtils::stripExtension(normalized);
  std::string foundPath;

  if (!basePath.empty()) {
    for (const auto &ext : kSupportedExtensions) {
      std::string candidate = basePath + ext;
      if (std::filesystem::exists(candidate)) {
        foundPath = candidate;
        break;
      }
    }
  }

  GLuint textureID = 0;
  bool hasMipmap = false;
  stbi_set_flip_vertically_on_load(true);

  if (!foundPath.empty()) {
    int width, height, channels;
    unsigned char *data = stbi_load(foundPath.c_str(), &width, &height, &channels, 0);
    if (data) {
      glGenTextures(1, &textureID);
      glBindTexture(GL_TEXTURE_2D, textureID);
      GLenum format = (channels == 4 ? GL_RGBA : GL_RGB);
      glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, data);
      glGenerateMipmap(GL_TEXTURE_2D);
      hasMipmap = true;
      stbi_image_free(data);
    } else {
      std::cerr << "[Material] Failed to load " << foundPath << ": " << stbi_failure_reason() << std::endl;
    }
  } else if (!basePath.empty()) {
    std::cerr << "[Material] File not found: " << normalized << std::endl;
  }

  // Magenta fallback on failure
  if (textureID == 0) {
    unsigned char magenta[3] = {255, 0, 255};
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_2D, textureID);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 1, 1, 0, GL_RGB, GL_UNSIGNED_BYTE, magenta);
    hasMipmap = false;
  }

  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
  if (hasMipmap) {
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  } else {
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  }

  return textureID;
}
