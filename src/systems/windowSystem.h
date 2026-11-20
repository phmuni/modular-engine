#pragma once
// SDL3 window and OpenGL context initialization.

#include "foundation/ecs/systemManager.h"
#include "rendering/renderer.h"

class WindowSystem : public BaseSystem {
private:
  SDL_Window *m_window;
  SDL_GLContext m_glContext;
  bool m_vsync = true;
  bool m_cursorVisible = true;
  static inline bool s_highDensity = true;

public:
  // Whether the next window gets a framebuffer at the display's full pixel density (sharp on Retina and Wayland, and
  // the window scaled up on displays with a content scale). Off for deterministic runs, so their pictures do not
  // depend on the screen they ran on. Before the window is created.
  static void setHighDensity(bool enabled) { s_highDensity = enabled; }

  WindowSystem(float screenWidth, float screenHeight);
  bool initialize(float screenWidth, float screenHeight);
  void onResize(int newWidth, int newHeight, Renderer &renderer);
  SDL_Window *getWindow() const;
  SDL_GLContext getContext() const;
  void setCursor(bool boolean);
  // Shows or hides the system cursor over the window; no-op when it already is so
  void setCursorVisible(bool visible);
  // No-op when the value is already applied
  void setVSync(bool enabled);
  // Borderless fullscreen at the desktop's size; the window comes back at its old size
  void setFullscreen(bool enabled);
  bool isFullscreen() const;

  // Framebuffer pixels per window unit (2 on a Retina screen with a high density window, else 1)
  float getPixelDensity() const;
  // The scale at which UI drawn in window units reads as on a normal screen: the display's content scale over the
  // pixel density. 1 where the window units are already points (macOS), 1.5 on a Windows display set to 150 %.
  float getAutoUiScale() const;
};
