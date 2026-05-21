
// Shader management class for OpenGL. Handles loading, compiling, and using vertex and fragment shaders.

#include "rendering/resources/shader.h"
#include "foundation/core/logger.h"
#include <fstream>
#include <glm/gtc/type_ptr.hpp>
#include <sstream>

Shader::Shader(std::string_view vertexFile, std::string_view fragmentFile) {
  m_shaderID = createShaderProgram(vertexFile.data(), fragmentFile.data());
}

bool Shader::load(std::string_view vertexFile, std::string_view fragmentFile) {
  if (m_shaderID) {
    glDeleteProgram(m_shaderID);
    m_shaderID = 0;
    m_uniformCache.clear();
  }
  m_shaderID = createShaderProgram(vertexFile.data(), fragmentFile.data());
  return m_shaderID != 0;
}

GLint Shader::getUniformLocation(const char *name) const {
  auto it = m_uniformCache.find(name);
  if (it != m_uniformCache.end())
    return it->second;
  GLint loc = glGetUniformLocation(m_shaderID, name);
  m_uniformCache[name] = loc;
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

GLuint Shader::compileShader(GLenum type, const char *filename) {
  std::string source = readShaderFile(filename);
  if (source.empty())
    return 0;

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
    LOG_E("[Shader] Compilation error in %s:\n%s", filename, log.data());
    glDeleteShader(shader);
    return 0;
  }
  return shader;
}

GLuint Shader::createShaderProgram(const char *vertexFile, const char *fragmentFile) {
  GLuint vert = compileShader(GL_VERTEX_SHADER, vertexFile);
  GLuint frag = compileShader(GL_FRAGMENT_SHADER, fragmentFile);

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
    LOG_E("[Shader] Linking error:\n%s", log.data());
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

void Shader::setTex(std::string_view name, GLuint textureID, int textureUnit) const {
  glActiveTexture(GL_TEXTURE0 + textureUnit);
  glBindTexture(GL_TEXTURE_2D, textureID);
  GLint loc = getUniformLocation(name.data());
  if (loc != -1)
    glUniform1i(loc, textureUnit);
  else
    LOG_W("[Shader] Uniform not found: %s", name);
}

void Shader::setInt(std::string_view name, int value) const {
  GLint loc = getUniformLocation(name.data());
  if (loc != -1)
    glUniform1i(loc, value);
}

void Shader::setFloat(std::string_view name, float value) const {
  GLint loc = getUniformLocation(name.data());
  if (loc != -1)
    glUniform1f(loc, value);
}

void Shader::setVec3(std::string_view name, glm::vec3 value) const {
  GLint loc = getUniformLocation(name.data());
  if (loc != -1)
    glUniform3fv(loc, 1, glm::value_ptr(value));
}

void Shader::setMat3(std::string_view name, glm::mat3 value) const {
  GLint loc = getUniformLocation(name.data());
  if (loc != -1)
    glUniformMatrix3fv(loc, 1, GL_FALSE, glm::value_ptr(value));
}

void Shader::setMat4(std::string_view name, glm::mat4 value) const {
  GLint loc = getUniformLocation(name.data());
  if (loc != -1)
    glUniformMatrix4fv(loc, 1, GL_FALSE, glm::value_ptr(value));
}