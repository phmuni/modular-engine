#pragma once
#include "foundation/ecs/systemManager.h"
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

// Forward declarations
class Material;
class Mesh;
class Shader;
using GLuint = unsigned int;

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
  Mesh &getMesh(uint32_t handle);
  void unloadMesh(uint32_t handle);

  GLuint loadTexture(const std::string &path);

  uint32_t createMaterial();
  Material &getMaterial(uint32_t handle);
  void unloadMaterial(uint32_t handle);

  uint32_t loadShader(const std::string &vertexPath, const std::string &fragmentPath);
  Shader &getShader(uint32_t handle);
  void unloadShader(uint32_t handle);
};
