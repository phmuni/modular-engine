#pragma once
// Shader class for loading, compiling, and using GLSL shader programs.

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

// A custom surface shader's code for the two stages (see customShader.h), put where each stage's file has the marker
// `//@CUSTOM_SURFACE_CODE`. `label` names the user's file in compile errors.
struct ShaderInjection {
  std::string label;
  std::string vertexCode;
  std::string fragmentCode;
};

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
  GLuint compileShader(GLenum type, const char *filename, std::string_view defines, const std::string *injected,
                       const std::string &label);
  GLuint createShaderProgram(const char *vertexFile, const char *fragmentFile, std::string_view defines,
                             const ShaderInjection *injection = nullptr);
  GLint getUniformLocation(std::string_view name) const;

public:
  Shader() = default;
  // `defines` (e.g. "#define ALPHA_MASK\n") is inserted after each file's #version line, to build shader variants
  Shader(std::string_view vertexFile, std::string_view fragmentFile, std::string_view defines = {});
  // The same with a custom surface shader's code injected; a program that fails to compile has no ID (getShaderID() 0)
  Shader(std::string_view vertexFile, std::string_view fragmentFile, std::string_view defines,
         const ShaderInjection &injection);
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
  void setVec2(std::string_view name, glm::vec2 value) const;
  void setVec3(std::string_view name, glm::vec3 value) const;
  void setVec4(std::string_view name, glm::vec4 value) const;
  void setMat3(std::string_view name, glm::mat3 value) const;
  void setMat4(std::string_view name, glm::mat4 value) const;
  // A uniform array of matrices from its first element: setMat4Array("bones", data, count)
  void setMat4Array(std::string_view name, const glm::mat4 *values, int count) const;
  // Binds a texture to `textureUnit` for the sampler `name`; `target` is GL_TEXTURE_CUBE_MAP for cube samplers,
  // GL_TEXTURE_2D_ARRAY for array samplers
  void setTex(std::string_view name, GLuint textureID, int textureUnit, GLenum target = GL_TEXTURE_2D,
              bool warnIfMissing = true) const;

  GLuint getShaderID() const;

  // True when the vertex shader reads per-instance model matrices (aInstanceModel at location 3)
  bool supportsInstancing() const;
};