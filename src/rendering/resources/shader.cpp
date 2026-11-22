
// Shader management class for OpenGL. Handles loading, compiling, and using vertex and fragment shaders.

#include "rendering/resources/shader.h"
#include "foundation/core/logger.h"
#include <fstream>
#include <glm/gtc/type_ptr.hpp>
#include <sstream>
#include <vector>

Shader::Shader(std::string_view vertexFile, std::string_view fragmentFile, std::string_view defines) {
  m_shaderID = createShaderProgram(vertexFile.data(), fragmentFile.data(), defines);
}

Shader::Shader(std::string_view vertexFile, std::string_view fragmentFile, std::string_view defines,
               const ShaderInjection &injection) {
  m_shaderID = createShaderProgram(vertexFile.data(), fragmentFile.data(), defines, &injection);
}

bool Shader::load(std::string_view vertexFile, std::string_view fragmentFile) {
  if (m_shaderID) {
    glDeleteProgram(m_shaderID);
    m_shaderID = 0;
    m_uniformCache.clear();
    m_instancingSupport = -1;
  }
  m_shaderID = createShaderProgram(vertexFile.data(), fragmentFile.data(), {});
  return m_shaderID != 0;
}

GLint Shader::getUniformLocation(std::string_view name) const {
  const size_t key = std::hash<std::string_view>{}(name);
  auto it = m_uniformCache.find(key);
  if (it != m_uniformCache.end() && it->second.name == name)
    return it->second.location;

  std::string owned(name); // glGetUniformLocation needs a null-terminated string
  GLint loc = glGetUniformLocation(m_shaderID, owned.c_str());
  if (it == m_uniformCache.end())
    m_uniformCache.emplace(key, CachedUniform{std::move(owned), loc});
  return loc;
}

std::string Shader::readShaderFile(const char *filename) const {
  std::ifstream file(filename);
  if (!file.is_open()) {
    LOG_E("[Shader] Cannot open: %s", filename);
    return {};
  }
  std::ostringstream buffer;
  buffer << file.rdbuf();
  return buffer.str();
}

GLuint Shader::compileShader(GLenum type, const char *filename, std::string_view defines, const std::string *injected,
                             const std::string &label) {
  std::string source = readShaderFile(filename);
  if (source.empty())
    return 0;

  if (!defines.empty()) {
    size_t afterVersion = source.find('\n', source.find("#version"));
    source.insert(afterVersion == std::string::npos ? 0 : afterVersion + 1, defines);
  }

  if (injected) {
    // The user's code as its own source string, so line numbers in errors ("1:12") are lines of their file
    static const std::string marker = "//@CUSTOM_SURFACE_CODE";
    const size_t at = source.find(marker);
    if (at != std::string::npos)
      source.replace(at, marker.size(), "#line 1 1\n" + *injected + "\n#line 1 0\n");
  }

  const char *src = source.c_str();
  GLuint shader = glCreateShader(type);
  glShaderSource(shader, 1, &src, nullptr);
  glCompileShader(shader);

  GLint success;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
  if (!success) {
    GLint logLength;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLength);
    std::vector<char> log(logLength);
    glGetShaderInfoLog(shader, logLength, nullptr, log.data());
    if (injected)
      LOG_E("[Shader] Compilation error in %s (\"1:N\" in the messages is line N of %s):\n%s", filename,
            label.c_str(), log.data());
    else
      LOG_E("[Shader] Compilation error in %s:\n%s", filename, log.data());
    glDeleteShader(shader);
    return 0;
  }
  return shader;
}

