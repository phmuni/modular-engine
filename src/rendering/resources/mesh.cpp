
// Mesh class implementation with OBJ and glTF loading using tinyobjloader/tinygltf.
#define TINYOBJLOADER_IMPLEMENTATION
#define TINYGLTF3_IMPLEMENTATION

#include "rendering/resources/mesh.h"
#include "foundation/core/config.h"
#include "foundation/core/logger.h"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <stb_image/stb_image.h>
#include <tiny_gltf_loader/tiny_gltf_v3.h>
#include <tiny_obj_loader/tiny_obj_loader.h>

namespace {

std::string toStdString(const tg3_str &value) {
  if (!value.data || value.len == 0)
    return {};
  return std::string(value.data, value.len);
}

std::string toLowerCopy(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return value;
}

const tg3_extension *findExtension(const tg3_extras_ext &ext, const char *name) {
  for (uint32_t i = 0; i < ext.extensions_count; ++i) {
    if (tg3_str_equals_cstr(ext.extensions[i].name, name))
      return &ext.extensions[i];
  }
  return nullptr;
}

const tg3_value *findObjectValue(const tg3_value &value, const char *key) {
  if (value.type != TG3_VALUE_OBJECT || !value.object_data)
    return nullptr;

  for (uint32_t i = 0; i < value.object_count; ++i) {
    if (tg3_str_equals_cstr(value.object_data[i].key, key))
      return &value.object_data[i].value;
  }

  return nullptr;
}

int32_t readNestedTextureIndex(const tg3_value *value, const char *key, int32_t fallback = -1) {
  if (!value)
    return fallback;

  const tg3_value *member = findObjectValue(*value, key);
  if (!member || member->type != TG3_VALUE_OBJECT)
    return fallback;

  const tg3_value *indexValue = findObjectValue(*member, "index");
  if (!indexValue || indexValue->type != TG3_VALUE_INT)
    return fallback;

  return static_cast<int32_t>(indexValue->int_val);
}

std::string joinBasePath(std::string_view baseDir, std::string_view relativePath) {
  if (baseDir.empty())
    return std::string(relativePath);

  std::string result;
  result.reserve(baseDir.size() + relativePath.size() + 1);
  result.append(baseDir);
  if (!result.empty() && result.back() != '/')
    result.push_back('/');
  result.append(relativePath);
  return result;
}

template <typename T> T readPod(const uint8_t *data) {
  T value{};
  std::memcpy(&value, data, sizeof(T));
  return value;
}

float readComponentAsFloat(const uint8_t *data, int32_t componentType, bool normalized) {
  switch (componentType) {
  case TG3_COMPONENT_TYPE_BYTE: {
    int8_t value = readPod<int8_t>(data);
    return normalized ? std::max(static_cast<float>(value) / 127.0f, -1.0f) : static_cast<float>(value);
  }
  case TG3_COMPONENT_TYPE_UNSIGNED_BYTE: {
    uint8_t value = readPod<uint8_t>(data);
    return normalized ? static_cast<float>(value) / 255.0f : static_cast<float>(value);
  }
  case TG3_COMPONENT_TYPE_SHORT: {
    int16_t value = readPod<int16_t>(data);
    return normalized ? std::max(static_cast<float>(value) / 32767.0f, -1.0f) : static_cast<float>(value);
  }
  case TG3_COMPONENT_TYPE_UNSIGNED_SHORT: {
    uint16_t value = readPod<uint16_t>(data);
    return normalized ? static_cast<float>(value) / 65535.0f : static_cast<float>(value);
  }
  case TG3_COMPONENT_TYPE_INT:
    return static_cast<float>(readPod<int32_t>(data));
  case TG3_COMPONENT_TYPE_UNSIGNED_INT:
    return static_cast<float>(readPod<uint32_t>(data));
  case TG3_COMPONENT_TYPE_FLOAT:
    return readPod<float>(data);
  case TG3_COMPONENT_TYPE_DOUBLE:
    return static_cast<float>(readPod<double>(data));
  default:
    return 0.0f;
  }
}

uint32_t readComponentAsIndex(const uint8_t *data, int32_t componentType) {
  switch (componentType) {
  case TG3_COMPONENT_TYPE_UNSIGNED_BYTE:
    return static_cast<uint32_t>(readPod<uint8_t>(data));
  case TG3_COMPONENT_TYPE_BYTE:
    return static_cast<uint32_t>(std::max<int32_t>(0, readPod<int8_t>(data)));
  case TG3_COMPONENT_TYPE_UNSIGNED_SHORT:
    return static_cast<uint32_t>(readPod<uint16_t>(data));
  case TG3_COMPONENT_TYPE_SHORT:
    return static_cast<uint32_t>(std::max<int32_t>(0, readPod<int16_t>(data)));
  case TG3_COMPONENT_TYPE_UNSIGNED_INT:
    return readPod<uint32_t>(data);
  case TG3_COMPONENT_TYPE_INT:
    return static_cast<uint32_t>(std::max<int32_t>(0, readPod<int32_t>(data)));
  default:
    return 0;
  }
}

const tg3_accessor *findAccessor(const tg3_model &model, const tg3_primitive &primitive, const char *semantic) {
  if (!primitive.attributes || primitive.attributes_count == 0)
    return nullptr;

  for (uint32_t i = 0; i < primitive.attributes_count; ++i) {
    if (!tg3_str_equals_cstr(primitive.attributes[i].key, semantic))
      continue;

    int32_t accessorIndex = primitive.attributes[i].value;
    if (accessorIndex < 0 || accessorIndex >= static_cast<int32_t>(model.accessors_count))
      return nullptr;
    return &model.accessors[accessorIndex];
  }

  return nullptr;
}

const uint8_t *getAccessorBase(const tg3_model &model, const tg3_accessor &accessor, size_t &stride, size_t &componentCount) {
  if (accessor.buffer_view < 0 || accessor.buffer_view >= static_cast<int32_t>(model.buffer_views_count))
    return nullptr;

  const tg3_buffer_view &bufferView = model.buffer_views[accessor.buffer_view];
  if (bufferView.buffer < 0 || bufferView.buffer >= static_cast<int32_t>(model.buffers_count))
    return nullptr;

  const tg3_buffer &buffer = model.buffers[bufferView.buffer];
  if (!buffer.data.data || buffer.data.count == 0)
    return nullptr;

  int32_t numComponents = tg3_num_components(accessor.type);
  int32_t componentSize = tg3_component_size(accessor.component_type);
  if (numComponents <= 0 || componentSize <= 0)
    return nullptr;

  int32_t accessorStride = tg3_accessor_byte_stride(&accessor, &bufferView);
  stride = accessorStride > 0 ? static_cast<size_t>(accessorStride) : static_cast<size_t>(numComponents * componentSize);
  componentCount = static_cast<size_t>(numComponents);

  size_t baseOffset = static_cast<size_t>(bufferView.byte_offset + accessor.byte_offset);
  size_t bufferSize = static_cast<size_t>(buffer.data.count);
  if (baseOffset > bufferSize)
    return nullptr;

  size_t lastByte = baseOffset;
  if (accessor.count > 0) {
    size_t count = static_cast<size_t>(accessor.count);
    lastByte = baseOffset + (count - 1) * stride + componentCount * static_cast<size_t>(componentSize);
    if (lastByte > bufferSize)
      return nullptr;
  }

  return buffer.data.data + baseOffset;
}

glm::vec2 readAccessorVec2(const tg3_model &model, const tg3_accessor &accessor, size_t elementIndex) {
  glm::vec2 value(0.0f);
  size_t stride = 0;
  size_t componentCount = 0;
  const uint8_t *base = getAccessorBase(model, accessor, stride, componentCount);
  if (!base || elementIndex >= static_cast<size_t>(accessor.count))
    return value;

  size_t componentSize = static_cast<size_t>(tg3_component_size(accessor.component_type));
  for (size_t c = 0; c < 2 && c < componentCount; ++c) {
    const uint8_t *component = base + elementIndex * stride + c * componentSize;
    value[c] = readComponentAsFloat(component, accessor.component_type, accessor.normalized != 0);
  }
  return value;
}

glm::vec3 readAccessorVec3(const tg3_model &model, const tg3_accessor &accessor, size_t elementIndex) {
  glm::vec3 value(0.0f);
  size_t stride = 0;
  size_t componentCount = 0;
  const uint8_t *base = getAccessorBase(model, accessor, stride, componentCount);
  if (!base || elementIndex >= static_cast<size_t>(accessor.count))
    return value;

  size_t componentSize = static_cast<size_t>(tg3_component_size(accessor.component_type));
  for (size_t c = 0; c < 3 && c < componentCount; ++c) {
    const uint8_t *component = base + elementIndex * stride + c * componentSize;
    value[c] = readComponentAsFloat(component, accessor.component_type, accessor.normalized != 0);
  }
  return value;
}

std::vector<uint32_t> readAccessorIndices(const tg3_model &model, const tg3_accessor &accessor) {
  std::vector<uint32_t> indices;
  if (accessor.count == 0)
    return indices;

  size_t stride = 0;
  size_t componentCount = 0;
  const uint8_t *base = getAccessorBase(model, accessor, stride, componentCount);
  if (!base || componentCount != 1)
    return indices;

  size_t componentSize = static_cast<size_t>(tg3_component_size(accessor.component_type));
  indices.reserve(static_cast<size_t>(accessor.count));
  for (size_t i = 0; i < static_cast<size_t>(accessor.count); ++i) {
    const uint8_t *component = base + i * stride;
    indices.push_back(readComponentAsIndex(component, accessor.component_type));
  }
  return indices;
}

std::string resolveTexturePath(const std::string &baseDir, const tg3_model &model, int32_t textureIndex) {
  if (textureIndex < 0 || textureIndex >= static_cast<int32_t>(model.textures_count))
    return {};

  const tg3_texture &texture = model.textures[textureIndex];
  if (texture.source < 0 || texture.source >= static_cast<int32_t>(model.images_count))
    return {};

  const tg3_image &image = model.images[texture.source];
  if (image.uri.len == 0 || tg3_is_data_uri(image.uri.data, image.uri.len))
    return {};

  return joinBasePath(baseDir, std::string_view(image.uri.data, image.uri.len));
}

glm::mat4 composeNodeTransform(const tg3_node &node) {
  if (node.has_matrix) {
    glm::mat4 matrix(1.0f);
    for (int i = 0; i < 16; ++i)
      matrix[i / 4][i % 4] = static_cast<float>(node.matrix[i]);
    return matrix;
  }

  glm::vec3 translation(static_cast<float>(node.translation[0]), static_cast<float>(node.translation[1]), static_cast<float>(node.translation[2]));
  glm::vec3 scale(static_cast<float>(node.scale[0]), static_cast<float>(node.scale[1]), static_cast<float>(node.scale[2]));
  glm::quat rotation(static_cast<float>(node.rotation[3]), static_cast<float>(node.rotation[0]), static_cast<float>(node.rotation[1]),
                     static_cast<float>(node.rotation[2]));

  glm::mat4 matrix(1.0f);
  matrix = glm::translate(matrix, translation);
  matrix *= glm::mat4_cast(rotation);
  matrix = glm::scale(matrix, scale);
  return matrix;
}

int decodeBase64Char(unsigned char c) {
  if (c >= 'A' && c <= 'Z')
    return static_cast<int>(c - 'A');
  if (c >= 'a' && c <= 'z')
    return static_cast<int>(c - 'a' + 26);
  if (c >= '0' && c <= '9')
    return static_cast<int>(c - '0' + 52);
  if (c == '+')
    return 62;
  if (c == '/')
    return 63;
  return -1;
}

std::vector<uint8_t> decodeBase64(std::string_view encoded) {
  std::vector<uint8_t> decoded;
  int value = 0;
  int bits = -8;

  for (unsigned char c : encoded) {
    if (std::isspace(c) || c == '=')
      continue;

    int digit = decodeBase64Char(c);
    if (digit < 0)
      return {};

    value = (value << 6) | digit;
    bits += 6;
    if (bits >= 0) {
      decoded.push_back(static_cast<uint8_t>((value >> bits) & 0xFF));
      bits -= 8;
    }
  }

  return decoded;
}

std::vector<uint8_t> extractImageBytes(const tg3_model &model, const tg3_image &image) {
  if (image.image.data && image.image.count > 0)
    return std::vector<uint8_t>(image.image.data, image.image.data + image.image.count);

  if (image.buffer_view >= 0 && image.buffer_view < static_cast<int32_t>(model.buffer_views_count)) {
    const tg3_buffer_view &bufferView = model.buffer_views[image.buffer_view];
    if (bufferView.buffer >= 0 && bufferView.buffer < static_cast<int32_t>(model.buffers_count)) {
      const tg3_buffer &buffer = model.buffers[bufferView.buffer];
      size_t offset = static_cast<size_t>(bufferView.byte_offset);
      size_t length = static_cast<size_t>(bufferView.byte_length);
      if (buffer.data.data && offset + length <= buffer.data.count) {
        return std::vector<uint8_t>(buffer.data.data + offset, buffer.data.data + offset + length);
      }
    }
  }

  if (image.uri.data && image.uri.len > 0 && tg3_is_data_uri(image.uri.data, image.uri.len)) {
    std::string_view uri(image.uri.data, image.uri.len);
    auto comma = uri.find(',');
    if (comma != std::string_view::npos) {
      std::string_view payload = uri.substr(comma + 1);
      return decodeBase64(payload);
    }
  }

  return {};
}

GLuint uploadTextureFromPixels(const uint8_t *pixels, int width, int height, int components, int pixelType) {
  GLenum format = GL_RGB;
  switch (components) {
  case 1: format = GL_RED; break;
  case 2: format = GL_RG; break;
  case 3: format = GL_RGB; break;
  case 4: format = GL_RGBA; break;
  default: return 0;
  }

  GLenum type = GL_UNSIGNED_BYTE;
  switch (pixelType) {
  case TG3_COMPONENT_TYPE_UNSIGNED_BYTE: type = GL_UNSIGNED_BYTE; break;
  case TG3_COMPONENT_TYPE_UNSIGNED_SHORT: type = GL_UNSIGNED_SHORT; break;
  case TG3_COMPONENT_TYPE_FLOAT: type = GL_FLOAT; break;
  default: return 0;
  }

  GLuint textureId = 0;
  glGenTextures(1, &textureId);
  glBindTexture(GL_TEXTURE_2D, textureId);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, type, pixels);
  glGenerateMipmap(GL_TEXTURE_2D);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
  return textureId;
}

