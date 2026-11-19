
// Resource system implementation for managing meshes, materials, shaders, and textures with caching and reference
// counting.

#include "systems/resourceSystem.h"
#include "components/model.h"
#include "foundation/core/config.h"
#include "foundation/core/logger.h"
#include "foundation/ecs/componentManager.h"
#include "rendering/resources/material.h"
#include "rendering/resources/mesh.h"
#include "rendering/resources/shader.h"
#include "rendering/resources/textureUploader.h"
#include <SDL3/SDL_timer.h>
#include <string_view>

ResourceSystem::ResourceSystem(ComponentManager &cm) : m_componentManager(cm) {
  m_materials[m_nextMaterial++] = std::make_unique<Material>();
}

ResourceSystem::~ResourceSystem() = default;

void ResourceSystem::preloadModel(std::string_view path) {
  std::string normalized = PathUtils::normalizeSeparators(std::string(path));
  if (m_meshCache.count(normalized) || m_pendingMeshes.count(normalized))
    return;
  m_pendingMeshes.emplace(normalized, std::async(std::launch::async, [normalized] { return Mesh::parse(normalized); }));
}

bool ResourceSystem::isModelPending(std::string_view path) const {
  return m_pendingMeshes.count(PathUtils::normalizeSeparators(std::string(path))) > 0;
}

void ResourceSystem::update() {
  TextureUploader::pump(m_textureUploadBudget);
  for (auto it = m_pendingMeshes.begin(); it != m_pendingMeshes.end();) {
    if (it->second.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
      ++it;
      continue;
    }
    MeshData data = it->second.get();
    std::string path = it->first;
    it = m_pendingMeshes.erase(it);
    storeMesh(path, data, SDL_GetTicksNS(), true);
  }
}

uint32_t ResourceSystem::loadMesh(std::string_view path) {
  std::string normalized = PathUtils::normalizeSeparators(std::string(path));
  auto it = m_meshCache.find(normalized);
  if (it != m_meshCache.end())
    return it->second;

  const uint64_t start = SDL_GetTicksNS();
  if (auto pending = m_pendingMeshes.find(normalized); pending != m_pendingMeshes.end()) {
    MeshData data = pending->second.get(); // waits only if the read has not finished yet
    m_pendingMeshes.erase(pending);
    return storeMesh(normalized, data, start, true);
  }

  MeshData data = Mesh::parse(normalized);
  return storeMesh(normalized, data, start, false);
}

uint32_t ResourceSystem::storeMesh(const std::string &path, MeshData &data, uint64_t startNs, bool readInBackground) {
  uint32_t handle = m_nextMesh++;
  auto mesh = std::make_unique<Mesh>();
  mesh->upload(data);

  for (auto &[texturePath, image] : data.externalImages)
    if (m_textures.find(texturePath) == m_textures.end())
      m_textures.emplace(texturePath, Material::uploadTexture(std::move(image)));

  if (mesh->isLoaded())
    LOG_I("[Resources] Loaded %s in %.1f ms%s%s", path.c_str(), static_cast<double>(SDL_GetTicksNS() - startNs) / 1e6,
          data.fromCache ? " from the mesh cache" : "",
          readInBackground ? " on the main thread (read in the background)" : "");

  m_meshes[handle] = std::move(mesh);
  m_meshCache[path] = handle;
  return handle;
}

Mesh &ResourceSystem::getMesh(uint32_t handle) {
  auto it = m_meshes.find(handle);
  assert(it != m_meshes.end() && "Invalid mesh handle");
  return *it->second;
}

