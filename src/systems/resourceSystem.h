#pragma once
// Resource system for loading and managing meshes, materials, shaders, and textures.

#include "foundation/core/config.h"
#include "foundation/ecs/componentManager.h"
#include "foundation/ecs/entityManager.h"
#include "foundation/ecs/systemManager.h"
#include "rendering/resources/mesh.h"
#include <future>
#include <glm/glm.hpp>
#include <string>
#include <string_view>
#include <vector>

class Material;
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
  // Meshes being read on worker threads (preloadModel), by normalized path
  std::unordered_map<std::string, std::future<MeshData>> m_pendingMeshes;

  // Materials loaded with each (mesh, shader); entities created from the same file share them until one of them
  // changes its copy. Kept alive by the cache.
  std::unordered_map<uint64_t, std::vector<uint32_t>> m_modelMaterials;

  // How many model slots use each material, recounted every frame by collectUnusedMaterials()
  std::unordered_map<uint32_t, uint32_t> m_materialUses;
  bool m_hasBlendMaterials = false;
  std::unordered_map<uint32_t, uint32_t> m_maskVariants;

  size_t m_textureUploadBudget = EngineConfig::TEXTURE_UPLOAD_BUDGET;
  uint32_t m_nextMesh = 0;
  uint32_t m_nextMaterial = 0;
  uint32_t m_nextShader = 1;

  ComponentManager &m_componentManager;

  // Uploads parsed data to the GPU and caches it; texture files decoded with it go into the texture cache
  uint32_t storeMesh(const std::string &path, MeshData &data, uint64_t startNs, bool readInBackground);

public:
  ResourceSystem(ComponentManager &cm);
  ~ResourceSystem();

  // Mesh / Model
  // Starts reading a model (geometry and textures) on a worker thread; a later loadMesh/loadModel of the same path
  // only uploads it, or waits for the read to finish
  void preloadModel(std::string_view path);
  // Whether a preloadModel read of this path is still going on
  bool isModelPending(std::string_view path) const;
  // Uploads background reads that finished and sends waiting texture data; call once per frame
  void update();
  // Texture bytes sent to the GPU per frame (EngineConfig::TEXTURE_UPLOAD_BUDGET by default)
  void setTextureUploadBudget(size_t bytes) { m_textureUploadBudget = bytes; }
  uint32_t loadMesh(std::string_view path);
  ModelLoadResult loadModel(std::string_view path, uint32_t shaderHandle);
  Mesh &getMesh(uint32_t handle);
  // Path a mesh was loaded from, empty if unknown
  std::string getMeshPath(uint32_t handle) const;
  // Materials a model file starts with for this shader (before any per-entity change), or null if not loaded
  const std::vector<uint32_t> *getModelMaterials(uint32_t meshHandle, uint32_t shaderHandle) const;

  // Shader
  uint32_t loadShader(std::string_view vertexPath, std::string_view fragmentPath, std::string_view defines = {});
  // Variant of `shaderHandle` used by alpha-masked (cut-out) materials; without one, masks render opaque
  void setMaskVariant(uint32_t shaderHandle, uint32_t maskShaderHandle) { m_maskVariants[shaderHandle] = maskShaderHandle; }
  Shader &getShader(uint32_t handle);

  // Material
  uint32_t createMaterial(uint32_t shaderHandle);
  Material &getMaterial(uint32_t handle);

  size_t getMaterialCount() const { return m_materials.size(); }
  // Whether any live material blends; lets the renderer skip per-part alpha checks when none does
  bool hasBlendMaterials() const { return m_hasBlendMaterials; }
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
  // Normal maps load as TextureKind::Normal; one that cannot be used (missing, or a grayscale height map) gives 0
  GLuint loadTexture(std::string_view path, TextureKind kind = TextureKind::Color);
  // Path a texture was loaded from ("solid_r_g_b" for solid colors), empty if unknown
  std::string getTexturePath(GLuint texture) const;

  // Entity API
  // Index of the entity's part with this name, or -1 (with a warning)
  int findSubmesh(Entity entity, std::string_view part);
  void setEmission(Entity entity, int submesh, const glm::vec3 &color, float strength);
  void setTexture(Entity entity, int submesh, TextureSlot slot, std::string_view path);
  void removeTexture(Entity entity, int submesh, TextureSlot slot);
  void setShininess(Entity entity, int submesh, float shininess);

  // Solid color helpers
  GLuint getOrCreateSolidColorTexture(const glm::vec3 &color);
  void setSolidColorToMaterial(uint32_t &handle, const glm::vec3 &color);
  void setSolidColor(Entity entity, int submesh, const glm::vec3 &color);
};