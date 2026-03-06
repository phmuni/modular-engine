// Mesh implementation: OBJ loading with tinyobjloader and GPU buffer setup.
#define TINYOBJLOADER_IMPLEMENTATION
#include "rendering/resources/mesh.h"
#include "foundation/core/config.h"
#include <filesystem>
#include <iostream>
#include <map>
#include <tiny_obj_loader/tiny_obj_loader.h>

Mesh::Mesh(const std::string &filename) {
  if (!loadOBJ(filename)) {
    std::cerr << "[Mesh] Failed to load: " << filename << std::endl;
  }
}

Mesh::Mesh(const std::vector<Vertex> &vertices, const std::vector<uint32_t> &indices,
           const std::vector<Submesh> &submeshes)
    : m_vertices(vertices), m_indices(indices), m_submeshes(submeshes) {
  setupBuffers();
}

Mesh::~Mesh() {
  glDeleteVertexArrays(1, &m_VAO);
  glDeleteBuffers(1, &m_VBO);
  glDeleteBuffers(1, &m_EBO);
}

void Mesh::setupBuffers() {
  if (m_vertices.empty() || m_indices.empty()) {
    std::cerr << "[Mesh] No vertices or indices to setup\n";
    return;
  }

  glGenVertexArrays(1, &m_VAO);
  glBindVertexArray(m_VAO);

  glGenBuffers(1, &m_VBO);
  glBindBuffer(GL_ARRAY_BUFFER, m_VBO);
  glBufferData(GL_ARRAY_BUFFER, m_vertices.size() * sizeof(Vertex), m_vertices.data(), GL_STATIC_DRAW);

  glGenBuffers(1, &m_EBO);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_EBO);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, m_indices.size() * sizeof(uint32_t), m_indices.data(), GL_STATIC_DRAW);

  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)offsetof(Vertex, position));
  glEnableVertexAttribArray(0);

  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)offsetof(Vertex, normal));
  glEnableVertexAttribArray(1);

  glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)offsetof(Vertex, texCoord));
  glEnableVertexAttribArray(2);

  glBindVertexArray(0);
}

GLuint Mesh::getVAO() const { return m_VAO; }

const std::vector<Submesh> &Mesh::getSubmeshes() const { return m_submeshes; }

const std::vector<MtlMaterialData> &Mesh::getMtlMaterials() const { return m_mtlMaterials; }

const std::string &Mesh::getBaseDir() const { return m_baseDir; }

void Mesh::setVerticesIndices(const std::vector<Vertex> &vertices, const std::vector<uint32_t> &indices,
                              const std::vector<Submesh> &submeshes) {
  m_vertices = vertices;
  m_indices = indices;
  m_submeshes = submeshes;
  setupBuffers();
}

bool Mesh::loadOBJ(const std::string &filename) {
  std::string normalized = PathUtils::normalize(filename);

  if (!PathUtils::hasExtension(normalized) || !std::filesystem::exists(normalized)) {
    std::string withObj = PathUtils::stripExtension(normalized) + ".obj";
    if (std::filesystem::exists(withObj)) {
      normalized = withObj;
    }
  }

  tinyobj::attrib_t attrib;
  std::vector<tinyobj::shape_t> shapes;
  std::vector<tinyobj::material_t> materials;
  std::string err;
  m_baseDir = normalized.substr(0, normalized.find_last_of('/') + 1);

  bool success = tinyobj::LoadObj(&attrib, &shapes, &materials, &err, normalized.c_str(), m_baseDir.c_str());

  if (!err.empty())
    std::cerr << "[Mesh] " << err << std::endl;

  if (!success)
    return false;

  // Convert tinyobj materials to MtlMaterialData
  for (const auto &mat : materials) {
    MtlMaterialData mtl;
    mtl.name = mat.name;
    mtl.diffuseTexPath = mat.diffuse_texname;
    mtl.specularTexPath = mat.specular_texname;
    mtl.normalTexPath = mat.normal_texname.empty() ? mat.bump_texname : mat.normal_texname;
    mtl.emissionTexPath = mat.emissive_texname;
    mtl.diffuseColor = {mat.diffuse[0], mat.diffuse[1], mat.diffuse[2]};
    mtl.specularColor = {mat.specular[0], mat.specular[1], mat.specular[2]};
    mtl.emissionColor = {mat.emission[0], mat.emission[1], mat.emission[2]};
    mtl.shininess = mat.shininess > 0.0f ? mat.shininess : 16.0f;
    m_mtlMaterials.push_back(std::move(mtl));
  }

  for (const auto &shape : shapes) {
    size_t numFaces = shape.mesh.material_ids.size();

    // Precompute index offsets per face (prefix sum)
    std::vector<size_t> faceIndexOffset(numFaces);
    if (numFaces > 0) {
      faceIndexOffset[0] = 0;
      for (size_t f = 1; f < numFaces; f++)
        faceIndexOffset[f] = faceIndexOffset[f - 1] + shape.mesh.num_face_vertices[f - 1];
    }

    // Group faces by material ID
    std::map<int, std::vector<size_t>> matFaceGroups;
    for (size_t f = 0; f < numFaces; f++) {
      matFaceGroups[shape.mesh.material_ids[f]].push_back(f);
    }

    for (auto &[matId, faceIndices] : matFaceGroups) {
      uint32_t submeshStart = static_cast<uint32_t>(m_indices.size());

      for (size_t f : faceIndices) {
        size_t fv = shape.mesh.num_face_vertices[f];
        size_t offset = faceIndexOffset[f];

        for (size_t v = 0; v < fv; v++) {
          const auto &idx = shape.mesh.indices[offset + v];
          Vertex vertex{};

          vertex.position = {attrib.vertices[3 * idx.vertex_index + 0], attrib.vertices[3 * idx.vertex_index + 1],
                             attrib.vertices[3 * idx.vertex_index + 2]};

          if (idx.normal_index >= 0) {
            vertex.normal = {attrib.normals[3 * idx.normal_index + 0], attrib.normals[3 * idx.normal_index + 1],
                             attrib.normals[3 * idx.normal_index + 2]};
          }

          if (idx.texcoord_index >= 0) {
            vertex.texCoord = {attrib.texcoords[2 * idx.texcoord_index + 0],
                               attrib.texcoords[2 * idx.texcoord_index + 1]};
          }

          m_vertices.push_back(vertex);
          m_indices.push_back(static_cast<uint32_t>(m_vertices.size() - 1));
        }
      }

      uint32_t submeshCount = static_cast<uint32_t>(m_indices.size()) - submeshStart;
      m_submeshes.push_back({submeshStart, submeshCount, matId});
    }
  }

  setupBuffers();
  return true;
}
