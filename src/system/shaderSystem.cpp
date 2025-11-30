#include "system/shaderSystem.h"

#include "system/shaderSystem.h"

bool ShaderSystem::loadShader(const std::string &name, const char *vertexPath, const char *fragmentPath) {
  if (shaders.count(name)) {
    return false;
  }
  shaders[name] = std::make_shared<Shader>(vertexPath, fragmentPath);
  return true;
}

Shader &ShaderSystem::getShader(const std::string &name) { return *shaders.at(name); }

void ShaderSystem::unloadShader(const std::string &name) { shaders.erase(name); }

void ShaderSystem::clear() { shaders.clear(); }
