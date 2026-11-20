
// Window system implementation for managing the SDL window, OpenGL context, and handling window-related events

#include "systems/windowSystem.h"
#include "foundation/core/logger.h"
#include <algorithm>

WindowSystem::WindowSystem(float screenWidth, float screenHeight) : m_window(nullptr), m_glContext(nullptr) {
  initialize(screenWidth, screenHeight);
};

bool WindowSystem::initialize(float screenWidth, float screenHeight) {
  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
    LOG_E("[Window System] Failed to initialize SDL: %s", SDL_GetError());
    return false;
  }

  // Request OpenGL 4.1 Core Profile (maximum supported on macOS)
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
#ifdef __APPLE__
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
#endif

  // Hidden until Engine::run has put it where it was last time (show), so it never opens at one size to jump to another
  SDL_WindowFlags flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN;
  if (s_highDensity)
    flags |= SDL_WINDOW_HIGH_PIXEL_DENSITY;
  m_window = SDL_CreateWindow("Engine", static_cast<int>(screenWidth), static_cast<int>(screenHeight), flags);
  if (!m_window) {
    LOG_E("[Window System] Failed to create the window: %s", SDL_GetError());
    return false;
  }

  // Where window units are pixels on a scaled display (Windows at 150 %), the default size would be a small window
  // there: grow it by the scale, but keep it inside the screen. Not when the game asked for a size of its own.
  const float autoScale = getAutoUiScale();
  if (s_highDensity && !s_sizeRequested && autoScale > 1.01f) {
    int width = static_cast<int>(screenWidth * autoScale), height = static_cast<int>(screenHeight * autoScale);
    SDL_Rect usable;
    if (SDL_GetDisplayUsableBounds(SDL_GetDisplayForWindow(m_window), &usable) && usable.w > 0 && usable.h > 0) {
      width = std::min(width, usable.w * 9 / 10);
      height = std::min(height, usable.h * 9 / 10);
    }
    SDL_SetWindowSize(m_window, width, height);
    SDL_SetWindowPosition(m_window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
  }

  m_glContext = SDL_GL_CreateContext(m_window);
  if (!m_glContext) {
    LOG_E("[Window System] Failed to create the OpenGL context: %s", SDL_GetError());
    return false;
  }

  if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) {
    LOG_E("[Window System] Failed to initialize GLAD.");
    return false;
  }

  glEnable(GL_DEPTH_TEST);
  glEnable(GL_CULL_FACE);
  glCullFace(GL_BACK);
  glFrontFace(GL_CCW);
  SDL_GL_SetSwapInterval(m_vsync ? 1 : 0);
  SDL_SetWindowRelativeMouseMode(m_window, true);

  return true;
}

void WindowSystem::onResize(int newWidth, int newHeight, Renderer &renderer) {
  renderer.setViewportSize(newWidth, newHeight);
}

SDL_Window *WindowSystem::getWindow() const { return m_window; }
SDL_GLContext WindowSystem::getContext() const { return m_glContext; }

void WindowSystem::setVSync(bool enabled) {
  if (enabled == m_vsync)
    return;
  if (SDL_GL_SetSwapInterval(enabled ? 1 : 0))
    m_vsync = enabled;
  else
    LOG_W("[Window System] Failed to set vsync: %s", SDL_GetError());
}

void WindowSystem::setCursor(bool boolean) { SDL_SetWindowRelativeMouseMode(m_window, boolean); }

void WindowSystem::setCursorVisible(bool visible) {
  if (visible == m_cursorVisible)
    return;
  m_cursorVisible = visible;
  if (visible)
    SDL_ShowCursor();
  else
    SDL_HideCursor();
}

void WindowSystem::setFullscreen(bool enabled) {
  if (enabled != isFullscreen() && !SDL_SetWindowFullscreen(m_window, enabled))
    LOG_W("[Window System] Failed to change fullscreen: %s", SDL_GetError());
}

bool WindowSystem::isFullscreen() const { return (SDL_GetWindowFlags(m_window) & SDL_WINDOW_FULLSCREEN) != 0; }

namespace {
// The usable area of the display a window is on, or of the primary one; false if SDL knows of none
bool usableBoundsOf(SDL_Window *window, SDL_Rect &bounds) {
  SDL_DisplayID display = window ? SDL_GetDisplayForWindow(window) : 0;
  if (!display)
    display = SDL_GetPrimaryDisplay();
  return display && SDL_GetDisplayUsableBounds(display, &bounds) && bounds.w > 0 && bounds.h > 0;
}

bool insideOf(const SDL_Rect &outer, int x, int y, int width, int height) {
  return x >= outer.x && y >= outer.y && x + width <= outer.x + outer.w && y + height <= outer.y + outer.h;
}
} // namespace