GLuint loadTextureFromImage(const tg3_model &model, const tg3_image &image) {
  if (image.image.data && image.image.count > 0 && image.as_is == 0) {
    return uploadTextureFromPixels(image.image.data, image.width, image.height, image.component, image.pixel_type);
  }

  std::vector<uint8_t> encoded = extractImageBytes(model, image);
  if (encoded.empty())
    return 0;

  int decodedWidth = 0;
  int decodedHeight = 0;
  int decodedChannels = 0;
  stbi_uc *decoded = stbi_load_from_memory(encoded.data(), static_cast<int>(encoded.size()), &decodedWidth, &decodedHeight,
                                          &decodedChannels, 0);
  if (!decoded)
    return 0;

  GLuint textureId = uploadTextureFromPixels(decoded, decodedWidth, decodedHeight, decodedChannels, TG3_COMPONENT_TYPE_UNSIGNED_BYTE);
  stbi_image_free(decoded);
  return textureId;
}

} // namespace

namespace std {
template <> struct hash<Vertex> {
  size_t operator()(const Vertex &v) const {
    size_t h = 0;
    auto combine = [&](float f) { h ^= std::hash<float>{}(f) + 0x9e3779b9 + (h << 6) + (h >> 2); };
    combine(v.position.x);
    combine(v.position.y);
    combine(v.position.z);
    combine(v.normal.x);
    combine(v.normal.y);
    combine(v.normal.z);
    combine(v.texCoord.x);
    combine(v.texCoord.y);
    return h;
  }
};
} // namespace std