ModelLoadResult ResourceSystem::loadModel(std::string_view path, uint32_t shaderHandle) {
  ModelLoadResult result;
  result.meshHandle = loadMesh(path);

  const uint64_t cacheKey = (static_cast<uint64_t>(result.meshHandle) << 32) | shaderHandle;
  if (auto cached = m_modelMaterials.find(cacheKey); cached != m_modelMaterials.end()) {
    result.materialHandles = cached->second;
    for (uint32_t handle : result.materialHandles)
      ++m_materialUses[handle];
    return result;
  }

  Mesh &mesh = getMesh(result.meshHandle);
  const auto &mtlMats = mesh.getMtlMaterials();
  const auto &submeshes = mesh.getSubmeshes();
  std::string_view baseDir = mesh.getBaseDir();

  auto resolve = [&](const std::string &rel) -> std::string {
    std::string result;
    result.reserve(baseDir.size() + rel.size());
    result.append(baseDir);
    result.append(rel);
    return result;
  };
  // Parts with the same source material share one handle, so they can be drawn together
  std::unordered_map<int, uint32_t> handleForSourceMaterial;
  for (const auto &sub : submeshes) {
    if (sub.materialIndex < 0 || sub.materialIndex >= static_cast<int>(mtlMats.size())) {
      result.materialHandles.push_back(0);
      continue;
    }
    if (auto existing = handleForSourceMaterial.find(sub.materialIndex); existing != handleForSourceMaterial.end()) {
      result.materialHandles.push_back(existing->second);
      continue;
    }
    const auto &mtl = mtlMats[sub.materialIndex];
    uint32_t matHandle = createMaterial(shaderHandle);
    handleForSourceMaterial[sub.materialIndex] = matHandle;
    Material &mat = getMaterial(matHandle);

    if (mtl.diffuseTextureId != 0) {
      mat.setDiffuseTexture(mtl.diffuseTextureId);
      mat.setHasDiffuseTexture(true);
      mat.setDiffuseColor(mtl.diffuseColor);
    } else if (!mtl.diffuseTexPath.empty()) {
      mat.setDiffuseTexture(loadTexture(resolve(mtl.diffuseTexPath)));
      mat.setHasDiffuseTexture(true);
      mat.setDiffuseColor(mtl.diffuseColor);
    } else {
      mat.setDiffuseColor(mtl.diffuseColor);
      mat.setHasDiffuseTexture(false);
    }

    if (mtl.specularTextureId != 0)
      mat.setSpecularTexture(mtl.specularTextureId);
    else if (!mtl.specularTexPath.empty())
      mat.setSpecularTexture(loadTexture(resolve(mtl.specularTexPath)));

    if (mtl.normalTextureId != 0)
      mat.setNormalTexture(mtl.normalTextureId);
    else if (!mtl.normalTexPath.empty())
      mat.setNormalTexture(loadTexture(resolve(mtl.normalTexPath), TextureKind::Normal));

    if (mtl.emissionTextureId != 0)
      mat.setEmissionTexture(mtl.emissionTextureId);
    else if (!mtl.emissionTexPath.empty())
      mat.setEmissionTexture(loadTexture(resolve(mtl.emissionTexPath)));

    mat.setShininess(mtl.shininess);
    mat.setEmissionColor(mtl.emissionColor);
    mat.setBaseColorFactor(mtl.baseColorFactor);
    mat.setAlphaMode(mtl.alphaMode);
    if (mtl.alphaMode == AlphaMode::Mask)
      if (auto variant = m_maskVariants.find(shaderHandle); variant != m_maskVariants.end())
        mat.setShaderHandle(variant->second);
    mat.setAlphaCutoff(mtl.alphaCutoff);
    mat.setDoubleSided(mtl.doubleSided);
    mat.setUnlit(mtl.unlit);
    m_hasBlendMaterials = m_hasBlendMaterials || mtl.alphaMode == AlphaMode::Blend;

    if (glm::length(mtl.emissionColor) > 0.0f)
      mat.setEmissionStrength(1.0f);

    result.materialHandles.push_back(matHandle);
  }

  // One use for the cache and one for the caller
  m_modelMaterials[cacheKey] = result.materialHandles;
  for (uint32_t handle : result.materialHandles)
    m_materialUses[handle] += 2;
  return result;
}

std::string ResourceSystem::getMeshPath(uint32_t handle) const {
  for (const auto &[path, cached] : m_meshCache)
    if (cached == handle)
      return path;
  return {};
}

