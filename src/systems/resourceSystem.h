#pragma once
// Resource system for loading and managing meshes, materials, shaders, and textures.

#include "foundation/core/config.h"
#include "foundation/ecs/componentManager.h"
#include "foundation/ecs/entityManager.h"
#include "foundation/ecs/systemManager.h"
#include "rendering/resources/customShader.h"
#include "rendering/resources/mesh.h"
#include <filesystem>
#include <future>
#include <glm/glm.hpp>
#include <string>
#include <optional>
#include <string_view>
#include <vector>

class Material;
class Shader;
struct MtlMaterialData;
using GLuint = unsigned int;

enum class TextureSlot { Diffuse = 0, Normal = 1, Emission = 2, MetallicRoughness = 3, Occlusion = 4 };

struct ModelLoadResult {
  uint32_t meshHandle;
  std::vector<uint32_t> materialHandles;
};

// A custom surface shader file in use (see customShader.h): compiled once per file and variant, and shared by every
// material that names it
struct CustomShader {
  std::string path;     // the asset path materials name it by ("shaders/water.glsl")
  std::string diskPath; // where the file is
  std::filesystem::file_time_type modified;
  CustomShaderCode code;   // the version that last compiled
  bool ok = false;         // whether any version compiled; if not, materials draw with the standard shader
  uint32_t handle = 0;     // the shader running it (0 while it does not compile)
  uint32_t maskHandle = 0; // the alpha-masked variant, made when a cut-out material first needs it
  // The depth-pass variant (shadow shaders with the file injected): vertex() moves what the shadow draws and
  // surface() may discard. 0 when the file says `#pragma noshadow` or the variant did not compile, in which case the
  // part casts a shadow from its original geometry (or none, for noshadow).
  uint32_t shadowHandle = 0;
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
  std::vector<uint32_t> m_pinnedMaterials;
  bool m_hasBlendMaterials = false;
  std::unordered_map<uint32_t, uint32_t> m_maskVariants;

  // Custom surface shaders by asset path, and by the shader handle that runs them (elements never move)
  std::unordered_map<std::string, CustomShader> m_customShaders;
  std::unordered_map<uint32_t, const CustomShader *> m_customByHandle;
  uint32_t m_shaderRevision = 0;

  size_t m_textureUploadBudget = EngineConfig::TEXTURE_UPLOAD_BUDGET;
  uint32_t m_nextMesh = 0;
  uint32_t m_nextMaterial = 0;
  uint32_t m_nextShader = 1;

  ComponentManager &m_componentManager;

  CustomShader &loadCustomShader(const std::string &path);
  // Reads, parses and compiles the file again; false (logged) leaves what ran before as it was
  bool refreshCustomShader(CustomShader &custom);
  uint32_t customMaskHandle(CustomShader &custom);
  // Points the material at the shader its path and alpha mode call for
  void useShaderOf(Material &material);
  uint32_t standardHandleFor(const Material &material);

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
  // The asset path a custom shader is known by: relative to assets/ without a "../assets/" in front
  static std::string canonicalShaderPath(std::string_view path);
  // Variant of `shaderHandle` used by alpha-masked (cut-out) materials; without one, masks render opaque
  void setMaskVariant(uint32_t shaderHandle, uint32_t maskShaderHandle) { m_maskVariants[shaderHandle] = maskShaderHandle; }
  Shader &getShader(uint32_t handle);

  // Custom surface shaders
  // Gives the material (copied first if shared) the shader in the file `path` (relative to assets/; "" for the
  // standard shader, which also drops its parameters). A file that does not compile is logged with its name, and the
  // material keeps drawing with the standard shader (it still remembers the path, so fixing the file brings it back).
  void setMaterialShader(uint32_t &handle, std::string_view path);
  // A value for one of the material's shader uniforms: a number, vec2/3/4, or a texture's asset path. A name the shader
  // does not declare, or a value of the wrong kind, is logged and ignored.
  void setMaterialShaderParam(uint32_t &handle, std::string_view name, const ShaderValue &value);
  // The same on an entity's parts; `submesh` -1 is all of them, an index out of range a warning
  void setShader(Entity entity, int submesh, std::string_view path);
  void setShaderParam(Entity entity, int submesh, std::string_view name, const ShaderValue &value);
  // A part's shader file ("" for the standard shader or no such part), and one of its values (the file's default when
  // the part did not set it; nothing for a name the shader does not have)
  std::string getShader(Entity entity, int submesh);
  std::optional<ShaderValue> getShaderParam(Entity entity, int submesh, std::string_view name);
  // The custom shader a shader handle runs, or null for the engine's own shaders
  const CustomShader *customShaderForHandle(uint32_t shaderHandle) const {
    auto it = m_customByHandle.find(shaderHandle);
    return it == m_customByHandle.end() ? nullptr : it->second;
  }
  bool hasCustomShaders() const { return !m_customByHandle.empty(); }
  // The shader file in use under this path (null when no material asked for it yet)
  const CustomShader *findCustomShader(std::string_view path) const;
  // Compiles again the files that changed on disk since they were read (a fixed typo shows at once; an edit that does
  // not compile is logged and the last good version keeps running). Returns whether any shader changed.
  bool reloadChangedShaders();
  // Counts every change of which shader a material draws with, so the renderer groups its draws again
  uint32_t shaderRevision() const { return m_shaderRevision; }

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
  // Materials kept alive (and counted as used, so changing one copies it first) while something outside the scene
  // refers to them: the editor's Play snapshot
  void setPinnedMaterials(std::vector<uint32_t> handles) { m_pinnedMaterials = std::move(handles); }

  // Material helpers
  void setMaterialMetallic(uint32_t &handle, float metallic);
  void setMaterialRoughness(uint32_t &handle, float roughness);
  void setMaterialTexture(uint32_t &handle, TextureSlot slot, GLuint tex);
  void setMaterialEmission(uint32_t &handle, const glm::vec3 &color, float strength);
  void removeMaterialTexture(uint32_t &handle, TextureSlot slot);

  // Texture
  // Normal maps load as TextureKind::Normal, metallic-roughness and occlusion maps as TextureKind::Data; one that cannot be used (missing, or a grayscale height map) gives 0
  GLuint loadTexture(std::string_view path, TextureKind kind = TextureKind::Color);
  // Path a texture was loaded from ("solid_r_g_b" for solid colors), empty if unknown
  std::string getTexturePath(GLuint texture) const;

  // Entity API
  // Index of the entity's part with this name, or -1 (with a warning)
  int findSubmesh(Entity entity, std::string_view part);
  void setEmission(Entity entity, int submesh, const glm::vec3 &color, float strength);
  void setTexture(Entity entity, int submesh, TextureSlot slot, std::string_view path);
  void removeTexture(Entity entity, int submesh, TextureSlot slot);
  void setMetallic(Entity entity, int submesh, float metallic);
  void setRoughness(Entity entity, int submesh, float roughness);

  // Solid color helpers
  GLuint getOrCreateSolidColorTexture(const glm::vec3 &color);
  void setSolidColorToMaterial(uint32_t &handle, const glm::vec3 &color);
  void setSolidColor(Entity entity, int submesh, const glm::vec3 &color);
};