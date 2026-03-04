#pragma once
// Mesh class: VAO/VBO/EBO management and OBJ loading.

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <string>
#include <vector>

struct Vertex {
  glm::vec3 position;
  glm::vec3 normal;
  glm::vec2 texCoord;
};

struct Submesh {
  uint32_t indexStart;
  uint32_t indexCount;
};

class Mesh {
private:
  GLuint m_VAO = 0;
  GLuint m_VBO = 0;
  GLuint m_EBO = 0;

  std::vector<Vertex> m_vertices;
  std::vector<uint32_t> m_indices;
  std::vector<Submesh> m_submeshes;

  void setupBuffers();
  bool loadOBJ(const std::string &filename);

public:
  Mesh() = default;
  explicit Mesh(const std::string &filename);
  Mesh(const std::vector<Vertex> &vertices, const std::vector<uint32_t> &indices,
       const std::vector<Submesh> &submeshes);
  ~Mesh();

  // Prevent copy, allow move
  Mesh(const Mesh &) = delete;
  Mesh &operator=(const Mesh &) = delete;
  Mesh(Mesh &&) = default;
  Mesh &operator=(Mesh &&) = default;

  GLuint getVAO() const;
  const std::vector<Submesh> &getSubmeshes() const;

  void setVerticesIndices(const std::vector<Vertex> &vertices, const std::vector<uint32_t> &indices,
                          const std::vector<Submesh> &submeshes);
};
