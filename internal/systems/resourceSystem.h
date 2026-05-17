#pragma once
// Resource system for loading and managing meshes, materials, shaders, and textures.

#include "components/model.h"
#include "foundation/ecs/systemManager.h"
#include <glm/glm.hpp>
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

  uint32_t m_nextMesh = 0;
  uint32_t m_nextMaterial = 0;
  uint32_t m_nextShader = 0;

public:
  ResourceSystem();
  ~ResourceSystem();

  uint32_t loadMesh(std::string_view path);
  ModelLoadResult loadModel(std::string_view path);
  Mesh &getMesh(uint32_t handle);
  void unloadMesh(uint32_t handle);

  GLuint loadTexture(std::string_view path);

  uint32_t createMaterial();
  Material &getMaterial(uint32_t handle);
  void unloadMaterial(uint32_t handle);

  uint32_t loadShader(std::string_view vertexPath, std::string_view fragmentPath);
  Shader &getShader(uint32_t handle);
  void unloadShader(uint32_t handle);

  void ensureOwnMaterial(uint32_t &handle, const std::vector<uint32_t> &allHandles);
  void setMaterialTexture(uint32_t &handle, const std::vector<uint32_t> &allHandles, TextureSlot slot, GLuint tex);
  void resetMaterialTexture(uint32_t &handle, const std::vector<uint32_t> &allHandles, TextureSlot slot);
  void setMaterialEmission(uint32_t &handle, const std::vector<uint32_t> &allHandles, const glm::vec3 &color,
                           float strength);
  void setMaterialShininess(uint32_t &handle, const std::vector<uint32_t> &allHandles, float shininess);

  void setEmission(Model &model, const glm::vec3 &color, float strength);
  void setTexture(Model &model, TextureSlot slot, GLuint tex);
  void resetTexture(Model &model, TextureSlot slot);
  void setShininess(Model &model, float shininess);
};