glm::ivec2 WindowSystem::getWindowSize() const {
  int width = 0, height = 0;
  SDL_GetWindowSize(m_window, &width, &height);
  return {width, height};
}

bool WindowSystem::setWindowSize(int width, int height) {
  if (width <= 0 || height <= 0)
    return false;
  if (!SDL_SetWindowSize(m_window, width, height))
    LOG_W("[Window System] Failed to resize the window: %s", SDL_GetError());
  // The system may answer later (X11, Wayland); wait for it so the size is what was asked for from here on
  SDL_SyncWindow(m_window);

  if (!isFullscreen() && !(SDL_GetWindowFlags(m_window) & SDL_WINDOW_MAXIMIZED)) {
    SDL_Rect usable;
    int x = 0, y = 0;
    SDL_GetWindowPosition(m_window, &x, &y);
    if (usableBoundsOf(m_window, usable) && !insideOf(usable, x, y, width, height)) {
      SDL_SetWindowPosition(m_window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
      SDL_SyncWindow(m_window);
    }
  }
  return true;
}

std::vector<glm::ivec2> WindowSystem::getResolutions() const {
  // 16:9 sizes, the ones games and monitors use
  static constexpr glm::ivec2 kCommon[] = {{960, 540},   {1280, 720},  {1600, 900},
                                           {1920, 1080}, {2560, 1440}, {3840, 2160}};
  std::vector<glm::ivec2> sizes;
  SDL_Rect usable;
  if (!usableBoundsOf(m_window, usable)) {
    // No display to measure: the window as it is is the one size known to be possible
    sizes.push_back(getWindowSize());
    return sizes;
  }
  for (const glm::ivec2 &size : kCommon)
    if (size.x <= usable.w && size.y <= usable.h)
      sizes.push_back(size);
  const glm::ivec2 desktop(usable.w, usable.h);
  if (std::find(sizes.begin(), sizes.end(), desktop) == sizes.end())
    sizes.push_back(desktop);
  std::sort(sizes.begin(), sizes.end(),
            [](const glm::ivec2 &a, const glm::ivec2 &b) { return a.x != b.x ? a.x < b.x : a.y < b.y; });
  return sizes;
}

std::optional<WindowSystem::Placement> WindowSystem::getPlacement() const {
  if (isFullscreen() || (SDL_GetWindowFlags(m_window) & SDL_WINDOW_MINIMIZED))
    return std::nullopt; // a minimized window's position is not a place
  Placement placement;
  SDL_GetWindowPosition(m_window, &placement.x, &placement.y);
  SDL_GetWindowSize(m_window, &placement.width, &placement.height);
  return placement;
}

bool WindowSystem::restorePlacement(const Placement &placement) {
  if (placement.width <= 0 || placement.height <= 0)
    return false;
  // Where it was is only good if all of it is still on one of the connected displays (a monitor may be gone, or the
  // resolution lower)
  bool fits = false;
  int count = 0;
  if (SDL_DisplayID *displays = SDL_GetDisplays(&count)) {
    for (int i = 0; i < count && !fits; ++i) {
      SDL_Rect usable;
      fits = SDL_GetDisplayUsableBounds(displays[i], &usable) &&
             insideOf(usable, placement.x, placement.y, placement.width, placement.height);
    }
    SDL_free(displays);
  }
  if (fits) {
    SDL_SetWindowSize(m_window, placement.width, placement.height);
    SDL_SetWindowPosition(m_window, placement.x, placement.y);
    SDL_SyncWindow(m_window);
  } else {
    // The size alone, if the primary display has room for it; centered either way
    SDL_Rect usable;
    if (usableBoundsOf(nullptr, usable) && placement.width <= usable.w && placement.height <= usable.h)
      SDL_SetWindowSize(m_window, placement.width, placement.height);
    SDL_SetWindowPosition(m_window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    SDL_SyncWindow(m_window);
  }
  return fits;
}

void WindowSystem::show() {
  SDL_ShowWindow(m_window);
  SDL_SyncWindow(m_window);
}

float WindowSystem::getPixelDensity() const {
  const float density = SDL_GetWindowPixelDensity(m_window);
  return density > 0.0f ? density : 1.0f;
}

float WindowSystem::getAutoUiScale() const {
  const float displayScale = SDL_GetWindowDisplayScale(m_window);
  return displayScale > 0.0f ? displayScale / getPixelDensity() : 1.0f;
}
