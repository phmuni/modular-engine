
// Resource system implementation for managing meshes, materials, shaders, and textures with caching and reference
// counting.

#include "systems/resourceSystem.h"
#include "foundation/core/config.h"
#include "rendering/resources/material.h"
#include "rendering/resources/mesh.h"
#include "rendering/resources/shader.h"
#include <iostream>

ResourceSystem::ResourceSystem() { m_materials[m_nextMaterial++] = std::make_unique<Material>(); }

ResourceSystem::~ResourceSystem() = default;

uint32_t ResourceSystem::loadMesh(std::string_view path) {
  std::string normalized = PathUtils::normalizeSeparators(std::string(path));
  auto it = m_meshCache.find(normalized);
  if (it != m_meshCache.end())
    return it->second;
  uint32_t handle = m_nextMesh++;
  m_meshes[handle] = std::make_unique<Mesh>(normalized);
  m_meshCache[std::move(normalized)] = handle;
  return handle;
}

Mesh &ResourceSystem::getMesh(uint32_t handle) {
  auto it = m_meshes.find(handle);
  assert(it != m_meshes.end() && "Invalid mesh handle");
  return *it->second;
}

ModelLoadResult ResourceSystem::loadModel(std::string_view path) {
  ModelLoadResult result;
  result.meshHandle = loadMesh(path);

  Mesh &mesh = getMesh(result.meshHandle);
  const auto &mtlMats = mesh.getMtlMaterials();
  const auto &submeshes = mesh.getSubmeshes();
  std::string_view baseDir = mesh.getBaseDir();

  std::string texPath;
  texPath.reserve(baseDir.size() + 64);

  auto resolve = [&](const std::string &rel) -> std::string {
    std::string result;
    result.reserve(baseDir.size() + rel.size());
    result.append(baseDir);
    result.append(rel);
    return result;
  };
  for (const auto &sub : submeshes) {
    if (sub.materialIndex < 0 || sub.materialIndex >= static_cast<int>(mtlMats.size())) {
      result.materialHandles.push_back(0);
      continue;
    }
    const auto &mtl = mtlMats[sub.materialIndex];
    uint32_t matHandle = createMaterial();
    Material &mat = getMaterial(matHandle);

    if (!mtl.diffuseTexPath.empty()) {
      mat.setDiffuse(resolve(mtl.diffuseTexPath));
      mat.setDiffuseColor(mtl.diffuseColor);
    } else {
      mat.setDiffuseColor(mtl.diffuseColor);
      mat.setHasDiffuseTexture(false);
    }
    if (!mtl.specularTexPath.empty())
      mat.setSpecular(resolve(mtl.specularTexPath));
    if (!mtl.normalTexPath.empty())
      mat.setNormal(resolve(mtl.normalTexPath));
    if (!mtl.emissionTexPath.empty())
      mat.setEmission(resolve(mtl.emissionTexPath));

    mat.setShininess(mtl.shininess);
    mat.setEmissionColor(mtl.emissionColor);

    if (glm::length(mtl.emissionColor) > 0.0f)
      mat.setEmissionStrength(1.0f);

    result.materialHandles.push_back(matHandle);
  }

  return result;
}

void ResourceSystem::unloadMesh(uint32_t handle) {
  auto it = m_meshes.find(handle);
  if (it == m_meshes.end()) {
    std::cerr << "[ResourceSystem] Failed to unload mesh " << handle << "\n";
    return;
  }
  for (auto cit = m_meshCache.begin(); cit != m_meshCache.end(); ++cit) {
    if (cit->second == handle) {
      m_meshCache.erase(cit);
      break;
    }
  }
  m_meshes.erase(it);
}

GLuint ResourceSystem::loadTexture(std::string_view path) {
  std::string normalized = PathUtils::normalizeSeparators(std::string(path));
  std::string key = std::string(PathUtils::stripExtension(normalized));
  auto it = m_textures.find(key);
  if (it != m_textures.end())
    return it->second;
  GLuint tex = Material::loadTexture(normalized);
  if (tex != 0)
    m_textures[std::move(key)] = tex;
  return tex;
}

uint32_t ResourceSystem::createMaterial() {
  uint32_t handle = m_nextMaterial++;
  m_materials[handle] = std::make_unique<Material>();
  return handle;
}

Material &ResourceSystem::getMaterial(uint32_t handle) {
  auto it = m_materials.find(handle);
  assert(it != m_materials.end() && "Invalid material handle");
  return *it->second;
}

void ResourceSystem::unloadMaterial(uint32_t handle) {
  if (handle == 0) {
    std::cerr << "[ResourceSystem] Cannot unload default material\n";
    return;
  }
  if (m_materials.erase(handle) == 0)
    std::cerr << "[ResourceSystem] Failed to unload material " << handle << "\n";
}

uint32_t ResourceSystem::loadShader(std::string_view vertexPath, std::string_view fragmentPath) {
  uint32_t handle = m_nextShader++;
  m_shaders[handle] = std::make_unique<Shader>(vertexPath, fragmentPath);
  return handle;
}

Shader &ResourceSystem::getShader(uint32_t handle) {
  auto it = m_shaders.find(handle);
  assert(it != m_shaders.end() && "Invalid shader handle");
  return *it->second;
}

void ResourceSystem::unloadShader(uint32_t handle) {
  if (m_shaders.erase(handle) == 0)
    std::cerr << "[ResourceSystem] Failed to unload shader " << handle << "\n";
}

