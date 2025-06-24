#include "manager/shaderManager.h"

bool ShaderManager::loadShader(const char *name, const char *vertexPath, const char *fragmentPath) {
  auto shader = std::make_shared<Shader>(vertexPath, fragmentPath);

  shaders[name] = shader;

  return true;
}

Shader &ShaderManager::getShader(const char *name) const { return *shaders.at(name); }

void ShaderManager::unloadShader(const char *name) { shaders.erase(name); }

void ShaderManager::clear() { shaders.clear(); }
