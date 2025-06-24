#include "loader/shaderLoader.h"

Shader::Shader(const char *vertexShaderFile, const char *fragmentShaderFile) : shaderID(0) {
  // Compile and link shaders
  shaderID = createShaderProgram(vertexShaderFile, fragmentShaderFile);
}

GLuint Shader::createShaderProgram(const char *vertexShaderFile, const char *fragmentShaderFile) {
  GLuint vertexShader = compileShader(GL_VERTEX_SHADER, vertexShaderFile);
  GLuint fragmentShader = compileShader(GL_FRAGMENT_SHADER, fragmentShaderFile);

  if (vertexShader == 0 || fragmentShader == 0) {
    std::cerr << "Failed to compile shaders!" << std::endl;
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
    std::cerr << "Shader program linking error: " << log.data() << std::endl;
    return 0;
  }

  // Delete shaders as they are already linked to the program
  glDeleteShader(vertexShader);
  glDeleteShader(fragmentShader);

  return programID; // Return the linked shader program
}

void Shader::use() const { glUseProgram(shaderID); }

void Shader::setTex(const char *name, GLuint textureID, int textureUnit) const {
  glActiveTexture(GL_TEXTURE0 + textureUnit); // Activate the correct texture unit
  glBindTexture(GL_TEXTURE_2D, textureID);
  if (!textureID) {
    std::cerr << "Warning: Invalid texture bind in: " << name << std::endl;
  }

  GLuint location = glGetUniformLocation(shaderID, name);
  if (location != -1) {
    glUniform1i(location, textureUnit);
  } else {
    std::cerr << "Warning: Uniform '" << name << "' not found in shader!" << std::endl;
  }
}

void Shader::setInt(const char *name, int value) const {
  GLuint location = glGetUniformLocation(shaderID, name);
  glUniform1i(location, value);
}

void Shader::setFloat(const char *name, float value) const {
  GLuint location = glGetUniformLocation(shaderID, name);
  glUniform1f(location, value);
}

void Shader::setVec3(const char *name, glm::vec3 value) const {
  GLuint location = glGetUniformLocation(shaderID, name);
  glUniform3fv(location, 1, glm::value_ptr(value));
}

void Shader::setMat4(const char *name, glm::mat4 value) const {
  GLuint location = glGetUniformLocation(shaderID, name);
  glUniformMatrix4fv(location, 1, GL_FALSE, glm::value_ptr(value));
}

std::string Shader::readShaderFile(const char *filename) const {
  std::ifstream file(filename);
  std::stringstream buffer;

  if (!file.is_open()) {
    std::cerr << "Error opening shader file: " << filename << std::endl;
    return ""; // Return empty string if file can't be opened
  }

  buffer << file.rdbuf(); // Read file content
  return buffer.str();    // Return content as a std::string
}

// Compile a shader of a specific type (vertex or fragment)
GLuint Shader::compileShader(GLenum type, const char *filename) {
  std::string shaderSource = readShaderFile(filename);
  const char *shaderSourceCStr = shaderSource.c_str(); // Convert string to C-string for OpenGL

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
    std::cerr << "Shader compilation error: " << log.data() << std::endl;
    return 0;
  }

  return shader; // Return the compiled shader
}