void ResourceSystem::ensureOwnMaterial(uint32_t &handle, const std::vector<uint32_t> &allHandles) {
  if (handle == 0) {
    handle = createMaterial();
  } else if (std::count(allHandles.begin(), allHandles.end(), handle) > 1) {
    uint32_t newHandle = createMaterial();
    getMaterial(newHandle) = getMaterial(handle);
    handle = newHandle;
  }
}

void ResourceSystem::setMaterialTexture(uint32_t &handle, const std::vector<uint32_t> &allHandles, TextureSlot slot,
                                        GLuint tex) {
  ensureOwnMaterial(handle, allHandles);
  Material &mat = getMaterial(handle);
  switch (slot) {
  case TextureSlot::Diffuse:
    mat.setDiffuseTexture(tex);
    break;
  case TextureSlot::Specular:
    mat.setSpecularTexture(tex);
    break;
  case TextureSlot::Normal:
    mat.setNormalTexture(tex);
    break;
  case TextureSlot::Emission:
    mat.setEmissionTexture(tex);
    if (mat.getEmissionStrength() == 0.0f) {
      mat.setEmissionColor(glm::vec3(1.0f));
      mat.setEmissionStrength(1.0f);
    }
    break;
  }
}

void ResourceSystem::resetMaterialTexture(uint32_t &handle, const std::vector<uint32_t> &allHandles, TextureSlot slot) {
  if (handle == 0)
    return;
  ensureOwnMaterial(handle, allHandles);
  Material &def = getMaterial(0);
  Material &mat = getMaterial(handle);
  switch (slot) {
  case TextureSlot::Diffuse:
    mat.setDiffuseTexture(def.getDiffuse());
    mat.setHasDiffuseTexture(false);
    break;
  case TextureSlot::Specular:
    mat.setSpecularTexture(def.getSpecular());
    break;
  case TextureSlot::Normal:
    mat.setNormalTexture(def.getNormal());
    break;
  case TextureSlot::Emission:
    mat.setEmissionTexture(def.getEmission());
    mat.setEmissionColor(def.getEmissionColor());
    mat.setEmissionStrength(def.getEmissionStrength());
    break;
  }
}

void ResourceSystem::setMaterialEmission(uint32_t &handle, const std::vector<uint32_t> &allHandles,
                                         const glm::vec3 &color, float strength) {
  ensureOwnMaterial(handle, allHandles);
  Material &mat = getMaterial(handle);
  mat.setEmissionColor(color);
  mat.setEmissionStrength(strength);
}

void ResourceSystem::setMaterialShininess(uint32_t &handle, const std::vector<uint32_t> &allHandles, float shininess) {
  ensureOwnMaterial(handle, allHandles);
  getMaterial(handle).setShininess(shininess);
}

void ResourceSystem::setEmission(Model &model, const glm::vec3 &color, float strength) {
  for (auto &h : model.materialHandles)
    setMaterialEmission(h, model.materialHandles, color, strength);
}

void ResourceSystem::setTexture(Model &model, TextureSlot slot, GLuint tex) {
  for (auto &h : model.materialHandles)
    setMaterialTexture(h, model.materialHandles, slot, tex);
}

void ResourceSystem::resetTexture(Model &model, TextureSlot slot) {
  for (auto &h : model.materialHandles)
    resetMaterialTexture(h, model.materialHandles, slot);
}

void ResourceSystem::setShininess(Model &model, float shininess) {
  for (auto &h : model.materialHandles)
    setMaterialShininess(h, model.materialHandles, shininess);
}

GLuint ResourceSystem::getOrCreateSolidColorTexture(const glm::vec3 &color) {
  auto clampToU8 = [](float v) -> unsigned int {
    return static_cast<unsigned int>(glm::clamp(v, 0.0f, 1.0f) * 255.0f);
  };
  unsigned int r = clampToU8(color.r);
  unsigned int g = clampToU8(color.g);
  unsigned int b = clampToU8(color.b);
  std::string key = "solid_" + std::to_string(r) + "_" + std::to_string(g) + "_" + std::to_string(b);
  auto it = m_textures.find(key);
  if (it != m_textures.end())
    return it->second;

  std::array<unsigned char, 3> rgb = {static_cast<unsigned char>(r), static_cast<unsigned char>(g),
                                      static_cast<unsigned char>(b)};
  GLuint tex = Material::createSolidColorTexture(rgb);
  if (tex != 0)
    m_textures[std::move(key)] = tex;
  return tex;
}

void ResourceSystem::applySolidColorToMaterial(uint32_t &handle, const std::vector<uint32_t> &allHandles,
                                               const glm::vec3 &color) {
  ensureOwnMaterial(handle, allHandles);
  Material &mat = getMaterial(handle);
  GLuint tex = getOrCreateSolidColorTexture(color);
  mat.setDiffuseTexture(tex);
  mat.setHasDiffuseTexture(true);
  mat.setDiffuseColor(color);
  mat.setEmissionColor(glm::vec3(0.0f));
  mat.setEmissionStrength(0.0f);
}

void ResourceSystem::applySolidColorToModel(Model &model, const glm::vec3 &color) {
  for (auto &h : model.materialHandles) {
    applySolidColorToMaterial(h, model.materialHandles, color);
  }
}