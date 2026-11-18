#pragma once
// Resource system for loading and managing meshes, materials, shaders, and textures.

#include "foundation/ecs/componentManager.h"
#include "foundation/ecs/entityManager.h"
#include "foundation/ecs/systemManager.h"
#include <glm/glm.hpp>
#include <string_view>
#include <vector>

class Material;
class Mesh;
class Shader;
struct MtlMaterialData;
using GLuint = unsigned int;

enum class TextureSlot { Diffuse = 0, Specular = 1, Normal = 2, Emission = 3 };

struct ModelLoadResult {
  uint32_t meshHandle;
  std::vector<uint32_t> materialHandles;
};

class ResourceSystem : public BaseSystem {
private:
  std::unordered_map<uint32_t, std::unique_ptr<Mesh>> m_meshes;
  std::unordered_map<uint32_t, std::unique_ptr<Material>> m_materials;
  std::unordered_map<uint32_t, std::unique_ptr<Shader>> m_shaders;

  std::unordered_map<std::string, GLuint> m_textures;
  std::unordered_map<std::string, uint32_t> m_meshCache;

  // Materials loaded with each (mesh, shader); entities created from the same file share them until one of them
  // changes its copy. Kept alive by the cache.
  std::unordered_map<uint64_t, std::vector<uint32_t>> m_modelMaterials;

  // How many model slots use each material, recounted every frame by collectUnusedMaterials()
  std::unordered_map<uint32_t, uint32_t> m_materialUses;

  uint32_t m_nextMesh = 0;
  uint32_t m_nextMaterial = 0;
  uint32_t m_nextShader = 1;

  ComponentManager &m_componentManager;

public:
  ResourceSystem(ComponentManager &cm);
  ~ResourceSystem();

  // Mesh / Model
  uint32_t loadMesh(std::string_view path);
  ModelLoadResult loadModel(std::string_view path, uint32_t shaderHandle);
  Mesh &getMesh(uint32_t handle);

  // Shader
  uint32_t loadShader(std::string_view vertexPath, std::string_view fragmentPath);
  Shader &getShader(uint32_t handle);

  // Material
  uint32_t createMaterial(uint32_t shaderHandle);
  Material &getMaterial(uint32_t handle);

  size_t getMaterialCount() const { return m_materials.size(); }
  size_t getTextureCount() const { return m_textures.size(); }

  // Copy-on-write: gives `handle` a material of its own if it is the default or used by another slot
  void ensureOwnMaterial(uint32_t &handle);
  // Frees materials no Model uses anymore; call once per frame, after entities were destroyed
  void collectUnusedMaterials();

  // Material helpers
  void setMaterialShininess(uint32_t &handle, float shininess);
  void setMaterialTexture(uint32_t &handle, TextureSlot slot, GLuint tex);
  void setMaterialEmission(uint32_t &handle, const glm::vec3 &color, float strength);
  void removeMaterialTexture(uint32_t &handle, TextureSlot slot);

  // Texture
  GLuint loadTexture(std::string_view path);

  // Entity API
  void setEmission(Entity entity, int submesh, const glm::vec3 &color, float strength);
  void setTexture(Entity entity, int submesh, TextureSlot slot, std::string_view path);
  void removeTexture(Entity entity, int submesh, TextureSlot slot);
  void setShininess(Entity entity, int submesh, float shininess);

  // Solid color helpers
  GLuint getOrCreateSolidColorTexture(const glm::vec3 &color);
  void setSolidColorToMaterial(uint32_t &handle, const glm::vec3 &color);
  void setSolidColor(Entity entity, int submesh, const glm::vec3 &color);
};