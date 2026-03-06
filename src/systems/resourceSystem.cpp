// Resource system: handle-based loading and caching of meshes, materials, and shaders.
#include "systems/resourceSystem.h"
#include "foundation/core/config.h"
#include "rendering/resources/material.h"
#include "rendering/resources/mesh.h"
#include "rendering/resources/shader.h"
#include <iostream>

ResourceSystem::ResourceSystem() { m_materials[m_nextMaterial++] = std::make_unique<Material>(); }

ResourceSystem::~ResourceSystem() = default;

uint32_t ResourceSystem::loadMesh(const std::string &path) {
  std::string normalized = PathUtils::normalize(path);
  uint32_t handle = m_nextMesh++;
  m_meshes[handle] = std::make_unique<Mesh>(normalized);
  return handle;
}

Mesh &ResourceSystem::getMesh(uint32_t handle) {
  auto it = m_meshes.find(handle);
  if (it == m_meshes.end()) {
    std::cerr << "[ResourceSystem] Mesh handle " << handle << " not found\n";
    static Mesh fallback;
    return fallback;
  }
  return *it->second;
}

ModelLoadResult ResourceSystem::loadModel(const std::string &path) {
  ModelLoadResult result;
  result.meshHandle = loadMesh(path);

  Mesh &mesh = getMesh(result.meshHandle);
  const auto &mtlMats = mesh.getMtlMaterials();
  const auto &submeshes = mesh.getSubmeshes();
  const std::string &baseDir = mesh.getBaseDir();

  for (const auto &sub : submeshes) {
    if (sub.materialIndex >= 0 && sub.materialIndex < static_cast<int>(mtlMats.size())) {
      const auto &mtl = mtlMats[sub.materialIndex];
      uint32_t matHandle = createMaterial();
      Material &mat = getMaterial(matHandle);

      if (!mtl.diffuseTexPath.empty())
        mat.setDiffuse(baseDir + mtl.diffuseTexPath);
      if (!mtl.specularTexPath.empty())
        mat.setSpecular(baseDir + mtl.specularTexPath);
      if (!mtl.normalTexPath.empty())
        mat.setNormal(baseDir + mtl.normalTexPath);
      if (!mtl.emissionTexPath.empty())
        mat.setEmission(baseDir + mtl.emissionTexPath);

      mat.setShininess(mtl.shininess);
      mat.setEmissionColor(mtl.emissionColor);

      if (glm::length(mtl.emissionColor) > 0.0f)
        mat.setEmissionStrength(1.0f);

      result.materialHandles.push_back(matHandle);
    } else {
      result.materialHandles.push_back(0);
    }
  }

  return result;
}

void ResourceSystem::unloadMesh(uint32_t handle) {
  if (m_meshes.erase(handle) == 0) {
    std::cerr << "[ResourceSystem] Failed to unload mesh " << handle << "\n";
  }
}

GLuint ResourceSystem::loadTexture(const std::string &path) {
  std::string normalized = PathUtils::normalize(path);
  std::string key = PathUtils::stripExtension(normalized);
  auto it = m_textures.find(key);
  if (it != m_textures.end())
    return it->second;

  GLuint texture = Material::loadTexture(normalized);
  if (texture != 0)
    m_textures[key] = texture;

  return texture;
}

uint32_t ResourceSystem::createMaterial() {
  uint32_t handle = m_nextMaterial++;
  m_materials[handle] = std::make_unique<Material>();
  return handle;
}

Material &ResourceSystem::getMaterial(uint32_t handle) {
  auto it = m_materials.find(handle);
  if (it == m_materials.end()) {
    std::cerr << "[ResourceSystem] Material handle " << handle << " not found, returning default\n";
    return *m_materials[0];
  }
  return *it->second;
}

void ResourceSystem::unloadMaterial(uint32_t handle) {
  if (handle == 0) {
    std::cerr << "[ResourceSystem] Cannot unload default material (handle 0)\n";
    return;
  }
  if (m_materials.erase(handle) == 0) {
    std::cerr << "[ResourceSystem] Failed to unload material " << handle << "\n";
  }
}

uint32_t ResourceSystem::loadShader(const std::string &vertexPath, const std::string &fragmentPath) {
  uint32_t handle = m_nextShader++;
  m_shaders[handle] = std::make_unique<Shader>(vertexPath.c_str(), fragmentPath.c_str());
  return handle;
}

Shader &ResourceSystem::getShader(uint32_t handle) {
  auto it = m_shaders.find(handle);
  if (it == m_shaders.end()) {
    std::cerr << "[ResourceSystem] Shader handle " << handle << " not found\n";
    static Shader fallback;
    return fallback;
  }
  return *it->second;
}

void ResourceSystem::unloadShader(uint32_t handle) {
  if (m_shaders.erase(handle) == 0) {
    std::cerr << "[ResourceSystem] Failed to unload shader " << handle << "\n";
  }
}

// --- Material utilities ---

void ResourceSystem::ensureOwnMaterial(uint32_t &handle, const std::vector<uint32_t> &allHandles) {
  if (handle == 0) {
    handle = createMaterial();
  } else {
    int count = 0;
    for (auto h : allHandles)
      if (h == handle)
        count++;
    if (count > 1) {
      uint32_t newHandle = createMaterial();
      getMaterial(newHandle) = getMaterial(handle);
      handle = newHandle;
    }
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
    break;
  case TextureSlot::Specular:
    mat.setSpecularTexture(def.getSpecular());
    break;
  case TextureSlot::Normal:
    mat.setNormalTexture(def.getNormal());
    break;
  case TextureSlot::Emission:
    mat.setEmissionTexture(def.getEmission());
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

void ResourceSystem::setEmission(ModelComponent &model, const glm::vec3 &color, float strength) {
  for (auto &h : model.materialHandles)
    setMaterialEmission(h, model.materialHandles, color, strength);
}

void ResourceSystem::setTexture(ModelComponent &model, TextureSlot slot, GLuint tex) {
  for (auto &h : model.materialHandles)
    setMaterialTexture(h, model.materialHandles, slot, tex);
}

void ResourceSystem::resetTexture(ModelComponent &model, TextureSlot slot) {
  for (auto &h : model.materialHandles)
    resetMaterialTexture(h, model.materialHandles, slot);
}

void ResourceSystem::setShininess(ModelComponent &model, float shininess) {
  for (auto &h : model.materialHandles)
    setMaterialShininess(h, model.materialHandles, shininess);
}
