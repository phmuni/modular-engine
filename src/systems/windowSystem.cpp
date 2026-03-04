// Window system: SDL3 window creation and OpenGL context setup.
#include "systems/windowSystem.h"

WindowSystem::WindowSystem(float screenWidth, float screenHeight) : m_window(nullptr), m_glContext(nullptr) {
  initialize(screenWidth, screenHeight);
};

bool WindowSystem::initialize(float screenWidth, float screenHeight) {
  if (!SDL_Init(SDL_INIT_VIDEO)) {
    SDL_Log("Failed to initialize SDL: %s", SDL_GetError());
    return false;
  }

  // Request OpenGL 4.1 Core Profile (maximum supported on macOS)
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
#ifdef __APPLE__
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
#endif

  m_window = SDL_CreateWindow("Engine", screenWidth, screenHeight, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
  if (!m_window) {
    SDL_Log("Failed to create the window: %s", SDL_GetError());
    return false;
  }

  m_glContext = SDL_GL_CreateContext(m_window);
  if (!m_glContext) {
    SDL_Log("Failed to create the OpenGL context: %s", SDL_GetError());
    return false;
  }

  if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) {
    SDL_Log("Failed to initialize GLAD.");
    return false;
  }

  glEnable(GL_DEPTH_TEST);
  SDL_GL_SetSwapInterval(1);
  SDL_SetWindowRelativeMouseMode(m_window, true);

  return true;
}

void WindowSystem::onResize(int newWidth, int newHeight, Renderer &renderer) {
  renderer.setViewportSize(newWidth, newHeight);
}

SDL_Window *WindowSystem::getWindow() const { return m_window; }
SDL_GLContext WindowSystem::getContext() const { return m_glContext; }

void WindowSystem::setCursor(bool boolean) { SDL_SetWindowRelativeMouseMode(m_window, boolean); }