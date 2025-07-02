#include "manager/windowManager.h"

bool WindowManager::initialize(float screenWidth, float screenHeight) {

  // OpenGL Initialization
  if (!SDL_Init(SDL_INIT_VIDEO)) {
    SDL_Log("Failed to initialize SDL: %s", SDL_GetError());
    return false;
  }

  window = SDL_CreateWindow("Engine", screenWidth, screenHeight, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
  if (!window) {
    SDL_Log("Failed to create the window: %s", SDL_GetError());
    return false;
  }

  glContext = SDL_GL_CreateContext(window);
  if (!glContext) {
    SDL_Log("Failed to create the OpenGL context: %s", SDL_GetError());
    return false;
  }

  // GLAD Initialization
  if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) {
    SDL_Log("Failed to initialize GLAD.");
    return false;
  }

  // Flags
  glEnable(GL_DEPTH_TEST);                      // Enables depth testing
  SDL_GL_SetSwapInterval(1);                    // Enable or Disable V-Sync
  SDL_SetWindowRelativeMouseMode(window, true); // Hides and confines the cursor

  return true;
}

SDL_Window *WindowManager::getWindow() const { return window; }
SDL_GLContext WindowManager::getContext() const { return glContext; }