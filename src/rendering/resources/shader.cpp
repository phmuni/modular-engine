#include "rendering/resources/shader.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>

Shader::Shader(const char *vertexShaderFile, const char *fragmentShaderFile) : shaderID(0) {
  shaderID = createShaderProgram(vertexShaderFile, fragmentShaderFile);
}

bool Shader::load(const char *vertexShaderFile, const char *fragmentShaderFile) {
  if (shaderID != 0) {
    glDeleteProgram(shaderID);
    shaderID = 0;
  }

  shaderID = createShaderProgram(vertexShaderFile, fragmentShaderFile);
  return (shaderID != 0);
}

GLuint Shader::createShaderProgram(const char *vertexShaderFile, const char *fragmentShaderFile) {
  GLuint vertexShader = compileShader(GL_VERTEX_SHADER, vertexShaderFile);
  GLuint fragmentShader = compileShader(GL_FRAGMENT_SHADER, fragmentShaderFile);

  if (vertexShader == 0 || fragmentShader == 0) {
    if (vertexShader != 0)
      glDeleteShader(vertexShader);
    if (fragmentShader != 0)
      glDeleteShader(fragmentShader);
    return 0;
  }

  GLuint programID = glCreateProgram();
  glAttachShader(programID, vertexShader);
  glAttachShader(programID, fragmentShader);
  glLinkProgram(programID);

  GLint success;
  glGetProgramiv(programID, GL_LINK_STATUS, &success);
  if (!success) {
    GLint logLength;
    glGetProgramiv(programID, GL_INFO_LOG_LENGTH, &logLength);
    std::vector<char> log(logLength);
    glGetProgramInfoLog(programID, logLength, &logLength, log.data());
    std::cerr << "[Shader] Program linking error: " << log.data() << std::endl;
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);
    glDeleteProgram(programID);
    return 0;
  }

  glDeleteShader(vertexShader);
  glDeleteShader(fragmentShader);

  return programID;
}

void Shader::use() const { glUseProgram(shaderID); }

void Shader::setTex(const char *name, GLuint textureID, int textureUnit) const {
  glActiveTexture(GL_TEXTURE0 + textureUnit);
  glBindTexture(GL_TEXTURE_2D, textureID);

  if (!textureID) {
    std::cerr << "[Shader] Warning: Invalid texture bind in: " << name << std::endl;
  }

  GLint location = glGetUniformLocation(shaderID, name);
  if (location != -1) {
    glUniform1i(location, textureUnit);
  } else {
    std::cerr << "[Shader] Warning: Uniform '" << name << "' not found!" << std::endl;
  }
}

void Shader::setInt(const char *name, int value) const {
  GLint location = glGetUniformLocation(shaderID, name);
  if (location != -1) {
    glUniform1i(location, value);
  }
}

void Shader::setFloat(const char *name, float value) const {
  GLint location = glGetUniformLocation(shaderID, name);
  if (location != -1) {
    glUniform1f(location, value);
  }
}

void Shader::setVec3(const char *name, glm::vec3 value) const {
  GLint location = glGetUniformLocation(shaderID, name);
  if (location != -1) {
    glUniform3fv(location, 1, glm::value_ptr(value));
  }
}

void Shader::setMat3(const char *name, glm::mat3 value) const {
  GLint location = glGetUniformLocation(shaderID, name);
  if (location != -1) {
    glUniformMatrix3fv(location, 1, GL_FALSE, glm::value_ptr(value));
  }
}

void Shader::setMat4(const char *name, glm::mat4 value) const {
  GLint location = glGetUniformLocation(shaderID, name);
  if (location != -1) {
    glUniformMatrix4fv(location, 1, GL_FALSE, glm::value_ptr(value));
  }
}

GLuint Shader::getShaderID() const { return shaderID; }

std::string Shader::readShaderFile(const char *filename) const {
  std::ifstream file(filename);
  std::stringstream buffer;

  if (!file.is_open()) {
    std::cerr << "[Shader] Error opening shader file: " << filename << std::endl;
    return "";
  }

  buffer << file.rdbuf();
  return buffer.str();
}

GLuint Shader::compileShader(GLenum type, const char *filename) {
  std::string shaderSource = readShaderFile(filename);

  if (shaderSource.empty()) {
    return 0;
  }

  const char *shaderSourceCStr = shaderSource.c_str();
  GLuint shader = glCreateShader(type);
  glShaderSource(shader, 1, &shaderSourceCStr, nullptr);
  glCompileShader(shader);

  GLint success;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
  if (!success) {
    GLint logLength;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLength);
    std::vector<char> log(logLength);
    glGetShaderInfoLog(shader, logLength, &logLength, log.data());
    std::cerr << "[Shader] Compilation error in " << filename << ":\n" << log.data() << std::endl;
    glDeleteShader(shader);
    return 0;
  }

  return shader;
}