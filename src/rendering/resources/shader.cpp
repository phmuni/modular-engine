#include "rendering/resources/shader.h"
#include <fstream>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <sstream>
#include <vector>


Shader::Shader(const char *vertexFile, const char *fragmentFile) {
  m_shaderID = createShaderProgram(vertexFile, fragmentFile);
}

bool Shader::load(const char *vertexFile, const char *fragmentFile) {
  if (m_shaderID != 0) {
    glDeleteProgram(m_shaderID);
    m_shaderID = 0;
  }

  m_shaderID = createShaderProgram(vertexFile, fragmentFile);
  return (m_shaderID != 0);
}

std::string Shader::readShaderFile(const char *filename) const {
  std::ifstream file(filename);
  if (!file.is_open()) {
    std::cerr << "[Shader] Cannot open: " << filename << std::endl;
    return "";
  }

  std::stringstream buffer;
  buffer << file.rdbuf();
  return buffer.str();
}

GLuint Shader::compileShader(GLenum type, const char *filename) {
  std::string shaderSource = readShaderFile(filename);
  if (shaderSource.empty())
    return 0;

  const char *sourceCStr = shaderSource.c_str();
  GLuint shader = glCreateShader(type);
  glShaderSource(shader, 1, &sourceCStr, nullptr);
  glCompileShader(shader);

  // Check compilation
  GLint success;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
  if (!success) {
    GLint logLength;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLength);
    std::vector<char> log(logLength);
    glGetShaderInfoLog(shader, logLength, nullptr, log.data());
    std::cerr << "[Shader] Compilation error in " << filename << ":\n" << log.data() << std::endl;
    glDeleteShader(shader);
    return 0;
  }

  return shader;
}

GLuint Shader::createShaderProgram(const char *vertexFile, const char *fragmentFile) {
  GLuint vertexShader = compileShader(GL_VERTEX_SHADER, vertexFile);
  GLuint fragmentShader = compileShader(GL_FRAGMENT_SHADER, fragmentFile);

  if (vertexShader == 0 || fragmentShader == 0) {
    if (vertexShader)
      glDeleteShader(vertexShader);
    if (fragmentShader)
      glDeleteShader(fragmentShader);
    return 0;
  }

  GLuint programID = glCreateProgram();
  glAttachShader(programID, vertexShader);
  glAttachShader(programID, fragmentShader);
  glLinkProgram(programID);

  // Check linking
  GLint success;
  glGetProgramiv(programID, GL_LINK_STATUS, &success);
  if (!success) {
    GLint logLength;
    glGetProgramiv(programID, GL_INFO_LOG_LENGTH, &logLength);
    std::vector<char> log(logLength);
    glGetProgramInfoLog(programID, logLength, nullptr, log.data());
    std::cerr << "[Shader] Linking error:\n" << log.data() << std::endl;
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);
    glDeleteProgram(programID);
    return 0;
  }

  glDeleteShader(vertexShader);
  glDeleteShader(fragmentShader);

  return programID;
}

void Shader::use() const { glUseProgram(m_shaderID); }

void Shader::setTex(const char *name, GLuint textureID, int textureUnit) const {
  glActiveTexture(GL_TEXTURE0 + textureUnit);
  glBindTexture(GL_TEXTURE_2D, textureID);

  GLint location = glGetUniformLocation(m_shaderID, name);
  if (location != -1) {
    glUniform1i(location, textureUnit);
  } else {
    std::cerr << "[Shader] Uniform not found: " << name << std::endl;
  }
}

void Shader::setInt(const char *name, int value) const {
  GLint location = glGetUniformLocation(m_shaderID, name);
  if (location != -1)
    glUniform1i(location, value);
}

void Shader::setFloat(const char *name, float value) const {
  GLint location = glGetUniformLocation(m_shaderID, name);
  if (location != -1)
    glUniform1f(location, value);
}

void Shader::setVec3(const char *name, glm::vec3 value) const {
  GLint location = glGetUniformLocation(m_shaderID, name);
  if (location != -1)
    glUniform3fv(location, 1, glm::value_ptr(value));
}

void Shader::setMat3(const char *name, glm::mat3 value) const {
  GLint location = glGetUniformLocation(m_shaderID, name);
  if (location != -1)
    glUniformMatrix3fv(location, 1, GL_FALSE, glm::value_ptr(value));
}

void Shader::setMat4(const char *name, glm::mat4 value) const {
  GLint location = glGetUniformLocation(m_shaderID, name);
  if (location != -1)
    glUniformMatrix4fv(location, 1, GL_FALSE, glm::value_ptr(value));
}

GLuint Shader::getShaderID() const { return m_shaderID; }
