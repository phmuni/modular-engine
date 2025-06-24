#define STB_IMAGE_IMPLEMENTATION
#include "loader/materialLoader.h"

static const std::vector<std::string> kSupportedExtensions = {".png", ".jpg", ".jpeg", ".bmp", ".tga"};

std::shared_ptr<Material> MaterialLoader::loadMaterial(const std::string &textureDirectory, float shininess) {
  std::filesystem::path baseDir(textureDirectory);

  GLuint diffuseMap = loadTextureFromFile(baseDir / "diffuse", {255, 255, 255});
  GLuint specularMap = loadTextureFromFile(baseDir / "specular", {255, 255, 255});
  GLuint normalMap = loadTextureFromFile(baseDir / "normal", {128, 128, 255});
  GLuint emissionMap = loadTextureFromFile(baseDir / "emission", {0, 0, 0});

  return std::make_shared<Material>(diffuseMap, specularMap, normalMap, emissionMap, shininess);
}

GLuint MaterialLoader::loadTextureFromFile(const std::filesystem::path &filePath,
                                           const std::array<unsigned char, 3> &fallbackColor) {
  std::string foundPath;
  // Tenta cada extensão até encontrar um arquivo existente
  for (auto const &ext : kSupportedExtensions) {
    auto candidate = filePath;
    candidate += ext;
    if (std::filesystem::exists(candidate)) {
      foundPath = candidate.string();
      break;
    }
  }

  GLuint textureID = 0;
  stbi_set_flip_vertically_on_load(true);

  if (!foundPath.empty()) {
    int width, height, channels;
    unsigned char *data = stbi_load(foundPath.c_str(), &width, &height, &channels, 0);
    if (data) {
      glGenTextures(1, &textureID);
      glBindTexture(GL_TEXTURE_2D, textureID);

      GLenum format = (channels == 4 ? GL_RGBA : GL_RGB);
      glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, data);
      glGenerateMipmap(GL_TEXTURE_2D);
      stbi_image_free(data);
    } else {
      std::cerr << "[MaterialLoader] Fail to load " << foundPath << ". Using fallback color.\n";
    }
  } else {
    std::cerr << "[MaterialLoader] File not found in: " << filePath << ".[png|jpg|...]. Using fallback color.\n";
  }

  // If loading failed (textureID is still 0), create a 1x1 texture with fallbackColor
  if (textureID == 0) {
    unsigned char fallbackData[3] = {fallbackColor[0], fallbackColor[1], fallbackColor[2]};
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_2D, textureID);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 1, 1, 0, GL_RGB, GL_UNSIGNED_BYTE, fallbackData);
  }

  // Wrapping and filtering parameters (always applied)
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

  return textureID;
}
