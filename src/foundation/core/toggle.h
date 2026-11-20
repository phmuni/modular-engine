#pragma once
// Global toggles shared by the state system, input bindings and engine config.

#include <utility>

enum class Toggle {
  FreeCamera,         // the player flies the camera (WASD + mouse) instead of the game driving it
  FreeCameraVertical, // the free camera also moves up and down (Space/Shift), else it stays on its plane
  CursorLock,
  Wireframe,
  ShowUI,
  Shadows,
  ShowStats,
  VSync,
  Paused,
  GammaCorrect,
  UIDebugger,
  Bloom,
};

#include <optional>
#include <string_view>

// The toggles' names where they are chosen by text (Lua's engine.setToggle, files): the C++ names in camelCase
inline constexpr std::pair<std::string_view, Toggle> TOGGLE_NAMES[] = {
    {"freeCamera", Toggle::FreeCamera}, {"freeCameraVertical", Toggle::FreeCameraVertical},
    {"cursorLock", Toggle::CursorLock}, {"wireframe", Toggle::Wireframe},
    {"showUI", Toggle::ShowUI},         {"shadows", Toggle::Shadows},
    {"showStats", Toggle::ShowStats},   {"vsync", Toggle::VSync},
    {"paused", Toggle::Paused},         {"gammaCorrect", Toggle::GammaCorrect},
    {"uiDebugger", Toggle::UIDebugger}, {"bloom", Toggle::Bloom},
};

inline std::optional<Toggle> toggleFromName(std::string_view name) {
  for (const auto &[toggleName, toggle] : TOGGLE_NAMES)
    if (toggleName == name)
      return toggle;
  return std::nullopt;
}
