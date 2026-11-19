
// Material class implementation with texture loading and solid-color texture generation.

#include "rendering/resources/material.h"
#include "foundation/core/config.h"
#include "foundation/core/logger.h"
#include "foundation/core/fileCache.h"
#include "rendering/resources/textureProcessing.h"
#include "rendering/resources/textureUploader.h"
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
  m_normal = 0; // no normal map
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

GLuint Material::loadTexture(const std::filesystem::path &path, TextureKind kind) {
  return uploadTexture(decodeTexture(path, kind));
}

namespace {
DecodedImage decodeFile(const std::string &foundPath, TextureKind kind) {
  DecodedImage image;

  // Every image is stored bottom row first (OpenGL's convention). Set per call and per thread: the flag is
  // otherwise global, and other loaders decode images too.
  stbi_set_flip_vertically_on_load_thread(1);

  // Grayscale color images are expanded: uploading 1 or 2 channels as RGB read past each row. Normal maps keep a
  // grayscale source as one channel, so TextureProcessing can tell a height map from a normal map.
  int width = 0, height = 0, sourceChannels = 0;
  stbi_info(foundPath.c_str(), &width, &height, &sourceChannels);
  int wanted = (sourceChannels == 2 || sourceChannels == 4) ? 4 : 3;
  if (kind == TextureKind::Normal)
    wanted = sourceChannels < 3 ? 1 : 3;

  int channels = 0;
  unsigned char *data = stbi_load(foundPath.c_str(), &width, &height, &channels, wanted);
  if (!data) {
    LOG_E("[Material] Failed to load %s: %s", foundPath.c_str(), stbi_failure_reason());
    return image;
  }

  image.width = width;
  image.height = height;
  image.channels = wanted;
  image.pixels.assign(data, data + static_cast<size_t>(width) * height * wanted);
  stbi_image_free(data);
  return image;
}
} // namespace

DecodedImage Material::decodeTexture(const std::filesystem::path &path, TextureKind kind) {
  std::string normalized = PathUtils::normalizeSeparators(path.string());
  std::string foundPath = findTextureFile(normalized);
  if (foundPath.empty()) {
    if (!PathUtils::stripExtension(normalized).empty())
      LOG_W("[Material] File not found: %s", normalized.c_str());
    DecodedImage missing;
    missing.kind = kind;
    return missing;
  }
  return TextureProcessing::load(FileCache::canonicalKey(foundPath), foundPath, kind,
                                 [&] { return decodeFile(foundPath, kind); });
}

GLuint Material::uploadTexture(DecodedImage image) {
  // A missing color texture shows as magenta so it stands out; a missing normal map is simply no normal map
  if (image.empty() && image.kind == TextureKind::Normal)
    return 0;
  return TextureUploader::upload(std::move(image));
}

bool Material::hasAlphaTexture(const std::filesystem::path &path) {
  std::string found = findTextureFile(PathUtils::normalizeSeparators(path.string()));
  int width = 0;
  int height = 0;
  int channels = 0;
  return !found.empty() && stbi_info(found.c_str(), &width, &height, &channels) && channels == 4;
}
