#pragma once
#include <array>
#include <filesystem>
#include <glad/glad.h>
#include <string>


class Material {
private:
  GLuint diffuseMap = 0;
  GLuint specularMap = 0;
  GLuint normalMap = 0;
  GLuint emissionMap = 0;
  float shininess = 32.0f;

  static GLuint loadTextureFromFile(const std::filesystem::path &filePath,
                                    const std::array<unsigned char, 3> &fallbackColor);

public:
  Material() {};

  // Construtor que carrega texturas do diretório
  Material(const std::string &textureDirectory, float shininess = 32.0f);

  // Construtor direto com texturas já carregadas
  Material(GLuint diffuse, GLuint specular, GLuint normal, GLuint emission, float shininess = 32.0f);

  GLuint getDiffuseMap() const;
  GLuint getSpecularMap() const;
  GLuint getNormalMap() const;
  GLuint getEmissionMap() const;
  float getShininess() const;
};