Mesh::Mesh(std::string filename) {
  const std::string originalFilename = filename;
  std::string normalized = PathUtils::normalizeSeparators(std::move(filename));
  std::string extension = toLowerCopy(std::filesystem::path(normalized).extension().string());

  bool loaded = false;
  if (extension == ".glb" || extension == ".gltf") {
    loaded = loadGLB(std::move(normalized));
  } else if (extension == ".obj") {
    loaded = loadOBJ(std::move(normalized));
  } else {
    loaded = loadOBJ(normalized) || loadGLB(std::move(normalized));
  }

  if (!loaded)
    LOG_E("[Mesh] Failed to load: %s", originalFilename.c_str());
}

Mesh::~Mesh() {
  resetMesh();
}

void Mesh::resetMesh() {
  m_submeshes.clear();
  m_mtlMaterials.clear();

  if (m_VAO != 0) {
    glDeleteVertexArrays(1, &m_VAO);
    m_VAO = 0;
  }
  if (m_VBO != 0) {
    glDeleteBuffers(1, &m_VBO);
    m_VBO = 0;
  }
  if (m_EBO != 0) {
    glDeleteBuffers(1, &m_EBO);
    m_EBO = 0;
  }
}

void Mesh::setupBuffers(std::vector<Vertex> &vertices, std::vector<uint32_t> &indices) {
  if (vertices.empty() || indices.empty()) {
    LOG_W("[Mesh] No vertices or indices to setup");
    return;
  }

  glGenVertexArrays(1, &m_VAO);
  glBindVertexArray(m_VAO);

  glGenBuffers(1, &m_VBO);
  glBindBuffer(GL_ARRAY_BUFFER, m_VBO);
  glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Vertex), vertices.data(), GL_STATIC_DRAW);

  glGenBuffers(1, &m_EBO);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_EBO);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(uint32_t), indices.data(), GL_STATIC_DRAW);

  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)offsetof(Vertex, position));
  glEnableVertexAttribArray(0);

  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)offsetof(Vertex, normal));
  glEnableVertexAttribArray(1);

  glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void *)offsetof(Vertex, texCoord));
  glEnableVertexAttribArray(2);

  glBindVertexArray(0);
}

