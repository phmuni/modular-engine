#pragma once
// Engine-wide configuration constants and path utilities.

#include "SDL3/SDL_filesystem.h"
#include "SDL3/SDL_scancode.h"
#include "systems/stateSystem.h"
#include <algorithm>
#include <string>
#include <utility>

namespace EngineConfig {

inline const std::string &getBasePath() {
  static std::string basePath = [] {
    const char *p = SDL_GetBasePath();
    return p ? std::string(p) : "./";
  }();
  return basePath;
}

inline std::string resolvePath(std::string_view relativePath) {
  std::string result = getBasePath();
  result.append(relativePath);
  return result;
}

// Window
constexpr float DEFAULT_SCREEN_WIDTH = 1280.0f;
constexpr float DEFAULT_SCREEN_HEIGHT = 720.0f;
constexpr const char *WINDOW_TITLE = "Modular Engine";

// Rendering
constexpr float DEFAULT_CLEAR_COLOR_R = 0.4f;
constexpr float DEFAULT_CLEAR_COLOR_G = 0.4f;
constexpr float DEFAULT_CLEAR_COLOR_B = 0.4f;
constexpr float DEFAULT_CLEAR_COLOR_A = 1.0f;

constexpr int SHADOW_MAP_WIDTH = 2048;
constexpr int SHADOW_MAP_HEIGHT = 2048;

// Asset paths
constexpr const char *ASSET_BASE_PATH = "../assets/";
constexpr const char *MODEL_PATH = "../assets/models/";
constexpr const char *SHADER_PATH = "../assets/shaders/";
constexpr const char *TEXTURE_PATH = "../assets/textures/";
constexpr const char *SOUND_PATH = "../assets/sounds/";

// Shaders
constexpr const char *SHADER_VERTEX = "../assets/shaders/vertexShader.vert";
constexpr const char *SHADER_FRAGMENT = "../assets/shaders/fragmentShader.frag";
constexpr const char *SHADER_VERTEX_SHADOW = "../assets/shaders/vertexShadowShader.vert";
constexpr const char *SHADER_FRAGMENT_SHADOW = "../assets/shaders/fragmentShadowShader.frag";
constexpr const char *SHADER_VERTEX_PARTICLE = "../assets/shaders/vertexParticle.vert";
constexpr const char *SHADER_FRAGMENT_PARTICLE = "../assets/shaders/fragmentParticle.frag";

// Models
constexpr const char *MODEL_BACKPACK = "../assets/models/backpack/backpack.obj";
constexpr const char *TEXTURE_BACKPACK = "../assets/models/backpack/";

constexpr const char *MODEL_BOX = "../assets/models/box/box.obj";
constexpr const char *TEXTURE_BOX = "../assets/models/box/";

// Default toggle states
inline const std::pair<Toggle, bool> DEFAULT_TOGGLES[] = {
    {Toggle::CameraMovement, true}, {Toggle::CameraFly, true}, {Toggle::CursorLock, true},
    {Toggle::Wireframe, false},     {Toggle::ShowUI, false},   {Toggle::Shadows, true},
};
constexpr int DEFAULT_TOGGLES_COUNT = std::size(DEFAULT_TOGGLES);

// Default toggle keybinds
inline const std::pair<SDL_Scancode, Toggle> DEFAULT_TOGGLE_KEYBINDS[] = {
    {SDL_SCANCODE_RALT, Toggle::CursorLock}, {SDL_SCANCODE_F1, Toggle::Wireframe}, {SDL_SCANCODE_F2, Toggle::ShowUI},
    {SDL_SCANCODE_F3, Toggle::Shadows},      {SDL_SCANCODE_F4, Toggle::CameraFly},
};

constexpr int DEFAULT_TOGGLE_KEYBINDS_COUNT = std::size(DEFAULT_TOGGLE_KEYBINDS);

} // namespace EngineConfig

namespace PathUtils {

inline std::string normalizeSeparators(std::string path) {
  std::replace(path.begin(), path.end(), '\\', '/');
  return path;
}

inline std::string_view stripExtension(std::string_view path) {
  auto slash = path.rfind('/');
  auto dot = path.rfind('.');

  if (dot != std::string_view::npos && (slash == std::string_view::npos || dot > slash)) {
    return path.substr(0, dot); // zero-copy
  }
  return path;
}

inline bool hasExtension(std::string_view path) { return stripExtension(path).size() < path.size(); }

} // namespace PathUtils