GLuint Shader::createShaderProgram(const char *vertexFile, const char *fragmentFile, std::string_view defines,
                                   const ShaderInjection *injection) {
  const std::string noLabel;
  GLuint vert = compileShader(GL_VERTEX_SHADER, vertexFile, defines, injection ? &injection->vertexCode : nullptr,
                              injection ? injection->label : noLabel);
  GLuint frag = compileShader(GL_FRAGMENT_SHADER, fragmentFile, defines, injection ? &injection->fragmentCode : nullptr,
                              injection ? injection->label : noLabel);

  if (!vert || !frag) {
    if (vert)
      glDeleteShader(vert);
    if (frag)
      glDeleteShader(frag);
    return 0;
  }

  GLuint program = glCreateProgram();
  glAttachShader(program, vert);
  glAttachShader(program, frag);
  glLinkProgram(program);

  GLint success;
  glGetProgramiv(program, GL_LINK_STATUS, &success);
  if (!success) {
    GLint logLength;
    glGetProgramiv(program, GL_INFO_LOG_LENGTH, &logLength);
    std::vector<char> log(logLength);
    glGetProgramInfoLog(program, logLength, nullptr, log.data());
    LOG_E("[Shader] Linking error%s%s:\n%s", injection ? " in " : "", injection ? injection->label.c_str() : "",
          log.data());
    glDeleteShader(vert);
    glDeleteShader(frag);
    glDeleteProgram(program);
    return 0;
  }

  glDeleteShader(vert);
  glDeleteShader(frag);
  return program;
}

void Shader::use() const { glUseProgram(m_shaderID); }

GLuint Shader::getShaderID() const { return m_shaderID; }

bool Shader::supportsInstancing() const {
  if (m_instancingSupport < 0)
    m_instancingSupport = glGetAttribLocation(m_shaderID, "aInstanceModel") == 3 ? 1 : 0;
  return m_instancingSupport == 1;
}

void Shader::setTex(std::string_view name, GLuint textureID, int textureUnit, GLenum target,
                    bool warnIfMissing) const {
  glActiveTexture(GL_TEXTURE0 + textureUnit);
  glBindTexture(target, textureID);
  GLint loc = getUniformLocation(name);
  if (loc != -1)
    glUniform1i(loc, textureUnit);
  else if (warnIfMissing)
    LOG_W("[Shader] Uniform not found: %.*s", static_cast<int>(name.size()), name.data());
}

void Shader::setInt(std::string_view name, int value) const {
  GLint loc = getUniformLocation(name);
  if (loc != -1)
    glUniform1i(loc, value);
}

void Shader::setFloat(std::string_view name, float value) const {
  GLint loc = getUniformLocation(name);
  if (loc != -1)
    glUniform1f(loc, value);
}

void Shader::setVec2(std::string_view name, glm::vec2 value) const {
  GLint loc = getUniformLocation(name);
  if (loc != -1)
    glUniform2fv(loc, 1, glm::value_ptr(value));
}

void Shader::setVec3(std::string_view name, glm::vec3 value) const {
  GLint loc = getUniformLocation(name);
  if (loc != -1)
    glUniform3fv(loc, 1, glm::value_ptr(value));
}

void Shader::setVec4(std::string_view name, glm::vec4 value) const {
  GLint loc = getUniformLocation(name);
  if (loc != -1)
    glUniform4fv(loc, 1, glm::value_ptr(value));
}

void Shader::setMat3(std::string_view name, glm::mat3 value) const {
  GLint loc = getUniformLocation(name);
  if (loc != -1)
    glUniformMatrix3fv(loc, 1, GL_FALSE, glm::value_ptr(value));
}

void Shader::setMat4Array(std::string_view name, const glm::mat4 *values, int count) const {
  GLint loc = getUniformLocation(name);
  if (loc != -1 && count > 0)
    glUniformMatrix4fv(loc, count, GL_FALSE, glm::value_ptr(values[0]));
}

void Shader::setVec3Array(std::string_view name, const glm::vec3 *values, int count) const {
  GLint loc = getUniformLocation(name);
  if (loc != -1 && count > 0)
    glUniform3fv(loc, count, glm::value_ptr(values[0]));
}

void Shader::setVec4Array(std::string_view name, const glm::vec4 *values, int count) const {
  GLint loc = getUniformLocation(name);
  if (loc != -1 && count > 0)
    glUniform4fv(loc, count, glm::value_ptr(values[0]));
}

void Shader::setMat4(std::string_view name, glm::mat4 value) const {
  GLint loc = getUniformLocation(name);
  if (loc != -1)
    glUniformMatrix4fv(loc, 1, GL_FALSE, glm::value_ptr(value));
}