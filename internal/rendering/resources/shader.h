#pragma once
// Shader class for loading, compiling, and using GLSL shader programs.

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <map>

class Shader {
private:
  GLuint m_shaderID = 0;
  mutable std::map<std::string, GLint> m_uniformCache;

  std::string readShaderFile(const char *filename) const;
  GLuint compileShader(GLenum type, const char *filename);
  GLuint createShaderProgram(const char *vertexFile, const char *fragmentFile);
  GLint getUniformLocation(const char *name) const;

public:
  Shader() = default;
  Shader(std::string_view vertexFile, std::string_view fragmentFile);
  ~Shader() {
    if (m_shaderID)
      glDeleteProgram(m_shaderID);
  }

  Shader(const Shader &) = delete;
  Shader &operator=(const Shader &) = delete;

  Shader(Shader &&other) noexcept
      : m_shaderID(std::exchange(other.m_shaderID, 0)), m_uniformCache(std::move(other.m_uniformCache)) {}

  Shader &operator=(Shader &&other) noexcept {
    if (this != &other) {
      if (m_shaderID)
        glDeleteProgram(m_shaderID);
      m_shaderID = std::exchange(other.m_shaderID, 0);
      m_uniformCache = std::move(other.m_uniformCache);
    }
    return *this;
  }

  bool load(std::string_view vertexFile, std::string_view fragmentFile);
  void use() const;

  void setInt(std::string_view name, int value) const;
  void setFloat(std::string_view name, float value) const;
  void setVec3(std::string_view name, glm::vec3 value) const;
  void setMat3(std::string_view name, glm::mat3 value) const;
  void setMat4(std::string_view name, glm::mat4 value) const;
  void setTex(std::string_view name, GLuint, int) const;

  GLuint getShaderID() const;
};