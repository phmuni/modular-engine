#pragma once

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <iostream> // IWYU pragma: keep
#include <vector>

struct Vertex {
  glm::vec3 position;
  glm::vec3 normal;
  glm::vec2 texCoord;
};

class Mesh {
private:
  GLuint VAO, VBO, EBO;
  std::vector<Vertex> vertices;
  std::vector<unsigned int> indices;

  void setupBuffers();

public:
  Mesh() {};
  Mesh(const std::vector<Vertex> &vertices, const std::vector<unsigned int> &indices);
  ~Mesh();

  // Getters
  GLuint getVAO() const;
  size_t getIndexCount() const;

  // Setters
  void setVerticesIndices(const std::vector<Vertex> &vertices, const std::vector<unsigned int> &indices);
};
