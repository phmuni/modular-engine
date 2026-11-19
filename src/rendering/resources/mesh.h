#pragma once
// Mesh class for loading OBJ/GLB files and managing vertex/index buffers.

#include "rendering/resources/alphaMode.h"
#include "rendering/resources/decodedImage.h"
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

struct Vertex {
  glm::vec3 position;
  glm::vec3 normal;
  glm::vec2 texCoord;
  // Direction of +u on the surface (xyz) and the bitangent's sign (w: bitangent = cross(normal, tangent) * w), for
  // normal maps; all zero until a loader reads or generates it
  glm::vec4 tangent{0.0f};

  friend bool operator==(const Vertex &a, const Vertex &b) {
    return a.position == b.position && a.normal == b.normal && a.texCoord == b.texCoord && a.tangent == b.tangent;
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

// Skeletal animation (glTF skins and animations). A vertex follows up to four joints, by weight; the joints are nodes
// of the model's node tree, which the clips animate. Parts of an animated model that have no skin follow their own
// node as a single joint, so a sword parented to a hand moves with it.
struct VertexSkin {
  glm::vec4 joints{0.0f};  // joint indices
  glm::vec4 weights{0.0f}; // summing to 1
};

struct SkeletonNode {
  std::string name;
  int parent = -1; // index in Skeleton::nodes
  // Rest pose, relative to the parent
  glm::vec3 translation{0.0f};
  glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
  glm::vec3 scale{1.0f};
};

struct Skeleton {
  std::vector<SkeletonNode> nodes;
  std::vector<int> order;                // node indices, parents before children
  std::vector<int> jointNodes;           // joint -> node
  std::vector<glm::mat4> inverseBind;    // per joint: from the model's space into the joint's, at rest
};

struct AnimationChannel {
  enum class Path { Translation, Rotation, Scale };
  enum class Interpolation { Linear, Step, CubicSpline };
  int node = -1;
  Path path = Path::Translation;
  Interpolation interpolation = Interpolation::Linear;
  std::vector<float> times;
  std::vector<glm::vec4> values; // xyz (rotation: xyzw); CubicSpline: in-tangent, value, out-tangent per key
};

struct AnimationClip {
  std::string name;
  float duration = 0.0f; // seconds
  std::vector<AnimationChannel> channels;
};

// What a skinned or animated mesh keeps for the animation system
struct MeshAnimation {
  Skeleton skeleton;
  std::vector<AnimationClip> clips;
  std::vector<glm::mat4> restJointMatrices; // the rest pose, for entities without an Animator
  int findClip(std::string_view name) const {
    for (size_t i = 0; i < clips.size(); ++i)
      if (clips[i].name == name)
        return static_cast<int>(i);
    return -1;
  }
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
  glm::vec3 boundsMin{0.0f}, boundsMax{0.0f};
  // Skinned or animated models: one entry per vertex, and the skeleton and clips (empty for static models)
  std::vector<VertexSkin> skin;
  std::shared_ptr<MeshAnimation> animation;
  // Welded positions and triangles for queries, built by parse (on the loading thread); Mesh keeps them
  std::vector<glm::vec3> collisionVertices;
  std::vector<uint32_t> collisionIndices;
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
  // Bounding box in model space
  glm::vec3 m_boundsMin{0.0f}, m_boundsMax{0.0f};
  // The triangles in memory, for picking and collision queries: one vertex per distinct position (the GPU copy
  // repeats positions for each normal and UV), three indices per triangle
  std::vector<glm::vec3> m_collisionVertices;
  std::vector<uint32_t> m_collisionIndices;
  // Skinned or animated meshes: joint indices and weights (vertex attributes 11 and 12), and the skeleton and clips
  GLuint m_skinVBO = 0;
  std::shared_ptr<const MeshAnimation> m_animation;
  static void buildCollisionGeometry(MeshData &data, const std::vector<glm::vec3> &positions);

  std::vector<uint32_t> m_drawOrder; // submesh indices sorted by indexStart
  uint32_t m_indexCount = 0;

  void resetMesh();
  // Fills data.submeshes (in file order) and data.indices (grouped by material, in order of first use)
  static void layoutParts(std::vector<PendingPart> &parts, MeshData &data);
  // Fills the tangents still zero from each triangle's UVs. The bitangent follows +v, which is up in the image for
  // every format: the glTF loader flips v and the image rows together, so normal maps keep their green direction.
  static void generateTangents(MeshData &data);
  // Texture cache key of a glTF file's embedded image, shared by the glTF loader and the mesh cache
  static std::string embeddedImageKey(const std::string &file, size_t image);
  // Embedded images of a cached glTF (MeshCache restores how many and their kinds), from the texture cache only;
  // false if any is missing, and the file must be parsed
  static bool loadCachedImages(const std::string &file, MeshData &data);
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
        m_boundsCenter(other.m_boundsCenter), m_boundsRadius(other.m_boundsRadius), m_boundsMin(other.m_boundsMin),
        m_boundsMax(other.m_boundsMax), m_collisionVertices(std::move(other.m_collisionVertices)),
        m_collisionIndices(std::move(other.m_collisionIndices)), m_skinVBO(std::exchange(other.m_skinVBO, 0)),
        m_animation(std::move(other.m_animation)),
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
      m_boundsMin = other.m_boundsMin;
      m_boundsMax = other.m_boundsMax;
      m_collisionVertices = std::move(other.m_collisionVertices);
      m_collisionIndices = std::move(other.m_collisionIndices);
      if (m_skinVBO)
        glDeleteBuffers(1, &m_skinVBO);
      m_skinVBO = std::exchange(other.m_skinVBO, 0);
      m_animation = std::move(other.m_animation);
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
  glm::vec3 getBoundsMin() const { return m_boundsMin; }
  glm::vec3 getBoundsMax() const { return m_boundsMax; }
  // Model-space triangles for queries (see ModelQueries): positions, and three indices per triangle
  const std::vector<glm::vec3> &getCollisionVertices() const { return m_collisionVertices; }
  const std::vector<uint32_t> &getCollisionIndices() const { return m_collisionIndices; }
  // Skinned or animated: drawn with its joints' matrices (see AnimationSystem); null for static meshes
  const MeshAnimation *getAnimation() const { return m_animation.get(); }
  bool isSkinned() const { return m_animation != nullptr; }
};