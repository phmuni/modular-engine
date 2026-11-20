#pragma once
// Engine-wide configuration constants and path utilities.

#include "SDL3/SDL_filesystem.h"
#include "SDL3/SDL_scancode.h"
#include "foundation/core/toggle.h"
#include <algorithm>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>

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

namespace EngineConfig {

inline const std::string &getBasePath() {
  static std::string basePath = [] {
    const char *p = SDL_GetBasePath();
    return p ? std::string(p) : "./";
  }();
  return basePath;
}

// Asset paths are written relative to the executable's folder ("../assets/..."): a game's bin/ sits next to its
// assets/. The engine's own files (shaders, fonts, basic models) go into bin/engine-assets when a game is built
// (modular_engine_add_game), and a "../assets/..." path the game's assets/ folder does not have is looked up there.
constexpr std::string_view ASSETS_PREFIX = "../assets/";
constexpr std::string_view ENGINE_ASSETS_FOLDER = "engine-assets/";

inline std::string resolvePath(std::string_view relativePath) {
  std::filesystem::path path(relativePath);
  if (path.is_absolute())
    return PathUtils::normalizeSeparators(path.string());

  std::string result = getBasePath();
  result.append(relativePath);
  result = PathUtils::normalizeSeparators(std::move(result));

  // Not in the game's assets: the engine's copy of it, if there is one (a new file to write stays in the game's)
  if (relativePath.substr(0, ASSETS_PREFIX.size()) == ASSETS_PREFIX) {
    std::error_code error;
    if (!std::filesystem::exists(result, error)) {
      std::string engineCopy = getBasePath();
      engineCopy.append(ENGINE_ASSETS_FOLDER).append(relativePath.substr(ASSETS_PREFIX.size()));
      engineCopy = PathUtils::normalizeSeparators(std::move(engineCopy));
      if (std::filesystem::exists(engineCopy, error))
        return engineCopy;
    }
  }
  return result;
}

// The one rule for paths a game writes: relative to assets/ ("prefabs/coin.json", "sounds/laser.wav"), the same in
// C++ and Lua. A path that starts with "../" (relative to the executable, as scene files store them) or is absolute
// is kept, so the older spelling "../assets/prefabs/coin.json" works too.
inline std::string assetPath(std::string_view path) {
  if (path.empty() || path.substr(0, 3) == "../" || std::filesystem::path(path).is_absolute())
    return std::string(path);
  return std::string(ASSETS_PREFIX).append(path);
}

// resolvePath for a path a game wrote: what the asset-loading calls (models, sounds, scenes, UI...) use
inline std::string resolveAsset(std::string_view path) { return resolvePath(assetPath(path)); }

// The inverse of resolvePath: a path relative to the executable's folder (e.g. "../assets/x.obj"), so files that
// store paths keep working when the project moves. Paths on another drive stay absolute. The engine's copies in
// bin/engine-assets map back to "../assets/...", the path they were asked for.
inline std::string relativePath(std::string_view absolutePath) {
  std::filesystem::path path(absolutePath);
  if (!path.is_absolute())
    return PathUtils::normalizeSeparators(std::string(absolutePath));
  std::string relative = PathUtils::normalizeSeparators(path.lexically_relative(getBasePath()).string());
  if (relative.empty())
    return PathUtils::normalizeSeparators(path.string());
  if (relative.compare(0, ENGINE_ASSETS_FOLDER.size(), ENGINE_ASSETS_FOLDER) == 0)
    return std::string(ASSETS_PREFIX) + relative.substr(ENGINE_ASSETS_FOLDER.size());
  return relative;
}

// Window
constexpr float DEFAULT_SCREEN_WIDTH = 1280.0f;
constexpr float DEFAULT_SCREEN_HEIGHT = 720.0f;

// Camera clip planes; the sun's shadow cascades split the same depth range
constexpr float CAMERA_NEAR = 0.1f;
constexpr float CAMERA_FAR = 100.0f;

// Sun (directional) shadows: cascaded shadow maps. The view from the camera out to SHADOW_DISTANCE is cut into
// SHADOW_CASCADES slices, each with its own map that follows the camera; nearer slices cover less and look sharper.
constexpr int SHADOW_CASCADES = 3; // at most 4, the size of the arrays in fragmentShader.frag
constexpr int SHADOW_CASCADE_SIZE = 2048;
constexpr float SHADOW_DISTANCE = 80.0f;
// 0 splits the distance evenly, 1 logarithmically (much more detail up close)
constexpr float SHADOW_CASCADE_SPLIT = 0.75f;
// How far toward the sun, beyond a cascade, objects still cast into it (tall things outside the view)
constexpr float SHADOW_CASTER_REACH = 60.0f;
// Point and spot lights with Light::castShadows each render a depth cube map of this size per face (4 bytes per
// texel: 1024 is 24 MB a light). Beyond the limit, further lights light without shadows.
constexpr int POINT_SHADOW_SIZE = 1024;
constexpr int MAX_POINT_SHADOWS = 4; // also the number of cube samplers in fragmentShader.frag

// Shaders
constexpr const char *SHADER_VERTEX = "../assets/shaders/vertexShader.vert";
constexpr const char *SHADER_FRAGMENT = "../assets/shaders/fragmentShader.frag";
constexpr const char *SHADER_VERTEX_SHADOW = "../assets/shaders/vertexShadowShader.vert";
constexpr const char *SHADER_FRAGMENT_SHADOW = "../assets/shaders/fragmentShadowShader.frag";
constexpr const char *SHADER_VERTEX_PARTICLE = "../assets/shaders/vertexParticle.vert";
constexpr const char *SHADER_FRAGMENT_PARTICLE = "../assets/shaders/fragmentParticle.frag";
constexpr const char *SHADER_VERTEX_FULLSCREEN = "../assets/shaders/fullscreen.vert";
constexpr const char *SHADER_FRAGMENT_BLOOM_DOWNSAMPLE = "../assets/shaders/bloomDownsample.frag";
constexpr const char *SHADER_FRAGMENT_BLOOM_UPSAMPLE = "../assets/shaders/bloomUpsample.frag";
constexpr const char *SHADER_FRAGMENT_TONEMAP = "../assets/shaders/tonemap.frag";
constexpr const char *SHADER_FRAGMENT_COMPOSITE = "../assets/shaders/composite.frag";
constexpr const char *SHADER_FRAGMENT_FXAA = "../assets/shaders/fxaa.frag";
constexpr const char *SHADER_FRAGMENT_FSR_EASU = "../assets/shaders/fsrEasu.frag";
constexpr const char *SHADER_FRAGMENT_FSR_RCAS = "../assets/shaders/fsrRcas.frag";
// Skeletal animation: the joints a skinned model's vertex shader can take (the bones[] uniform array)
constexpr int MAX_JOINTS = 128;
constexpr const char *SHADER_VERTEX_SKYBOX = "../assets/shaders/skybox.vert";
constexpr const char *SHADER_FRAGMENT_SKYBOX = "../assets/shaders/skybox.frag";

// Bloom (F10): light brighter than the threshold (emission, strong highlights) glows; strength scales the glow.
// Above 1.0, so surfaces that are merely well lit do not glow.
constexpr float BLOOM_THRESHOLD = 1.5f;
constexpr float BLOOM_STRENGTH = 0.08f;

// Models
constexpr const char *MODEL_BOX = "../assets/models/box/box.obj";
constexpr const char *MODEL_SPHERE = "../assets/models/sphere/sphere.obj";
constexpr const char *MODEL_CYLINDER = "../assets/models/cylinder/cylinder.obj"; // diameter 1, height 1, along Y

// Textures: converted to BC1/BC3 (and cached in bin/cache/textures) when the GPU supports S3TC
constexpr bool COMPRESS_TEXTURES = true;
// Bytes of texture data sent to the GPU per frame; larger textures finish over the following frames
constexpr size_t TEXTURE_UPLOAD_BUDGET = 4 * 1024 * 1024;

// Game UI fonts loaded at start-up, available to every document as font-family: LatoLatin
inline const char *const UI_FONTS[] = {"../assets/ui/fonts/LatoLatin-Regular.ttf",
                                       "../assets/ui/fonts/LatoLatin-Bold.ttf"};

// Scene files listed by the editor and the scene browser example
constexpr const char *SCENES_DIR = "../assets/scenes";

// Screenshots (F12), saved next to the executable
constexpr SDL_Scancode SCREENSHOT_KEY = SDL_SCANCODE_F12;
constexpr const char *SCREENSHOT_PATH = "screenshot.bmp";

// Default toggle states. The free camera is off: a game drives its camera, and the editor's right-drag flight needs
// no toggle; examples that want the free camera (the scene browser) turn it on.
inline const std::pair<Toggle, bool> DEFAULT_TOGGLES[] = {
    {Toggle::FreeCamera, false}, {Toggle::FreeCameraVertical, true}, {Toggle::CursorLock, true},
    {Toggle::Wireframe, false},     {Toggle::ShowUI, false},   {Toggle::Shadows, true},
    {Toggle::ShowStats, false},     {Toggle::VSync, true},     {Toggle::Paused, false},
    {Toggle::GammaCorrect, true},   {Toggle::UIDebugger, false},
    {Toggle::Bloom, true},
};
constexpr int DEFAULT_TOGGLES_COUNT = std::size(DEFAULT_TOGGLES);

// Default toggle keybinds
inline const std::pair<SDL_Scancode, Toggle> DEFAULT_TOGGLE_KEYBINDS[] = {
    {SDL_SCANCODE_RALT, Toggle::CursorLock}, {SDL_SCANCODE_F1, Toggle::Wireframe}, {SDL_SCANCODE_F2, Toggle::ShowUI},
    {SDL_SCANCODE_F3, Toggle::Shadows},      {SDL_SCANCODE_F4, Toggle::FreeCameraVertical},
    {SDL_SCANCODE_F5, Toggle::ShowStats},    {SDL_SCANCODE_F6, Toggle::VSync},
    {SDL_SCANCODE_F7, Toggle::Paused},       {SDL_SCANCODE_F8, Toggle::GammaCorrect},
    {SDL_SCANCODE_F9, Toggle::UIDebugger},  {SDL_SCANCODE_F10, Toggle::Bloom},
};

constexpr int DEFAULT_TOGGLE_KEYBINDS_COUNT = std::size(DEFAULT_TOGGLE_KEYBINDS);

} // namespace EngineConfig
