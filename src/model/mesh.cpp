#include "model/mesh.h"

Mesh::Mesh(const std::vector<Vertex> &vertices, const std::vector<unsigned int> &indices)
    : vertices(vertices), indices(indices), VAO(0), VBO(0), EBO(0) {
  setupBuffers();
}

Mesh::~Mesh() {
  // Free OpenGL resources
  glDeleteVertexArrays(1, &VAO);
  glDeleteBuffers(1, &VBO);
  glDeleteBuffers(1, &EBO);
}

void Mesh::setupBuffers() {
  if (vertices.empty() || indices.empty()) {
    std::cerr << "Erro: Vertices ou índices não definidos!" << std::endl;
    return;
  }

  // Create VAO, VBO and EBO
  glGenVertexArrays(1, &VAO);
  glBindVertexArray(VAO);

  glGenBuffers(1, &VBO);
  glBindBuffer(GL_ARRAY_BUFFER, VBO);
  glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Vertex), &vertices[0], GL_STATIC_DRAW);

  glGenBuffers(1, &EBO);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), &indices[0], GL_STATIC_DRAW);

  // Vertex attribute configuration
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)offsetof(Vertex, position));
  glEnableVertexAttribArray(0);

  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)offsetof(Vertex, normal));
  glEnableVertexAttribArray(1);

  glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)offsetof(Vertex, texCoord));
  glEnableVertexAttribArray(2);

  glBindVertexArray(0);
}

GLuint Mesh::getVAO() const { return VAO; }

size_t Mesh::getIndexCount() const { return indices.size(); }

void Mesh::setVerticesIndices(const std::vector<Vertex> &vertices, const std::vector<unsigned int> &indices) {
  this->vertices = vertices;
  this->indices = indices;
  setupBuffers();
}
