#pragma once

// External headers
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

// Native headers
#include <fstream>  // IWYU pragma: keep
#include <iostream> // IWYU pragma: keep
#include <sstream>  // IWYU pragma: keep
#include <vector>   // IWYU pragma: keep

class Shader {
private:
  GLuint shaderID;

  std::string readShaderFile(const char *filename) const; // Read shader file at specified path
  GLuint compileShader(GLenum type,
                       const char *filename); // Compile shader (vertex or fragment)
  GLuint createShaderProgram(const char *vertexShaderFile,
                             const char *fragmentShaderFile); // Create shader program and link
                                                              // the compiled shaders to it

public:
  Shader(const char *vertexShaderFile, const char *fragmentShaderFile); // Default constructor

  bool load(const char *vertexShaderFile,
            const char *fragmentShaderFile); // Initialize shaders with path
  void use() const;                          // Activate Shader

  // Define uniforms in the shader
  void setTex(const char *name, GLuint textureID, int textureUnit) const;
  void setInt(const char *name, int value) const;
  void setFloat(const char *name, float value) const;
  void setVec3(const char *name, glm::vec3 value) const;
  void setMat4(const char *name, glm::mat4 value) const;

  GLuint getShaderID() const; // Getter for shaderID
};