bool Mesh::loadOBJ(std::string filename) {
  resetMesh();

  std::string normalized = PathUtils::normalizeSeparators(std::move(filename));

  if (!PathUtils::hasExtension(normalized) || !std::filesystem::exists(normalized)) {
    std::string withObj = std::string(PathUtils::stripExtension(normalized)) + ".obj";
    if (std::filesystem::exists(withObj))
      normalized = std::move(withObj);
  }

  auto slash = normalized.rfind('/');
  m_baseDir = (slash != std::string::npos) ? normalized.substr(0, slash + 1) : "./";

  tinyobj::attrib_t attrib;
  std::vector<tinyobj::shape_t> shapes;
  std::vector<tinyobj::material_t> materials;
  std::string err;

  bool success = tinyobj::LoadObj(&attrib, &shapes, &materials, &err, normalized.c_str(), m_baseDir.c_str());

  if (!err.empty())
    LOG_W("[Mesh] %s", err.c_str());

  if (!success)
    return false;

  m_mtlMaterials.reserve(materials.size());
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

  std::vector<Vertex> vertices;
  std::vector<uint32_t> indices;
  std::unordered_map<Vertex, uint32_t> vertexCache;
  const bool generateNormals = attrib.normals.empty();

  for (const auto &shape : shapes) {
    const size_t numFaces = shape.mesh.num_face_vertices.size();

    std::vector<size_t> faceIndexOffset(numFaces);
    if (numFaces > 0) {
      faceIndexOffset[0] = 0;
      for (size_t f = 1; f < numFaces; f++)
        faceIndexOffset[f] = faceIndexOffset[f - 1] + shape.mesh.num_face_vertices[f - 1];
    }

    std::unordered_map<int, std::vector<size_t>> matFaceGroups;
    for (size_t f = 0; f < numFaces; f++) {
      int matId = (f < shape.mesh.material_ids.size()) ? shape.mesh.material_ids[f] : -1;
      matFaceGroups[matId].push_back(f);
    }

    auto emitVertex = [&](const tinyobj::index_t &idx) {
      Vertex vertex{};

      if (idx.vertex_index >= 0) {
        vertex.position = {attrib.vertices[3 * idx.vertex_index + 0], attrib.vertices[3 * idx.vertex_index + 1],
                           attrib.vertices[3 * idx.vertex_index + 2]};
      }

      if (idx.normal_index >= 0) {
        vertex.normal = {attrib.normals[3 * idx.normal_index + 0], attrib.normals[3 * idx.normal_index + 1],
                         attrib.normals[3 * idx.normal_index + 2]};
      }

      if (idx.texcoord_index >= 0) {
        vertex.texCoord = {attrib.texcoords[2 * idx.texcoord_index + 0], attrib.texcoords[2 * idx.texcoord_index + 1]};
      }

      auto [it, inserted] = vertexCache.emplace(vertex, static_cast<uint32_t>(vertices.size()));
      if (inserted)
        vertices.push_back(vertex);
      indices.push_back(it->second);
    };

    for (auto &[matId, faceIndices] : matFaceGroups) {
      uint32_t submeshStart = static_cast<uint32_t>(indices.size());

      for (size_t f : faceIndices) {
        size_t fv = shape.mesh.num_face_vertices[f];
        size_t offset = faceIndexOffset[f];

        if (fv < 3)
          continue;

        const auto &first = shape.mesh.indices[offset + 0];
        for (size_t v = 1; v + 1 < fv; ++v) {
          emitVertex(first);
          emitVertex(shape.mesh.indices[offset + v]);
          emitVertex(shape.mesh.indices[offset + v + 1]);
        }
      }

      uint32_t submeshCount = static_cast<uint32_t>(indices.size()) - submeshStart;
      m_submeshes.push_back({submeshStart, submeshCount, matId});
    }
  }

  if (generateNormals) {
    for (auto &vertex : vertices) {
      vertex.normal = glm::vec3(0.0f);
    }

    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
      Vertex &a = vertices[indices[i]];
      Vertex &b = vertices[indices[i + 1]];
      Vertex &c = vertices[indices[i + 2]];

      glm::vec3 faceNormal = glm::cross(b.position - a.position, c.position - a.position);
      float normalLength = glm::length(faceNormal);
      if (normalLength <= 0.0f)
        continue;

      faceNormal /= normalLength;
      a.normal += faceNormal;
      b.normal += faceNormal;
      c.normal += faceNormal;
    }

    for (auto &vertex : vertices) {
      float normalLength = glm::length(vertex.normal);
      if (normalLength > 0.0f) {
        vertex.normal /= normalLength;
      } else {
        vertex.normal = glm::vec3(0.0f, 1.0f, 0.0f);
      }
    }
  }

  setupBuffers(vertices, indices);
  return true;
}

