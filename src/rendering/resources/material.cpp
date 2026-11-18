
// Material class implementation with texture loading and solid-color texture generation.
#define STB_IMAGE_IMPLEMENTATION

#include "rendering/resources/material.h"
#include "foundation/core/config.h"
#include "foundation/core/logger.h"
#include <stb_image/stb_image.h>

static const std::vector<std::string> kSupportedExtensions = {".png", ".jpg", ".jpeg", ".bmp", ".tga"};

namespace {
struct DefaultTextures {
  GLuint diffuse, specular, emission;
};

// Created once and shared by every material; each Material used to create three new 1x1 textures
const DefaultTextures &defaultTextures() {
  static const DefaultTextures textures{Material::createSolidColorTexture({128, 128, 128}),
                                        Material::createSolidColorTexture({64, 64, 64}),
                                        Material::createSolidColorTexture({255, 255, 255})};
  return textures;
}
} // namespace

Material::Material() {
  const DefaultTextures &defaults = defaultTextures();
  m_diffuse = defaults.diffuse;
  m_specular = defaults.specular;
  m_normal = 0; // disabled until TBN is implemented
  m_emission = defaults.emission;
  m_shininess = 16.0f;
  m_shaderHandle = 0;
  m_hasDiffuseTexture = false;
  m_diffuseColor = glm::vec3(0.8f);
}

Material::Material(GLuint diffuse, GLuint specular, GLuint normal, GLuint emission, float shininess)
    : m_diffuse(diffuse), m_specular(specular), m_normal(normal), m_emission(emission), m_shininess(shininess) {}

GLuint Material::getDiffuse() const { return m_diffuse; }
GLuint Material::getSpecular() const { return m_specular; }
GLuint Material::getNormal() const { return m_normal; }
GLuint Material::getEmission() const { return m_emission; }
float Material::getShininess() const { return m_shininess; }
uint32_t Material::getShaderHandle() const { return m_shaderHandle; }
bool Material::hasDiffuseTexture() const { return m_hasDiffuseTexture; }
glm::vec3 Material::getDiffuseColor() const { return m_diffuseColor; }

void Material::setDiffuseTexture(GLuint texture) {
  m_diffuse = texture;
  m_hasDiffuseTexture = true;
}
void Material::setSpecularTexture(GLuint texture) { m_specular = texture; }
void Material::setNormalTexture(GLuint texture) { m_normal = texture; }
void Material::setEmissionTexture(GLuint texture) { m_emission = texture; }

void Material::setShininess(float shine) { m_shininess = shine; }
void Material::setShaderHandle(uint32_t handle) { m_shaderHandle = handle; }
void Material::setDiffuseColor(const glm::vec3 &color) { m_diffuseColor = color; }
void Material::setHasDiffuseTexture(bool value) { m_hasDiffuseTexture = value; }

glm::vec3 Material::getEmissionColor() const { return m_emissionColor; }
float Material::getEmissionStrength() const { return m_emissionStrength; }
void Material::setEmissionColor(const glm::vec3 &color) { m_emissionColor = color; }
void Material::setEmissionStrength(float strength) { m_emissionStrength = strength; }

GLuint Material::createSolidColorTexture(const std::array<unsigned char, 3> &color) {
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

namespace {
// The requested file if it exists; otherwise the same name with another supported extension
std::string findTextureFile(const std::string &normalized) {
  if (std::filesystem::is_regular_file(normalized))
    return normalized;

  std::string basePath = std::string(PathUtils::stripExtension(normalized));
  if (basePath.empty())
    return {};
  for (const auto &ext : kSupportedExtensions) {
    std::string candidate = basePath + ext;
    if (std::filesystem::exists(candidate)) {
      LOG_W("[Material] %s not found, using %s", normalized.c_str(), candidate.c_str());
      return candidate;
    }
  }
  return {};
}
} // namespace

GLuint Material::loadTexture(const std::filesystem::path &path) {
  std::string normalized = PathUtils::normalizeSeparators(path.string());
  std::string basePath = std::string(PathUtils::stripExtension(normalized));
  std::string foundPath = findTextureFile(normalized);

  GLuint textureID = 0;
  bool hasMipmap = false;
  // Every image is stored bottom row first (OpenGL's convention). Set per call and per thread: the flag is
  // otherwise global, and other loaders decode images too.
  stbi_set_flip_vertically_on_load_thread(1);

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
      LOG_E("[Material] Failed to load %s: %s", foundPath.c_str(), stbi_failure_reason());
    }
  } else if (!basePath.empty()) {
    LOG_W("[Material] File not found: %s", normalized.c_str());
  }

  // Fallback texture on failure
  if (textureID == 0) {
    unsigned char fallbackColor[3] = {255, 0, 255};
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_2D, textureID);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 1, 1, 0, GL_RGB, GL_UNSIGNED_BYTE, fallbackColor);
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

bool Material::hasAlphaTexture(const std::filesystem::path &path) {
  std::string found = findTextureFile(PathUtils::normalizeSeparators(path.string()));
  int width = 0;
  int height = 0;
  int channels = 0;
  return !found.empty() && stbi_info(found.c_str(), &width, &height, &channels) && channels == 4;
}