const std::vector<uint32_t> *ResourceSystem::getModelMaterials(uint32_t meshHandle, uint32_t shaderHandle) const {
  auto it = m_modelMaterials.find((static_cast<uint64_t>(meshHandle) << 32) | shaderHandle);
  return it == m_modelMaterials.end() ? nullptr : &it->second;
}

std::string ResourceSystem::getTexturePath(GLuint texture) const {
  for (const auto &[path, cached] : m_textures)
    if (cached == texture)
      return path;
  return {};
}

// Cached by the full path: "rock.png" and "rock.jpg" are different textures
GLuint ResourceSystem::loadTexture(std::string_view path, TextureKind kind) {
  std::string normalized = PathUtils::normalizeSeparators(std::string(path));
  auto it = m_textures.find(normalized);
  if (it != m_textures.end())
    return it->second;
  GLuint tex = Material::loadTexture(normalized, kind);
  if (tex != 0)
    m_textures[std::move(normalized)] = tex;
  return tex;
}

uint32_t ResourceSystem::createMaterial(uint32_t shaderHandle) {
  uint32_t handle = m_nextMaterial++;
  m_materials[handle] = std::make_unique<Material>();
  m_materials[handle]->setShaderHandle(shaderHandle);
  return handle;
}

Material &ResourceSystem::getMaterial(uint32_t handle) {
  auto it = m_materials.find(handle);
  assert(it != m_materials.end() && "Invalid material handle");
  return *it->second;
}

uint32_t ResourceSystem::loadShader(std::string_view vertexPath, std::string_view fragmentPath,
                                    std::string_view defines) {
  auto shader = std::make_unique<Shader>(vertexPath, fragmentPath, defines);
  if (shader->getShaderID() == 0)
    return 0;

  uint32_t handle = m_nextShader++;
  m_shaders[handle] = std::move(shader);
  return handle;
}

Shader &ResourceSystem::getShader(uint32_t handle) {
  auto it = m_shaders.find(handle);
  assert(it != m_shaders.end() && "Invalid shader handle");
  return *it->second;
}

void ResourceSystem::ensureOwnMaterial(uint32_t &handle) {
  if (handle == 0) {
    handle = createMaterial(getMaterial(0).getShaderHandle());
    m_materialUses[handle] = 1;
    return;
  }

  auto uses = m_materialUses.find(handle);
  if (uses == m_materialUses.end() || uses->second <= 1)
    return;

  uint32_t newHandle = createMaterial(getMaterial(handle).getShaderHandle());
  getMaterial(newHandle) = getMaterial(handle);
  --uses->second;
  m_materialUses[newHandle] = 1;
  handle = newHandle;
}

void ResourceSystem::collectUnusedMaterials() {
  m_materialUses.clear();
  for (const auto &[key, handles] : m_modelMaterials)
    for (uint32_t handle : handles)
      ++m_materialUses[handle];
  m_componentManager.forEachComponent<Model>([&](Entity, Model &model) {
    for (uint32_t handle : model.materialHandles)
      ++m_materialUses[handle];
  });
  for (uint32_t handle : m_pinnedMaterials)
    ++m_materialUses[handle];

  // Handle 0 is the default material and always stays
  m_hasBlendMaterials = false;
  for (auto it = m_materials.begin(); it != m_materials.end();) {
    if (it->first != 0 && m_materialUses.find(it->first) == m_materialUses.end()) {
      it = m_materials.erase(it);
    } else {
      m_hasBlendMaterials = m_hasBlendMaterials || it->second->getAlphaMode() == AlphaMode::Blend;
      ++it;
    }
  }
}