bool Mesh::loadGLB(std::string filename) {
  resetMesh();

  std::string normalized = PathUtils::normalizeSeparators(std::move(filename));
  if (!PathUtils::hasExtension(normalized) || !std::filesystem::exists(normalized)) {
    std::string base = std::string(PathUtils::stripExtension(normalized));
    std::string withGlb = base + ".glb";
    std::string withGltf = base + ".gltf";
    if (std::filesystem::exists(withGlb)) {
      normalized = std::move(withGlb);
    } else if (std::filesystem::exists(withGltf)) {
      normalized = std::move(withGltf);
    }
  }

  if (!std::filesystem::exists(normalized)) {
    LOG_W("[Mesh] glTF file not found: %s", normalized.c_str());
    return false;
  }

  std::filesystem::path filePath(normalized);
  std::filesystem::path basePath = filePath.parent_path();
  m_baseDir = basePath.empty() ? std::string("./") : PathUtils::normalizeSeparators(basePath.string()) + "/";

  std::ifstream file(normalized, std::ios::binary);
  if (!file) {
    LOG_E("[Mesh] Failed to open GLB: %s", normalized.c_str());
    return false;
  }

  file.seekg(0, std::ios::end);
  std::streamsize fileSize = file.tellg();
  if (fileSize <= 0) {
    LOG_E("[Mesh] GLB file is empty: %s", normalized.c_str());
    return false;
  }
  file.seekg(0, std::ios::beg);

  std::vector<uint8_t> fileData(static_cast<size_t>(fileSize));
  if (!file.read(reinterpret_cast<char *>(fileData.data()), fileSize)) {
    LOG_E("[Mesh] Failed to read GLB: %s", normalized.c_str());
    return false;
  }

  tinygltf3::Model model;
  tinygltf3::ErrorStack errors;
  tg3_parse_options options;
  tg3_parse_options_init(&options);

  tg3_error_code parseResult = tg3_parse_auto(model.get(), errors.get(), fileData.data(), static_cast<uint64_t>(fileData.size()),
                                              m_baseDir.c_str(), static_cast<uint32_t>(m_baseDir.size()), &options);
  if (parseResult != TG3_OK) {
    for (uint32_t i = 0; i < errors.count(); ++i) {
      const tg3_error_entry *entry = errors.entry(i);
      if (entry && entry->message)
        LOG_W("[Mesh] %s", entry->message);
    }
    LOG_E("[Mesh] Failed to parse GLB: %s", normalized.c_str());
    return false;
  }

  const tg3_model *gltf = model.get();
  std::vector<GLuint> imageTextureIds(gltf->images_count, 0);
  for (uint32_t i = 0; i < gltf->images_count; ++i) {
    imageTextureIds[i] = loadTextureFromImage(*gltf, gltf->images[i]);
  }

  m_mtlMaterials.reserve(gltf->materials_count);
  for (uint32_t i = 0; i < gltf->materials_count; ++i) {
    const tg3_material &source = gltf->materials[i];
    MtlMaterialData material;
    material.name = toStdString(source.name);
    material.diffuseColor = {static_cast<float>(source.pbr_metallic_roughness.base_color_factor[0]),
                             static_cast<float>(source.pbr_metallic_roughness.base_color_factor[1]),
                             static_cast<float>(source.pbr_metallic_roughness.base_color_factor[2])};
    material.specularColor = {0.0f, 0.0f, 0.0f};
    material.emissionColor = {static_cast<float>(source.emissive_factor[0]), static_cast<float>(source.emissive_factor[1]),
                              static_cast<float>(source.emissive_factor[2])};
    material.shininess = 16.0f;
    if (source.pbr_metallic_roughness.base_color_texture.index >= 0 &&
        source.pbr_metallic_roughness.base_color_texture.index < static_cast<int32_t>(gltf->textures_count)) {
      const tg3_texture &texture = gltf->textures[source.pbr_metallic_roughness.base_color_texture.index];
      if (texture.source >= 0 && texture.source < static_cast<int32_t>(imageTextureIds.size()) && imageTextureIds[texture.source] != 0) {
        material.diffuseTextureId = imageTextureIds[texture.source];
      } else {
        material.diffuseTexPath = resolveTexturePath(m_baseDir, *gltf, source.pbr_metallic_roughness.base_color_texture.index);
      }
    }
    if (source.normal_texture.index >= 0 && source.normal_texture.index < static_cast<int32_t>(gltf->textures_count)) {
      const tg3_texture &texture = gltf->textures[source.normal_texture.index];
      if (texture.source >= 0 && texture.source < static_cast<int32_t>(imageTextureIds.size()) && imageTextureIds[texture.source] != 0) {
        material.normalTextureId = imageTextureIds[texture.source];
      } else {
        material.normalTexPath = resolveTexturePath(m_baseDir, *gltf, source.normal_texture.index);
      }
    }
    if (source.emissive_texture.index >= 0 && source.emissive_texture.index < static_cast<int32_t>(gltf->textures_count)) {
      const tg3_texture &texture = gltf->textures[source.emissive_texture.index];
      if (texture.source >= 0 && texture.source < static_cast<int32_t>(imageTextureIds.size()) && imageTextureIds[texture.source] != 0) {
        material.emissionTextureId = imageTextureIds[texture.source];
      } else {
        material.emissionTexPath = resolveTexturePath(m_baseDir, *gltf, source.emissive_texture.index);
      }
    }

    if (const tg3_extension *specularExt = findExtension(source.ext, "KHR_materials_specular")) {
      const tg3_value *specularValue = &specularExt->value;
      int32_t specularTextureIndex = readNestedTextureIndex(specularValue, "specularTexture", -1);
      if (specularTextureIndex >= 0 && specularTextureIndex < static_cast<int32_t>(gltf->textures_count)) {
        const tg3_texture &texture = gltf->textures[specularTextureIndex];
        if (texture.source >= 0 && texture.source < static_cast<int32_t>(imageTextureIds.size()) && imageTextureIds[texture.source] != 0) {
          material.specularTextureId = imageTextureIds[texture.source];
        } else {
          material.specularTexPath = resolveTexturePath(m_baseDir, *gltf, specularTextureIndex);
        }
      }
    }

    m_mtlMaterials.push_back(std::move(material));
  }

  std::vector<Vertex> vertices;
  std::vector<uint32_t> indices;
  vertices.reserve(4096);
  indices.reserve(4096);

  auto appendPrimitive = [&](const tg3_primitive &primitive, const glm::mat4 &worldMatrix) {
    const tg3_accessor *positionAccessor = findAccessor(*gltf, primitive, "POSITION");
    if (!positionAccessor) {
      LOG_W("[Mesh] GLB primitive missing POSITION attribute");
      return;
    }

    size_t vertexCount = static_cast<size_t>(positionAccessor->count);
    if (vertexCount == 0)
      return;

    const tg3_accessor *normalAccessor = findAccessor(*gltf, primitive, "NORMAL");
    const tg3_accessor *texcoordAccessor = findAccessor(*gltf, primitive, "TEXCOORD_0");

    glm::mat3 normalMatrix = glm::mat3(glm::transpose(glm::inverse(worldMatrix)));
    size_t primitiveBaseVertex = vertices.size();
    vertices.reserve(vertices.size() + vertexCount);

    for (size_t i = 0; i < vertexCount; ++i) {
      Vertex vertex{};
      vertex.position = readAccessorVec3(*gltf, *positionAccessor, i);
      vertex.position = glm::vec3(worldMatrix * glm::vec4(vertex.position, 1.0f));

      if (normalAccessor) {
        vertex.normal = readAccessorVec3(*gltf, *normalAccessor, i);
        float normalLength = glm::length(vertex.normal);
        if (normalLength > 0.0f) {
          vertex.normal = glm::normalize(normalMatrix * vertex.normal);
        }
      }

      if (texcoordAccessor) {
        vertex.texCoord = readAccessorVec2(*gltf, *texcoordAccessor, i);
      }

      vertices.push_back(vertex);
    }

    std::vector<uint32_t> primitiveIndices;
    if (primitive.indices >= 0 && primitive.indices < static_cast<int32_t>(gltf->accessors_count)) {
      primitiveIndices = readAccessorIndices(*gltf, gltf->accessors[primitive.indices]);
    }
    if (primitiveIndices.empty()) {
      primitiveIndices.reserve(vertexCount);
      for (uint32_t i = 0; i < vertexCount; ++i)
        primitiveIndices.push_back(i);
    }

    uint32_t submeshStart = static_cast<uint32_t>(indices.size());
    auto emitTriangle = [&](uint32_t a, uint32_t b, uint32_t c) {
      indices.push_back(static_cast<uint32_t>(primitiveBaseVertex) + a);
      indices.push_back(static_cast<uint32_t>(primitiveBaseVertex) + b);
      indices.push_back(static_cast<uint32_t>(primitiveBaseVertex) + c);
    };

    int32_t mode = primitive.mode >= 0 ? primitive.mode : TG3_MODE_TRIANGLES;
    switch (mode) {
    case TG3_MODE_TRIANGLES:
      for (size_t i = 0; i + 2 < primitiveIndices.size(); i += 3)
        emitTriangle(primitiveIndices[i], primitiveIndices[i + 1], primitiveIndices[i + 2]);
      break;
    case TG3_MODE_TRIANGLE_STRIP:
      for (size_t i = 2; i < primitiveIndices.size(); ++i) {
        if ((i & 1U) == 0U)
          emitTriangle(primitiveIndices[i - 2], primitiveIndices[i - 1], primitiveIndices[i]);
        else
          emitTriangle(primitiveIndices[i - 1], primitiveIndices[i - 2], primitiveIndices[i]);
      }
      break;
    case TG3_MODE_TRIANGLE_FAN:
      for (size_t i = 2; i < primitiveIndices.size(); ++i)
        emitTriangle(primitiveIndices[0], primitiveIndices[i - 1], primitiveIndices[i]);
      break;
    default:
      LOG_W("[Mesh] Unsupported GLB primitive mode %d", mode);
      break;
    }

    uint32_t submeshCount = static_cast<uint32_t>(indices.size()) - submeshStart;
    if (submeshCount > 0)
      m_submeshes.push_back({submeshStart, submeshCount, primitive.material});
  };

  std::vector<bool> childReferenced(gltf->nodes_count, false);
  for (uint32_t i = 0; i < gltf->nodes_count; ++i) {
    const tg3_node &node = gltf->nodes[i];
    for (uint32_t c = 0; c < node.children_count; ++c) {
      int32_t childIndex = node.children ? node.children[c] : -1;
      if (childIndex >= 0 && childIndex < static_cast<int32_t>(gltf->nodes_count))
        childReferenced[static_cast<size_t>(childIndex)] = true;
    }
  }

  std::function<void(int32_t, const glm::mat4 &)> traverseNode = [&](int32_t nodeIndex, const glm::mat4 &parentMatrix) {
    if (nodeIndex < 0 || nodeIndex >= static_cast<int32_t>(gltf->nodes_count))
      return;

    const tg3_node &node = gltf->nodes[nodeIndex];
    glm::mat4 worldMatrix = parentMatrix * composeNodeTransform(node);

    if (node.mesh >= 0 && node.mesh < static_cast<int32_t>(gltf->meshes_count)) {
      const tg3_mesh &mesh = gltf->meshes[node.mesh];
      for (uint32_t primitiveIndex = 0; primitiveIndex < mesh.primitives_count; ++primitiveIndex)
        appendPrimitive(mesh.primitives[primitiveIndex], worldMatrix);
    }

    for (uint32_t c = 0; c < node.children_count; ++c) {
      int32_t childIndex = node.children ? node.children[c] : -1;
      if (childIndex >= 0)
        traverseNode(childIndex, worldMatrix);
    }
  };

  if (gltf->scenes_count > 0) {
    int32_t sceneIndex = gltf->default_scene >= 0 ? gltf->default_scene : 0;
    if (sceneIndex >= 0 && sceneIndex < static_cast<int32_t>(gltf->scenes_count)) {
      const tg3_scene &scene = gltf->scenes[sceneIndex];
      for (uint32_t i = 0; i < scene.nodes_count; ++i) {
        int32_t nodeIndex = scene.nodes ? scene.nodes[i] : -1;
        if (nodeIndex >= 0)
          traverseNode(nodeIndex, glm::mat4(1.0f));
      }
    }
  } else {
    bool traversedRoot = false;
    for (uint32_t i = 0; i < gltf->nodes_count; ++i) {
      if (!childReferenced[i]) {
        traverseNode(static_cast<int32_t>(i), glm::mat4(1.0f));
        traversedRoot = true;
      }
    }
    if (!traversedRoot) {
      for (uint32_t i = 0; i < gltf->nodes_count; ++i)
        traverseNode(static_cast<int32_t>(i), glm::mat4(1.0f));
    }
  }

  if (vertices.empty() || indices.empty()) {
    LOG_W("[Mesh] GLB produced no drawable geometry: %s", normalized.c_str());
    return false;
  }

  bool needsGeneratedNormals = false;
  for (const auto &vertex : vertices) {
    if (glm::dot(vertex.normal, vertex.normal) == 0.0f) {
      needsGeneratedNormals = true;
      break;
    }
  }

  if (needsGeneratedNormals) {
    std::vector<glm::vec3> accumulatedNormals(vertices.size(), glm::vec3(0.0f));
    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
      uint32_t ia = indices[i];
      uint32_t ib = indices[i + 1];
      uint32_t ic = indices[i + 2];
      if (ia >= vertices.size() || ib >= vertices.size() || ic >= vertices.size())
        continue;

      const Vertex &a = vertices[ia];
      const Vertex &b = vertices[ib];
      const Vertex &c = vertices[ic];
      glm::vec3 faceNormal = glm::cross(b.position - a.position, c.position - a.position);
      float length = glm::length(faceNormal);
      if (length <= 0.0f)
        continue;

      faceNormal /= length;
      if (glm::dot(a.normal, a.normal) == 0.0f)
        accumulatedNormals[ia] += faceNormal;
      if (glm::dot(b.normal, b.normal) == 0.0f)
        accumulatedNormals[ib] += faceNormal;
      if (glm::dot(c.normal, c.normal) == 0.0f)
        accumulatedNormals[ic] += faceNormal;
    }

    for (size_t i = 0; i < vertices.size(); ++i) {
      if (glm::dot(vertices[i].normal, vertices[i].normal) > 0.0f)
        continue;

      float length = glm::length(accumulatedNormals[i]);
      if (length > 0.0f)
        vertices[i].normal = accumulatedNormals[i] / length;
      else
        vertices[i].normal = glm::vec3(0.0f, 1.0f, 0.0f);
    }
  }

  setupBuffers(vertices, indices);
  return true;
}