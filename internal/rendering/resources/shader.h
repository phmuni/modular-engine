#pragma once
// OpenGL shader program wrapper with uniform setters.

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <string>

class Shader {
private:
  GLuint m_shaderID = 0;

  std::string readShaderFile(const char *filename) const;
  GLuint compileShader(GLenum type, const char *filename);
  GLuint createShaderProgram(const char *vertexFile, const char *fragmentFile);

public:
  Shader() = default;
  Shader(const char *vertexFile, const char *fragmentFile);
  ~Shader() {
    if (m_shaderID)
      glDeleteProgram(m_shaderID);
  }

  // Prevent copy, allow move
  Shader(const Shader &) = delete;
  Shader &operator=(const Shader &) = delete;
  Shader(Shader &&) = default;
  Shader &operator=(Shader &&) = default;

  bool load(const char *vertexFile, const char *fragmentFile);
  void use() const;

  void setTex(const char *name, GLuint textureID, int textureUnit) const;
  void setInt(const char *name, int value) const;
  void setFloat(const char *name, float value) const;
  void setVec3(const char *name, glm::vec3 value) const;
  void setMat3(const char *name, glm::mat3 value) const;
  void setMat4(const char *name, glm::mat4 value) const;

  GLuint getShaderID() const;
};
