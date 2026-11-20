
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

  SDL_WindowFlags flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE;
  if (s_highDensity)
    flags |= SDL_WINDOW_HIGH_PIXEL_DENSITY;
  m_window = SDL_CreateWindow("Engine", static_cast<int>(screenWidth), static_cast<int>(screenHeight), flags);
  if (!m_window) {
    LOG_E("[Window System] Failed to create the window: %s", SDL_GetError());
    return false;
  }

  // Where window units are pixels on a scaled display (Windows at 150 %), the default size would be a small window
  // there: grow it by the scale, but keep it inside the screen
  const float autoScale = getAutoUiScale();
  if (s_highDensity && autoScale > 1.01f) {
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

float WindowSystem::getPixelDensity() const {
  const float density = SDL_GetWindowPixelDensity(m_window);
  return density > 0.0f ? density : 1.0f;
}

float WindowSystem::getAutoUiScale() const {
  const float displayScale = SDL_GetWindowDisplayScale(m_window);
  return displayScale > 0.0f ? displayScale / getPixelDensity() : 1.0f;
}
