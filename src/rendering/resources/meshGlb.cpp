// Mesh glTF/GLB loading using tinygltf.

#include "foundation/core/config.h"
#include "foundation/core/logger.h"
#include "rendering/resources/mesh.h"
#include "foundation/core/fileCache.h"
#include "rendering/resources/textureProcessing.h"
#include <future>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <glm/gtc/matrix_transform.hpp>
#include "rendering/resources/skeletonPose.h"
#include <glm/gtc/quaternion.hpp>
#include <stb_image/stb_image.h>
#include <tiny_gltf_loader/tiny_gltf_v3.h>

namespace {

std::string toStdString(const tg3_str &value) {
  if (!value.data || value.len == 0)
    return {};
  return std::string(value.data, value.len);
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

// An accessor resolved and bounds-checked once, so per-element reads are plain pointer arithmetic
struct AccessorView {
  const uint8_t *base = nullptr;
  size_t stride = 0;
  size_t componentCount = 0;
  size_t componentSize = 0;
  int32_t componentType = 0;
  bool normalized = false;
  size_t count = 0;

  bool valid() const { return base != nullptr; }

  template <int N> glm::vec<N, float> read(size_t elementIndex) const {
    glm::vec<N, float> value(0.0f);
    if (!base || elementIndex >= count)
      return value;
    const uint8_t *element = base + elementIndex * stride;
    if (componentType == TG3_COMPONENT_TYPE_FLOAT) {
      for (size_t c = 0; c < static_cast<size_t>(N) && c < componentCount; ++c)
        value[c] = readPod<float>(element + c * sizeof(float));
      return value;
    }
    for (size_t c = 0; c < static_cast<size_t>(N) && c < componentCount; ++c)
      value[c] = readComponentAsFloat(element + c * componentSize, componentType, normalized);
    return value;
  }
};

AccessorView makeAccessorView(const tg3_model &model, const tg3_accessor *accessor) {
  AccessorView view;
  if (!accessor)
    return view;
  view.base = getAccessorBase(model, *accessor, view.stride, view.componentCount);
  view.componentSize = static_cast<size_t>(tg3_component_size(accessor->component_type));
  view.componentType = accessor->component_type;
  view.normalized = accessor->normalized != 0;
  view.count = static_cast<size_t>(accessor->count);
  return view;
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

// glTF image URIs are percent-encoded ("my%20texture.png")
std::string decodeUri(std::string_view uri) {
  std::string result;
  result.reserve(uri.size());
  for (size_t i = 0; i < uri.size(); ++i) {
    if (uri[i] == '%' && i + 2 < uri.size() && std::isxdigit(static_cast<unsigned char>(uri[i + 1])) &&
        std::isxdigit(static_cast<unsigned char>(uri[i + 2]))) {
      result.push_back(static_cast<char>(std::stoi(std::string(uri.substr(i + 1, 2)), nullptr, 16)));
      i += 2;
    } else {
      result.push_back(uri[i]);
    }
  }
  return result;
}

// Relative to the model's folder, like OBJ texture paths: ResourceSystem::loadModel prepends the base directory
std::string textureFileUri(const tg3_model &model, int32_t textureIndex) {
  if (textureIndex < 0 || textureIndex >= static_cast<int32_t>(model.textures_count))
    return {};

  const tg3_texture &texture = model.textures[textureIndex];
  if (texture.source < 0 || texture.source >= static_cast<int32_t>(model.images_count))
    return {};

  const tg3_image &image = model.images[texture.source];
  if (image.uri.len == 0 || tg3_is_data_uri(image.uri.data, image.uri.len))
    return {};

  return decodeUri(std::string_view(image.uri.data, image.uri.len));
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

// A node's place relative to its parent, as translation, rotation and scale (what animations change)
NodePose nodePose(const tg3_node &node) {
  NodePose pose;
  if (node.has_matrix) {
    const glm::mat4 matrix = composeNodeTransform(node);
    pose.translation = glm::vec3(matrix[3]);
    for (int axis = 0; axis < 3; ++axis)
      pose.scale[axis] = glm::length(glm::vec3(matrix[axis]));
    glm::mat3 rotation;
    for (int axis = 0; axis < 3; ++axis)
      rotation[axis] = glm::vec3(matrix[axis]) / (pose.scale[axis] > 0.0f ? pose.scale[axis] : 1.0f);
    pose.rotation = glm::normalize(glm::quat_cast(rotation));
    return pose;
  }
  pose.translation = glm::vec3(static_cast<float>(node.translation[0]), static_cast<float>(node.translation[1]),
                               static_cast<float>(node.translation[2]));
  pose.rotation = glm::normalize(glm::quat(static_cast<float>(node.rotation[3]), static_cast<float>(node.rotation[0]),
                                           static_cast<float>(node.rotation[1]), static_cast<float>(node.rotation[2])));
  pose.scale = glm::vec3(static_cast<float>(node.scale[0]), static_cast<float>(node.scale[1]),
                         static_cast<float>(node.scale[2]));
  return pose;
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

// Decodes an embedded image (already decoded by tinygltf, or PNG/JPEG bytes) to bottom-row-first pixels, like every
// other texture in the engine; texcoords get v -> 1 - v to match
DecodedImage decodeImage(const tg3_model &model, const tg3_image &image) {
  DecodedImage decoded;

  if (image.image.data && image.image.count > 0 && image.as_is == 0) {
    decoded.width = image.width;
    decoded.height = image.height;
    decoded.channels = image.component;
    switch (image.pixel_type) {
    case TG3_COMPONENT_TYPE_UNSIGNED_BYTE: decoded.type = GL_UNSIGNED_BYTE; break;
    case TG3_COMPONENT_TYPE_UNSIGNED_SHORT: decoded.type = GL_UNSIGNED_SHORT; break;
    case TG3_COMPONENT_TYPE_FLOAT: decoded.type = GL_FLOAT; break;
    default: return {};
    }
    const size_t rowBytes = image.image.count / static_cast<size_t>(std::max(image.height, 1));
    decoded.pixels.resize(image.image.count);
    for (int y = 0; y < image.height; ++y)
      std::memcpy(decoded.pixels.data() + static_cast<size_t>(y) * rowBytes,
                  image.image.data + static_cast<size_t>(image.height - 1 - y) * rowBytes, rowBytes);
    return decoded;
  }

  std::vector<uint8_t> encoded = extractImageBytes(model, image);
  if (encoded.empty())
    return decoded;

  stbi_set_flip_vertically_on_load_thread(1);
  int width = 0, height = 0, channels = 0;
  stbi_uc *pixels = stbi_load_from_memory(encoded.data(), static_cast<int>(encoded.size()), &width, &height, &channels, 0);
  if (!pixels)
    return decoded;

  decoded.width = width;
  decoded.height = height;
  decoded.channels = channels;
  decoded.pixels.assign(pixels, pixels + static_cast<size_t>(width) * height * channels);
  stbi_image_free(pixels);
  return decoded;
}

} // namespace

bool Mesh::parseGLB(std::string filename, MeshData &data) {

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
  data.baseDir = basePath.empty() ? std::string("./") : PathUtils::normalizeSeparators(basePath.string()) + "/";

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
                                              data.baseDir.c_str(), static_cast<uint32_t>(data.baseDir.size()), &options);
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

  // Extensions this loader understands; anything else in extensionsRequired would load as broken geometry or
  // materials (e.g. Draco- or meshopt-compressed buffers), so refuse those files with a clear message.
  static constexpr const char *kSupportedExtensions[] = {"KHR_materials_specular", "KHR_materials_unlit",
                                                         "KHR_mesh_quantization"};
  auto isSupported = [](const tg3_str &name) {
    for (const char *supported : kSupportedExtensions)
      if (tg3_str_equals_cstr(name, supported))
        return true;
    return false;
  };
  for (uint32_t i = 0; i < gltf->extensions_required_count; ++i) {
    if (!isSupported(gltf->extensions_required[i])) {
      LOG_E("[Mesh] %s requires the unsupported glTF extension %s", normalized.c_str(),
            toStdString(gltf->extensions_required[i]).c_str());
      return false;
    }
  }
  for (uint32_t i = 0; i < gltf->extensions_used_count; ++i) {
    if (!isSupported(gltf->extensions_used[i]))
      LOG_W("[Mesh] %s uses the glTF extension %s, which is ignored", normalized.c_str(),
            toStdString(gltf->extensions_used[i]).c_str());
  }
  // Images used as normal maps are processed as normals (BC5), the rest as colors
  std::vector<TextureKind> imageKinds(gltf->images_count, TextureKind::Color);
  for (uint32_t i = 0; i < gltf->materials_count; ++i) {
    const int32_t normal = gltf->materials[i].normal_texture.index;
    if (normal >= 0 && normal < static_cast<int32_t>(gltf->textures_count)) {
      const int32_t source = gltf->textures[normal].source;
      if (source >= 0 && source < static_cast<int32_t>(gltf->images_count))
        imageKinds[static_cast<size_t>(source)] = TextureKind::Normal;
    }
  }

  // Each image is decoded (or read from the texture cache) on its own thread
  std::vector<std::future<DecodedImage>> decoding;
  for (uint32_t i = 0; i < gltf->images_count; ++i)
    decoding.push_back(std::async(std::launch::async, [&, i] {
      return TextureProcessing::load(embeddedImageKey(normalized, i), normalized, imageKinds[i],
                                     [&] { return decodeImage(*gltf, gltf->images[i]); });
    }));
  data.images.resize(gltf->images_count);
  for (uint32_t i = 0; i < gltf->images_count; ++i)
    data.images[i] = decoding[i].get();

  data.materials.reserve(gltf->materials_count);
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
    const double *factor = source.pbr_metallic_roughness.base_color_factor;
    material.baseColorFactor = {static_cast<float>(factor[0]), static_cast<float>(factor[1]),
                                static_cast<float>(factor[2]), static_cast<float>(factor[3])};
    if (tg3_str_equals_cstr(source.alpha_mode, "MASK"))
      material.alphaMode = AlphaMode::Mask;
    else if (tg3_str_equals_cstr(source.alpha_mode, "BLEND"))
      material.alphaMode = AlphaMode::Blend;
    material.alphaCutoff = static_cast<float>(source.alpha_cutoff);
    material.doubleSided = source.double_sided != 0;
    material.unlit = findExtension(source.ext, "KHR_materials_unlit") != nullptr;
    if (source.pbr_metallic_roughness.base_color_texture.index >= 0 &&
        source.pbr_metallic_roughness.base_color_texture.index < static_cast<int32_t>(gltf->textures_count)) {
      const tg3_texture &texture = gltf->textures[source.pbr_metallic_roughness.base_color_texture.index];
      if (texture.source >= 0 && texture.source < static_cast<int32_t>(data.images.size()) && !data.images[texture.source].empty()) {
        material.diffuseImage = texture.source;
      } else {
        material.diffuseTexPath = textureFileUri(*gltf, source.pbr_metallic_roughness.base_color_texture.index);
      }
    }
    if (source.normal_texture.index >= 0 && source.normal_texture.index < static_cast<int32_t>(gltf->textures_count)) {
      const tg3_texture &texture = gltf->textures[source.normal_texture.index];
      if (texture.source >= 0 && texture.source < static_cast<int32_t>(data.images.size()) && !data.images[texture.source].empty()) {
        material.normalImage = texture.source;
      } else {
        material.normalTexPath = textureFileUri(*gltf, source.normal_texture.index);
      }
    }
    if (source.emissive_texture.index >= 0 && source.emissive_texture.index < static_cast<int32_t>(gltf->textures_count)) {
      const tg3_texture &texture = gltf->textures[source.emissive_texture.index];
      if (texture.source >= 0 && texture.source < static_cast<int32_t>(data.images.size()) && !data.images[texture.source].empty()) {
        material.emissionImage = texture.source;
      } else {
        material.emissionTexPath = textureFileUri(*gltf, source.emissive_texture.index);
      }
    }

    if (const tg3_extension *specularExt = findExtension(source.ext, "KHR_materials_specular")) {
      const tg3_value *specularValue = &specularExt->value;
      int32_t specularTextureIndex = readNestedTextureIndex(specularValue, "specularTexture", -1);
      if (specularTextureIndex >= 0 && specularTextureIndex < static_cast<int32_t>(gltf->textures_count)) {
        const tg3_texture &texture = gltf->textures[specularTextureIndex];
        if (texture.source >= 0 && texture.source < static_cast<int32_t>(data.images.size()) && !data.images[texture.source].empty()) {
          material.specularImage = texture.source;
        } else {
          material.specularTexPath = textureFileUri(*gltf, specularTextureIndex);
        }
      }
    }

    data.materials.push_back(std::move(material));
  }

  // Animated models (skins or animations): the node tree becomes a skeleton, and vertices stay in the space of the
  // joints that move them instead of being placed once
  const bool animated = gltf->skins_count > 0 || gltf->animations_count > 0;
  std::shared_ptr<MeshAnimation> animation;
  std::vector<int> rigidJoint(gltf->nodes_count, -1); // the joint a node's unskinned parts follow
  std::vector<int> skinJointOffset(gltf->skins_count, 0);
  if (animated) {
    animation = std::make_shared<MeshAnimation>();
    Skeleton &skeleton = animation->skeleton;
    skeleton.nodes.resize(gltf->nodes_count);
    for (uint32_t i = 0; i < gltf->nodes_count; ++i) {
      const tg3_node &node = gltf->nodes[i];
      const NodePose pose = nodePose(node);
      skeleton.nodes[i].name = toStdString(node.name);
      skeleton.nodes[i].translation = pose.translation;
      skeleton.nodes[i].rotation = pose.rotation;
      skeleton.nodes[i].scale = pose.scale;
    }
    for (uint32_t i = 0; i < gltf->nodes_count; ++i) {
      const tg3_node &node = gltf->nodes[i];
      for (uint32_t c = 0; c < node.children_count; ++c) {
        const int32_t child = node.children ? node.children[c] : -1;
        if (child >= 0 && child < static_cast<int32_t>(gltf->nodes_count))
          skeleton.nodes[child].parent = static_cast<int>(i);
      }
    }
    // Parents before children: breadth first from the roots
    for (uint32_t i = 0; i < gltf->nodes_count; ++i)
      if (skeleton.nodes[i].parent < 0)
        skeleton.order.push_back(static_cast<int>(i));
    for (size_t k = 0; k < skeleton.order.size() && skeleton.order.size() <= gltf->nodes_count; ++k)
      for (uint32_t i = 0; i < gltf->nodes_count; ++i)
        if (skeleton.nodes[i].parent == skeleton.order[k])
          skeleton.order.push_back(static_cast<int>(i));

    for (uint32_t s = 0; s < gltf->skins_count; ++s) {
      const tg3_skin &skin = gltf->skins[s];
      skinJointOffset[s] = static_cast<int>(skeleton.jointNodes.size());
      const tg3_accessor *inverseBinds = skin.inverse_bind_matrices >= 0 &&
                                                 skin.inverse_bind_matrices < static_cast<int32_t>(gltf->accessors_count)
                                             ? &gltf->accessors[skin.inverse_bind_matrices]
                                             : nullptr;
      const AccessorView binds = makeAccessorView(*gltf, inverseBinds);
      for (uint32_t j = 0; j < skin.joints_count; ++j) {
        skeleton.jointNodes.push_back(skin.joints ? skin.joints[j] : 0);
        glm::mat4 inverseBind(1.0f);
        if (binds.valid() && j < binds.count && binds.componentType == TG3_COMPONENT_TYPE_FLOAT) {
          const uint8_t *element = binds.base + j * binds.stride;
          for (int k = 0; k < 16; ++k)
            inverseBind[k / 4][k % 4] = readPod<float>(element + k * sizeof(float));
        }
        skeleton.inverseBind.push_back(inverseBind);
      }
    }

    // Clips: channels of translation, rotation and scale (morph weights are not supported)
    for (uint32_t a = 0; a < gltf->animations_count; ++a) {
      const tg3_animation &source = gltf->animations[a];
      AnimationClip clip;
      clip.name = toStdString(source.name);
      if (clip.name.empty())
        clip.name = "animation " + std::to_string(a);
      for (uint32_t c = 0; c < source.channels_count; ++c) {
        const tg3_animation_channel &channelSource = source.channels[c];
        const std::string path = toStdString(channelSource.target.path);
        if (channelSource.sampler < 0 || channelSource.sampler >= static_cast<int32_t>(source.samplers_count) ||
            channelSource.target.node < 0 || channelSource.target.node >= static_cast<int32_t>(gltf->nodes_count) ||
            (path != "translation" && path != "rotation" && path != "scale"))
          continue;
        const tg3_animation_sampler &sampler = source.samplers[channelSource.sampler];
        if (sampler.input < 0 || sampler.output < 0 || sampler.input >= static_cast<int32_t>(gltf->accessors_count) ||
            sampler.output >= static_cast<int32_t>(gltf->accessors_count))
          continue;
        AnimationChannel channel;
        channel.node = channelSource.target.node;
        channel.path = path == "translation" ? AnimationChannel::Path::Translation
                       : path == "rotation"  ? AnimationChannel::Path::Rotation
                                             : AnimationChannel::Path::Scale;
        const std::string interpolation = toStdString(sampler.interpolation);
        channel.interpolation = interpolation == "STEP"          ? AnimationChannel::Interpolation::Step
                                : interpolation == "CUBICSPLINE" ? AnimationChannel::Interpolation::CubicSpline
                                                                 : AnimationChannel::Interpolation::Linear;
        const AccessorView times = makeAccessorView(*gltf, &gltf->accessors[sampler.input]);
        const AccessorView values = makeAccessorView(*gltf, &gltf->accessors[sampler.output]);
        for (size_t k = 0; k < times.count; ++k)
          channel.times.push_back(times.read<1>(k).x);
        for (size_t k = 0; k < values.count; ++k)
          channel.values.push_back(values.read<4>(k));
        const size_t perKey = channel.interpolation == AnimationChannel::Interpolation::CubicSpline ? 3 : 1;
        if (channel.times.empty() || channel.values.size() < channel.times.size() * perKey)
          continue;
        clip.duration = std::max(clip.duration, channel.times.back());
        clip.channels.push_back(std::move(channel));
      }
      animation->clips.push_back(std::move(clip));
    }
  }
  // An unskinned mesh node of an animated model becomes a joint of its own
  auto jointForNode = [&](int32_t nodeIndex) {
    if (rigidJoint[nodeIndex] < 0) {
      rigidJoint[nodeIndex] = static_cast<int>(animation->skeleton.jointNodes.size());
      animation->skeleton.jointNodes.push_back(nodeIndex);
      animation->skeleton.inverseBind.push_back(glm::mat4(1.0f));
    }
    return rigidJoint[nodeIndex];
  };

  std::vector<Vertex> &vertices = data.vertices;
  std::vector<uint32_t> &indices = data.indices;
  vertices.reserve(4096);
  indices.reserve(4096);

  std::vector<PendingPart> parts;

  // skinIndex >= 0: skinned by that skin; else rigidJointIndex >= 0: follows that joint; both -1: a static model
  auto appendPrimitive = [&](const tg3_primitive &primitive, const glm::mat4 &worldMatrix, std::string name,
                             int skinIndex, int rigidJointIndex) {
    const tg3_accessor *positionAccessor = findAccessor(*gltf, primitive, "POSITION");
    if (!positionAccessor) {
      LOG_W("[Mesh] GLB primitive missing POSITION attribute");
      return;
    }

    size_t vertexCount = static_cast<size_t>(positionAccessor->count);
    if (vertexCount == 0)
      return;

    const AccessorView positions = makeAccessorView(*gltf, positionAccessor);
    const AccessorView normals = makeAccessorView(*gltf, findAccessor(*gltf, primitive, "NORMAL"));
    const AccessorView texcoords = makeAccessorView(*gltf, findAccessor(*gltf, primitive, "TEXCOORD_0"));
    const AccessorView tangents = makeAccessorView(*gltf, findAccessor(*gltf, primitive, "TANGENT"));
    const AccessorView joints = makeAccessorView(*gltf, findAccessor(*gltf, primitive, "JOINTS_0"));
    const AccessorView weights = makeAccessorView(*gltf, findAccessor(*gltf, primitive, "WEIGHTS_0"));
    // A mirroring node transform flips which side the bitangent is on
    const float mirror = glm::determinant(glm::mat3(worldMatrix)) < 0.0f ? -1.0f : 1.0f;

    glm::mat3 normalMatrix = glm::mat3(glm::transpose(glm::inverse(worldMatrix)));
    size_t primitiveBaseVertex = vertices.size();
    vertices.reserve(vertices.size() + vertexCount);

    for (size_t i = 0; i < vertexCount; ++i) {
      Vertex vertex{};
      vertex.position = glm::vec3(worldMatrix * glm::vec4(positions.read<3>(i), 1.0f));

      if (normals.valid()) {
        vertex.normal = normals.read<3>(i);
        float normalLength = glm::length(vertex.normal);
        if (normalLength > 0.0f) {
          vertex.normal = glm::normalize(normalMatrix * vertex.normal);
        }
      }

      if (texcoords.valid()) {
        // glTF puts the UV origin at the top-left of the image; the engine's images start at the bottom
        vertex.texCoord = texcoords.read<2>(i);
        vertex.texCoord.y = 1.0f - vertex.texCoord.y;
      }

      if (tangents.valid()) {
        const glm::vec4 tangent = tangents.read<4>(i);
        const glm::vec3 direction = glm::mat3(worldMatrix) * glm::vec3(tangent);
        if (glm::dot(direction, direction) > 0.0f)
          vertex.tangent = glm::vec4(glm::normalize(direction), (tangent.w < 0.0f ? -1.0f : 1.0f) * mirror);
      }

      vertices.push_back(vertex);

      if (animation) {
        VertexSkin skin;
        if (skinIndex >= 0 && joints.valid() && weights.valid()) {
          skin.joints = joints.read<4>(i) + glm::vec4(static_cast<float>(skinJointOffset[skinIndex]));
          skin.weights = weights.read<4>(i);
          const float total = skin.weights.x + skin.weights.y + skin.weights.z + skin.weights.w;
          skin.weights = total > 0.0f ? skin.weights / total : glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
        } else {
          skin.joints = glm::vec4(static_cast<float>(std::max(rigidJointIndex, 0)), 0.0f, 0.0f, 0.0f);
          skin.weights = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
        }
        data.skin.push_back(skin);
      }
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

    PendingPart part;
    part.name = std::move(name);
    part.materialIndex = primitive.material;
    auto emitTriangle = [&](uint32_t a, uint32_t b, uint32_t c) {
      part.indices.push_back(static_cast<uint32_t>(primitiveBaseVertex) + a);
      part.indices.push_back(static_cast<uint32_t>(primitiveBaseVertex) + b);
      part.indices.push_back(static_cast<uint32_t>(primitiveBaseVertex) + c);
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

    if (!part.indices.empty())
      parts.push_back(std::move(part));
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
      // Part names: the node's name, else the mesh's, plus the primitive index when the mesh has several
      std::string name = toStdString(node.name);
      if (name.empty())
        name = toStdString(mesh.name);
      if (name.empty())
        name = "node " + std::to_string(nodeIndex);
      // Animated models keep vertices where their joints are: a skin's in the mesh's space (glTF ignores a skinned
      // mesh node's transform), an unskinned part in its node's, which it follows as a joint
      const bool skinned = animation && node.skin >= 0 && node.skin < static_cast<int32_t>(gltf->skins_count);
      const int rigid = animation && !skinned ? jointForNode(nodeIndex) : -1;
      const glm::mat4 placement = animation ? glm::mat4(1.0f) : worldMatrix;
      for (uint32_t primitiveIndex = 0; primitiveIndex < mesh.primitives_count; ++primitiveIndex) {
        std::string partName = mesh.primitives_count > 1 ? name + "/" + std::to_string(primitiveIndex) : name;
        appendPrimitive(mesh.primitives[primitiveIndex], placement, std::move(partName), skinned ? node.skin : -1,
                        rigid);
      }
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

  layoutParts(parts, data);

  if (animation) {
    // The rest pose, for bounds, picking and entities without an Animator
    std::vector<glm::mat4> globals;
    SkeletonPose::jointMatrices(animation->skeleton, SkeletonPose::rest(animation->skeleton), globals,
                                animation->restJointMatrices);
    if (animation->skeleton.jointNodes.size() > EngineConfig::MAX_JOINTS)
      LOG_W("[Mesh] %s has %zu joints; only %d are drawn moving", normalized.c_str(),
            animation->skeleton.jointNodes.size(), EngineConfig::MAX_JOINTS);
    data.animation = std::move(animation);
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

  generateTangents(data);
  return true;
}
