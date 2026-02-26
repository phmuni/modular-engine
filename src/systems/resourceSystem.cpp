#include "systems/resourceSystem.h"
#include "foundation/core/config.h"
#include "rendering/resources/material.h"
#include "rendering/resources/mesh.h"
#include "rendering/resources/shader.h"
#include <iostream>

// Create default material (handle 0) with PBR fallbacks
ResourceSystem::ResourceSystem() { m_materials[m_nextMaterial++] = std::make_unique<Material>(); }

// Destructor definition
ResourceSystem::~ResourceSystem() = default;

// Mesh management
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

void ResourceSystem::unloadMesh(uint32_t handle) {
  if (m_meshes.erase(handle) == 0) {
    std::cerr << "[ResourceSystem] Failed to unload mesh " << handle << "\n";
  }
}

// Texture management (cached)
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

// Material management
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

// Shader management
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
