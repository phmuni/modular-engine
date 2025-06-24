#pragma once

#include "loader/shaderLoader.h"
#include <memory>
#include <string>
#include <unordered_map>

class ShaderManager {
private:
  std::unordered_map<std::string, std::shared_ptr<Shader>> shaders;

public:
  ShaderManager() = default;

  // Loads and stores a shader
  bool loadShader(const char *name, const char *vertexPath, const char *fragmentPath);

  // Returns a previously loaded shader
  Shader &getShader(const char *name) const;

  // Removes a shader
  void unloadShader(const char *name);

  // Removes all shaders
  void clear();
};
