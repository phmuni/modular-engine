#pragma once
// Handle-based resource manager for meshes, materials, and shaders.

#include "components/modelComponent.h"
#include "foundation/ecs/systemManager.h"
#include <cstdint>
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <unordered_map>

// Forward declarations
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

  uint32_t m_nextMesh = 0;
  uint32_t m_nextMaterial = 0;
  uint32_t m_nextShader = 0;

public:
  ResourceSystem();
  ~ResourceSystem();

  uint32_t loadMesh(const std::string &path);
  ModelLoadResult loadModel(const std::string &path);
  Mesh &getMesh(uint32_t handle);
  void unloadMesh(uint32_t handle);

  GLuint loadTexture(const std::string &path);

  uint32_t createMaterial();
  Material &getMaterial(uint32_t handle);
  void unloadMaterial(uint32_t handle);

  uint32_t loadShader(const std::string &vertexPath, const std::string &fragmentPath);
  Shader &getShader(uint32_t handle);
  void unloadShader(uint32_t handle);

  // Material utilities (copy-on-write: creates own material if handle is 0 or shared)
  void ensureOwnMaterial(uint32_t &handle, const std::vector<uint32_t> &allHandles);
  void setMaterialTexture(uint32_t &handle, const std::vector<uint32_t> &allHandles, TextureSlot slot, GLuint tex);
  void resetMaterialTexture(uint32_t &handle, const std::vector<uint32_t> &allHandles, TextureSlot slot);
  void setMaterialEmission(uint32_t &handle, const std::vector<uint32_t> &allHandles, const glm::vec3 &color,
                           float strength);
  void setMaterialShininess(uint32_t &handle, const std::vector<uint32_t> &allHandles, float shininess);

  // Convenience: apply to all submeshes of a model
  void setEmission(ModelComponent &model, const glm::vec3 &color, float strength);
  void setTexture(ModelComponent &model, TextureSlot slot, GLuint tex);
  void resetTexture(ModelComponent &model, TextureSlot slot);
  void setShininess(ModelComponent &model, float shininess);
};
