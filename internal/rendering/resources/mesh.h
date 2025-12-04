#pragma once
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <iostream> // IWYU pragma: keep
#include <string>
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
  bool loadFromOBJ(const std::string &filename);

public:
  Mesh() {};

  // Construtor que carrega de arquivo OBJ
  Mesh(const std::string &filename);

  // Construtor com vértices e índices
  Mesh(const std::vector<Vertex> &vertices, const std::vector<unsigned int> &indices);

  ~Mesh();

  GLuint getVAO() const;
  size_t getIndexCount() const;
  void setVerticesIndices(const std::vector<Vertex> &vertices, const std::vector<unsigned int> &indices);
};