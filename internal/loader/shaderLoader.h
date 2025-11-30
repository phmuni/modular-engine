#pragma once

// External headers
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <string>

class Shader {
private:
  GLuint shaderID;

  std::string readShaderFile(const char *filename) const;
  GLuint compileShader(GLenum type, const char *filename);
  GLuint createShaderProgram(const char *vertexShaderFile, const char *fragmentShaderFile);

public:
  Shader(const char *vertexShaderFile, const char *fragmentShaderFile);

  bool load(const char *vertexShaderFile, const char *fragmentShaderFile);
  void use() const;

  void setTex(const char *name, GLuint textureID, int textureUnit) const;
  void setInt(const char *name, int value) const;
  void setFloat(const char *name, float value) const;
  void setVec3(const char *name, glm::vec3 value) const;
  void setMat3(const char *name, glm::mat3 value) const;
  void setMat4(const char *name, glm::mat4 value) const;

  GLuint getShaderID() const;
};
