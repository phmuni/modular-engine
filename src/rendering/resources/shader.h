#pragma once
// Shader class for loading, compiling, and using GLSL shader programs.

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

class Shader {
private:
  GLuint m_shaderID = 0;
  struct CachedUniform {
    std::string name;
    GLint location;
  };
  // Keyed by the name's hash, so lookups never allocate; the stored name guards against collisions.
  mutable std::unordered_map<size_t, CachedUniform> m_uniformCache;
  mutable int m_instancingSupport = -1; // -1 unknown, 0 no, 1 yes

  std::string readShaderFile(const char *filename) const;
  GLuint compileShader(GLenum type, const char *filename, std::string_view defines);
  GLuint createShaderProgram(const char *vertexFile, const char *fragmentFile, std::string_view defines);
  GLint getUniformLocation(std::string_view name) const;

public:
  Shader() = default;
  // `defines` (e.g. "#define ALPHA_MASK\n") is inserted after each file's #version line, to build shader variants
  Shader(std::string_view vertexFile, std::string_view fragmentFile, std::string_view defines = {});
  ~Shader() {
    if (m_shaderID)
      glDeleteProgram(m_shaderID);
  }

  Shader(const Shader &) = delete;
  Shader &operator=(const Shader &) = delete;

  Shader(Shader &&other) noexcept
      : m_shaderID(std::exchange(other.m_shaderID, 0)), m_uniformCache(std::move(other.m_uniformCache)),
        m_instancingSupport(other.m_instancingSupport) {}

  Shader &operator=(Shader &&other) noexcept {
    if (this != &other) {
      if (m_shaderID)
        glDeleteProgram(m_shaderID);
      m_shaderID = std::exchange(other.m_shaderID, 0);
      m_uniformCache = std::move(other.m_uniformCache);
      m_instancingSupport = other.m_instancingSupport;
    }
    return *this;
  }

  bool load(std::string_view vertexFile, std::string_view fragmentFile);
  void use() const;

  void setInt(std::string_view name, int value) const;
  void setFloat(std::string_view name, float value) const;
  void setVec3(std::string_view name, glm::vec3 value) const;
  void setVec4(std::string_view name, glm::vec4 value) const;
  void setMat3(std::string_view name, glm::mat3 value) const;
  void setMat4(std::string_view name, glm::mat4 value) const;
  void setTex(std::string_view name, GLuint, int) const;

  GLuint getShaderID() const;

  // True when the vertex shader reads per-instance model matrices (aInstanceModel at location 3)
  bool supportsInstancing() const;
};