void ResourceSystem::setMaterialTexture(uint32_t &handle, TextureSlot slot,
                                        GLuint tex) {
  ensureOwnMaterial(handle);
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

void ResourceSystem::removeMaterialTexture(uint32_t &handle, TextureSlot slot) {
  if (handle == 0)
    return;
  ensureOwnMaterial(handle);
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

void ResourceSystem::setMaterialEmission(uint32_t &handle, const glm::vec3 &color, float strength) {
  ensureOwnMaterial(handle);
  Material &mat = getMaterial(handle);
  mat.setEmissionColor(color);
  mat.setEmissionStrength(strength);
}

void ResourceSystem::setMaterialShininess(uint32_t &handle, float shininess) {
  ensureOwnMaterial(handle);
  getMaterial(handle).setShininess(shininess);
}

int ResourceSystem::findSubmesh(Entity entity, std::string_view part) {
  const auto *model = m_componentManager.tryGet<Model>(entity);
  int index = model ? getMesh(model->meshHandle).findSubmesh(part) : -1;
  if (index < 0)
    LOG_W("[Resources] Entity %d has no part named \"%.*s\"", entity, static_cast<int>(part.size()), part.data());
  return index;
}

void ResourceSystem::setEmission(Entity entity, int submesh, const glm::vec3 &color, float strength) {
  auto *modelPtr = m_componentManager.tryGet<Model>(entity);
  if (!modelPtr) {
    return;
  }

  auto &model = *modelPtr;
  if (submesh < 0) {
    for (auto &h : model.materialHandles)
      setMaterialEmission(h, color, strength);
  } else if (submesh < static_cast<int>(model.materialHandles.size())) {
    setMaterialEmission(model.materialHandles[submesh], color, strength);
  }
}

void ResourceSystem::setTexture(Entity entity, int submesh, TextureSlot slot, std::string_view path) {
  auto *modelPtr = m_componentManager.tryGet<Model>(entity);
  if (!modelPtr) {
    return;
  }

  auto &model = *modelPtr;
  GLuint tex = loadTexture(EngineConfig::resolvePath(path),
                           slot == TextureSlot::Normal ? TextureKind::Normal : TextureKind::Color);
  if (submesh < 0) {
    for (auto &h : model.materialHandles)
      setMaterialTexture(h, slot, tex);
  } else if (submesh < static_cast<int>(model.materialHandles.size())) {
    setMaterialTexture(model.materialHandles[submesh], slot, tex);
  }
}

void ResourceSystem::removeTexture(Entity entity, int submesh, TextureSlot slot) {
  auto *modelPtr = m_componentManager.tryGet<Model>(entity);
  if (!modelPtr) {
    return;
  }

  auto &model = *modelPtr;
  if (submesh < 0) {
    for (auto &h : model.materialHandles)
      removeMaterialTexture(h, slot);
  } else if (submesh < static_cast<int>(model.materialHandles.size())) {
    removeMaterialTexture(model.materialHandles[submesh], slot);
  }
}

void ResourceSystem::setShininess(Entity entity, int submesh, float shininess) {
  auto *modelPtr = m_componentManager.tryGet<Model>(entity);
  if (!modelPtr) {
    return;
  }

  auto &model = *modelPtr;
  if (submesh < 0) {
    for (auto &h : model.materialHandles)
      setMaterialShininess(h, shininess);
  } else if (submesh < static_cast<int>(model.materialHandles.size())) {
    setMaterialShininess(model.materialHandles[submesh], shininess);
  }
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

void ResourceSystem::setSolidColorToMaterial(uint32_t &handle, const glm::vec3 &color) {
  ensureOwnMaterial(handle);
  Material &mat = getMaterial(handle);
  GLuint tex = getOrCreateSolidColorTexture(color);
  // The color replaces the base color entirely, keeping only its alpha
  mat.setBaseColorFactor(glm::vec4(1.0f, 1.0f, 1.0f, mat.getBaseColorFactor().a));
  mat.setDiffuseTexture(tex);
  mat.setHasDiffuseTexture(true);
  mat.setDiffuseColor(color);
}

void ResourceSystem::setSolidColor(Entity entity, int submesh, const glm::vec3 &color) {
  auto *modelPtr = m_componentManager.tryGet<Model>(entity);
  if (!modelPtr) {
    return;
  }

  auto &model = *modelPtr;
  if (submesh < 0) {
    for (auto &h : model.materialHandles)
      setSolidColorToMaterial(h, color);
  } else if (submesh < static_cast<int>(model.materialHandles.size())) {
    setSolidColorToMaterial(model.materialHandles[submesh], color);
  }
}