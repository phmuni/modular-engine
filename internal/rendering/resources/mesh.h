#pragma once
// Mesh class for loading OBJ files and managing vertex/index buffers.

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <string>
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
  glm::vec3 diffuseColor{0.8f};
  glm::vec3 specularColor{0.0f};
  glm::vec3 emissionColor{0.0f};
  float shininess = 16.0f;
};

struct Submesh {
  uint32_t indexStart;
  uint32_t indexCount;
  int materialIndex = -1;
};

class Mesh {
private:
  GLuint m_VAO = 0;
  GLuint m_VBO = 0;
  GLuint m_EBO = 0;
  std::vector<Submesh> m_submeshes;
  std::vector<MtlMaterialData> m_mtlMaterials;
  std::string m_baseDir;

  void setupBuffers(std::vector<Vertex> &vertices, std::vector<uint32_t> &indices);
  bool loadOBJ(const std::string filename);

public:
  Mesh() = default;
  explicit Mesh(const std::string filename);
  Mesh(std::vector<Vertex> vertices, std::vector<uint32_t> indices, std::vector<Submesh> submeshes);
  ~Mesh();

  Mesh(const Mesh &) = delete;
  Mesh &operator=(const Mesh &) = delete;

  Mesh(Mesh &&other) noexcept
      : m_VAO(std::exchange(other.m_VAO, 0)), m_VBO(std::exchange(other.m_VBO, 0)),
        m_EBO(std::exchange(other.m_EBO, 0)), m_submeshes(std::move(other.m_submeshes)),
        m_mtlMaterials(std::move(other.m_mtlMaterials)), m_baseDir(std::move(other.m_baseDir)) {}

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
    }
    return *this;
  }

  GLuint getVAO() const { return m_VAO; }
  const std::vector<Submesh> &getSubmeshes() const { return m_submeshes; }
  const std::vector<MtlMaterialData> &getMtlMaterials() const { return m_mtlMaterials; }
  std::string_view getBaseDir() const { return m_baseDir; }

  void setVerticesIndices(std::vector<Vertex> vertices, std::vector<uint32_t> indices, std::vector<Submesh> submeshes);
};