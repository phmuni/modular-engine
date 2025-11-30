#pragma once

// ============================================================================
//  ENGINE CONFIGURATION - Central place for all engine defaults and constants
// ============================================================================
// This file contains all compile-time configuration values used throughout
// the engine. Modify these values to adjust engine behavior without recompiling
// dependent code multiple times.
// ============================================================================

namespace EngineConfig {

// ========== WINDOW CONFIGURATION ==========
constexpr float DEFAULT_SCREEN_WIDTH = 1280.0f;
constexpr float DEFAULT_SCREEN_HEIGHT = 720.0f;
constexpr const char *WINDOW_TITLE = "Modular Engine";

// ========== RENDERING CONFIGURATION ==========
constexpr float DEFAULT_CLEAR_COLOR_R = 0.4f;
constexpr float DEFAULT_CLEAR_COLOR_G = 0.4f;
constexpr float DEFAULT_CLEAR_COLOR_B = 0.4f;
constexpr float DEFAULT_CLEAR_COLOR_A = 1.0f;

constexpr int SHADOW_MAP_WIDTH = 2048;
constexpr int SHADOW_MAP_HEIGHT = 2048;

// ========== ASSET PATHS CONFIGURATION ==========
// Base directories (relative to binary execution directory)
constexpr const char *ASSET_BASE_PATH = "../assets/";
constexpr const char *MODEL_PATH = "../assets/models/";
constexpr const char *SHADER_PATH = "../assets/shaders/";
constexpr const char *TEXTURE_PATH = "../assets/textures/";
constexpr const char *SOUND_PATH = "../assets/sounds/";

// ========== SHADER FILES ==========
constexpr const char *SHADER_VERTEX = "../assets/shaders/vertexShader.vert";
constexpr const char *SHADER_FRAGMENT = "../assets/shaders/fragmentShader.frag";
constexpr const char *SHADER_VERTEX_SHADOW = "../assets/shaders/vertexShadowShader.vert";
constexpr const char *SHADER_FRAGMENT_SHADOW = "../assets/shaders/fragmentShadowShader.frag";

// ========== MODEL ASSETS ==========
constexpr const char *MODEL_BACKPACK = "../assets/models/backpack/backpack.obj";
constexpr const char *TEXTURE_BACKPACK = "../assets/models/backpack/";

constexpr const char *MODEL_BOX = "../assets/models/box/box.obj";
constexpr const char *TEXTURE_BOX = "../assets/models/box/";

} // namespace EngineConfig
