#pragma once
// Global toggles shared by the state system, input bindings and engine config.

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
