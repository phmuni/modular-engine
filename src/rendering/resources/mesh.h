#pragma once
// Mesh class for loading OBJ/GLB files and managing vertex/index buffers.

#include "rendering/resources/alphaMode.h"
#include "rendering/resources/decodedImage.h"
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

struct Vertex {
  glm::vec3 position;
  glm::vec3 normal;
  glm::vec2 texCoord;

  friend bool operator==(const Vertex &a, const Vertex &b) {
    return a.position == b.position && a.normal == b.normal && a.texCoord == b.texCoord;
  }
};

struct MtlMaterialData {
  std::string name;
  std::string diffuseTexPath;
  std::string specularTexPath;
  std::string normalTexPath;
  std::string emissionTexPath;
  // Embedded images (glTF), as indices into MeshData::images; Mesh::upload turns them into the texture ids below
  int diffuseImage = -1;
  int specularImage = -1;
  int normalImage = -1;
  int emissionImage = -1;
  GLuint diffuseTextureId = 0;
  GLuint specularTextureId = 0;
  GLuint normalTextureId = 0;
  GLuint emissionTextureId = 0;
  glm::vec3 diffuseColor{0.8f};
  glm::vec3 specularColor{0.0f};
  glm::vec3 emissionColor{0.0f};
  float shininess = 16.0f;
  glm::vec4 baseColorFactor{1.0f}; // multiplies the diffuse texture (glTF); alpha multiplies its alpha
  AlphaMode alphaMode = AlphaMode::Opaque;
  float alphaCutoff = 0.5f;
  bool doubleSided = false;
  bool unlit = false; // KHR_materials_unlit: shown at its base color, without lighting or shadows
};

// A named part of a mesh (an OBJ object/group or a glTF node primitive), kept in file order. Parts are what
// materials are assigned to; in the index buffer they are laid out by material, so parts that share a material sit
// next to each other and can be drawn with one call.
struct Submesh {
  uint32_t indexStart;
  uint32_t indexCount;
  int materialIndex = -1;
  std::string name;
};

// A part as a loader produces it, before Mesh::layoutParts places its indices
struct PendingPart {
  std::string name;
  int materialIndex = -1;
  std::vector<uint32_t> indices;
};

// Everything a loader produces, without touching OpenGL, so it can be built on a worker thread
struct MeshData {
  std::vector<Vertex> vertices;
  std::vector<uint32_t> indices; // parts laid out by material
  std::vector<Submesh> submeshes;
  std::vector<MtlMaterialData> materials;
  std::vector<DecodedImage> images;                             // embedded images
  std::unordered_map<std::string, DecodedImage> externalImages; // texture files, by baseDir + relative path
  std::string baseDir;
  glm::vec3 boundsCenter{0.0f};
  float boundsRadius = 0.0f;
  bool loaded = false;
  bool fromCache = false; // read from the mesh cache instead of parsing the file
};

class Mesh {
private:
  GLuint m_VAO = 0;
  GLuint m_VBO = 0;
  GLuint m_EBO = 0;
  std::vector<Submesh> m_submeshes;
  std::vector<MtlMaterialData> m_mtlMaterials;
  std::string m_baseDir;

  // Bounding sphere in model space, used for frustum culling
  glm::vec3 m_boundsCenter{0.0f};
  float m_boundsRadius = 0.0f;

  std::vector<uint32_t> m_drawOrder; // submesh indices sorted by indexStart
  uint32_t m_indexCount = 0;

  void resetMesh();
  // Fills data.submeshes (in file order) and data.indices (grouped by material, in order of first use)
  static void layoutParts(std::vector<PendingPart> &parts, MeshData &data);
  static bool parseOBJ(std::string filename, MeshData &data);
  static bool parseGLB(std::string filename, MeshData &data);

public:
  Mesh() = default;
  // Parses and uploads in one go, on the calling (main) thread
  explicit Mesh(const std::string filename);

  // Reads the file and decodes its textures (embedded and external) without touching OpenGL; safe on any thread
  static MeshData parse(std::string filename);
  // Creates the GL buffers and textures; main thread only. Leaves data.externalImages for the texture cache.
  void upload(MeshData &data);
  ~Mesh();

  Mesh(const Mesh &) = delete;
  Mesh &operator=(const Mesh &) = delete;

  Mesh(Mesh &&other) noexcept
      : m_VAO(std::exchange(other.m_VAO, 0)), m_VBO(std::exchange(other.m_VBO, 0)),
        m_EBO(std::exchange(other.m_EBO, 0)), m_submeshes(std::move(other.m_submeshes)),
        m_mtlMaterials(std::move(other.m_mtlMaterials)), m_baseDir(std::move(other.m_baseDir)),
        m_boundsCenter(other.m_boundsCenter), m_boundsRadius(other.m_boundsRadius),
        m_drawOrder(std::move(other.m_drawOrder)), m_indexCount(other.m_indexCount) {}

  Mesh &operator=(Mesh &&other) noexcept {
    if (this != &other) {
      glDeleteVertexArrays(1, &m_VAO);
      glDeleteBuffers(1, &m_VBO);
      glDeleteBuffers(1, &m_EBO);
      m_VAO = std::exchange(other.m_VAO, 0);
      m_VBO = std::exchange(other.m_VBO, 0);
      m_EBO = std::exchange(other.m_EBO, 0);
      m_submeshes = std::move(other.m_submeshes);
      m_mtlMaterials = std::move(other.m_mtlMaterials);
      m_baseDir = std::move(other.m_baseDir);
      m_boundsCenter = other.m_boundsCenter;
      m_boundsRadius = other.m_boundsRadius;
      m_drawOrder = std::move(other.m_drawOrder);
      m_indexCount = other.m_indexCount;
    }
    return *this;
  }

  GLuint getVAO() const { return m_VAO; }
  bool isLoaded() const { return m_VAO != 0; }
  const std::vector<Submesh> &getSubmeshes() const { return m_submeshes; }
  // Submesh indices in index-buffer order: neighbors here are contiguous ranges that can be merged into one draw
  const std::vector<uint32_t> &getDrawOrder() const { return m_drawOrder; }
  // All parts together, for passes that ignore materials (shadows)
  uint32_t getIndexCount() const { return m_indexCount; }
  // Index of the first part with this name, or -1
  int findSubmesh(std::string_view name) const;
  const std::vector<MtlMaterialData> &getMtlMaterials() const { return m_mtlMaterials; }
  std::string_view getBaseDir() const { return m_baseDir; }
  glm::vec3 getBoundsCenter() const { return m_boundsCenter; }
  float getBoundsRadius() const { return m_boundsRadius; }
};