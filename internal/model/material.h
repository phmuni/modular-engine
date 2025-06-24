#pragma once

#include <glad/glad.h>

class Material {
private:
  GLuint diffuseMap = 0;
  GLuint specularMap = 0;
  GLuint normalMap = 0;
  GLuint emissionMap = 0;
  float shininess = 32.0f;

public:
  Material() {};
  Material(GLuint diffuse, GLuint specular, GLuint normal, GLuint emission, float shininess = 32.0f);

  GLuint getDiffuseMap() const;
  GLuint getSpecularMap() const;
  GLuint getNormalMap() const;
  GLuint getEmissionMap() const;
  float getShininess() const;
};
