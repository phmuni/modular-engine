#pragma once
// SDL3 window and OpenGL context initialization.

#include "foundation/ecs/systemManager.h"
#include "rendering/renderer.h"
#include <glm/glm.hpp>
#include <optional>
#include <vector>

class WindowSystem : public BaseSystem {
private:
  SDL_Window *m_window;
  SDL_GLContext m_glContext;
  bool m_vsync = true;
  bool m_cursorVisible = true;
  static inline bool s_highDensity = true;
  static inline bool s_sizeRequested = false;

public:
  // Whether the next window gets a framebuffer at the display's full pixel density (sharp on Retina and Wayland, and
  // the window scaled up on displays with a content scale). Off for deterministic runs, so their pictures do not
  // depend on the screen they ran on. Before the window is created.
  static void setHighDensity(bool enabled) { s_highDensity = enabled; }

  // Where a window sits and how big it is, in window units; what is remembered between runs of a game
  struct Placement {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
  };

  // The game asked for this size, so the default window is not grown on a scaled display (the size is exactly what
  // was asked). Before the window is created.
  static void setSizeRequested(bool requested) { s_sizeRequested = requested; }

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

  // The window's size in window units (the SDL window size, not the framebuffer's pixels)
  glm::ivec2 getWindowSize() const;
  // Resizes the window to that many window units and waits for the system to do it, so getWindowSize answers with it
  // right after. A window that would then reach outside its display is centered; fullscreen keeps it for later.
  // Returns false for a size that is not positive.
  bool setWindowSize(int width, int height);
  // Window sizes worth offering on the display the window is on, smallest first: the common 16:9 sizes that fit its
  // usable bounds (what is left after the taskbar and menu bar), and always the usable bounds themselves
  std::vector<glm::ivec2> getResolutions() const;

  // The window's placement now, or nothing in fullscreen or minimized, where it is not the window's own
  std::optional<Placement> getPlacement() const;
  // Moves and resizes the window to a placement from an earlier run, if it still fits on a display that is connected
  // now; otherwise the window stays as it is, and is centered. Returns whether the placement was used.
  bool restorePlacement(const Placement &placement);
  // The window is created hidden, so it can be put in place before anyone sees it; this shows it
  void show();

  // Framebuffer pixels per window unit (2 on a Retina screen with a high density window, else 1)
  float getPixelDensity() const;
  // The scale at which UI drawn in window units reads as on a normal screen: the display's content scale over the
  // pixel density. 1 where the window units are already points (macOS), 1.5 on a Windows display set to 150 %.
  float getAutoUiScale() const;
};
