#pragma once

#include "SDL3/SDL_filesystem.h"
#include "SDL3/SDL_scancode.h"
#include "systems/stateSystem.h"
#include <algorithm>
#include <string>
#include <utility>

namespace EngineConfig {

// Returns the base path of the executable's directory (e.g. ".../bin/")
// All asset paths are resolved relative to this.
inline std::string getBasePath() {
  static std::string basePath;
  if (basePath.empty()) {
    const char *sdlBase = SDL_GetBasePath();
    if (sdlBase) {
      basePath = sdlBase;
    } else {
      basePath = "./";
    }
  }
  return basePath;
}

// Resolve a relative asset path to an absolute path based on the executable location
inline std::string resolvePath(const char *relativePath) { return getBasePath() + relativePath; }

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

// Asset paths (relative to binary directory)
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

// Models
constexpr const char *MODEL_BACKPACK = "../assets/models/backpack/backpack.obj";
constexpr const char *TEXTURE_BACKPACK = "../assets/models/backpack/";

constexpr const char *MODEL_BOX = "../assets/models/box/box.obj";
constexpr const char *TEXTURE_BOX = "../assets/models/box/";

// Default toggle states
inline const std::pair<Toggle, bool> DEFAULT_TOGGLES[] = {
    {Toggle::CameraMovement, true},
    {Toggle::CameraFly, true},
    {Toggle::CursorLock, true},
    {Toggle::Wireframe, false},
    {Toggle::ShowUI, false},
    {Toggle::Shadows, true},
};
constexpr int DEFAULT_TOGGLES_COUNT = sizeof(DEFAULT_TOGGLES) / sizeof(DEFAULT_TOGGLES[0]);

// Default toggle keybinds
inline const std::pair<SDL_Scancode, Toggle> DEFAULT_TOGGLE_KEYBINDS[] = {
    {SDL_SCANCODE_RALT, Toggle::CursorLock},
    {SDL_SCANCODE_F1, Toggle::Wireframe},
    {SDL_SCANCODE_F2, Toggle::ShowUI},
    {SDL_SCANCODE_F3, Toggle::Shadows},
    {SDL_SCANCODE_F4, Toggle::CameraFly},
};
constexpr int DEFAULT_TOGGLE_KEYBINDS_COUNT = sizeof(DEFAULT_TOGGLE_KEYBINDS) / sizeof(DEFAULT_TOGGLE_KEYBINDS[0]);

} // namespace EngineConfig

namespace PathUtils {

inline std::string normalizeSeparators(const std::string &path) {
  std::string result = path;
  std::replace(result.begin(), result.end(), '\\', '/');
  return result;
}

inline std::string stripExtension(const std::string &path) {
  auto slash = path.find_last_of('/');
  auto dot = path.find_last_of('.');
  if (dot != std::string::npos && (slash == std::string::npos || dot > slash)) {
    return path.substr(0, dot);
  }
  return path;
}

inline bool hasExtension(const std::string &path) {
  auto slash = path.find_last_of('/');
  auto dot = path.find_last_of('.');
  return dot != std::string::npos && (slash == std::string::npos || dot > slash);
}

inline std::string normalize(const std::string &path) { return normalizeSeparators(path); }

} // namespace PathUtils
