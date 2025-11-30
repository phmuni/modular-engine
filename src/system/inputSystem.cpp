#include "system/inputSystem.h"
#include "imgui/imgui_impl_sdl3.h"
#include "system/renderSystem.h"
#include "system/windowSystem.h"

InputSystem::InputSystem() { setDefaultKeyBinds(); }

void InputSystem::setDefaultKeyBinds() {
  keyBinds = {{Action::MoveForward, SDL_SCANCODE_W}, {Action::MoveBackward, SDL_SCANCODE_S},
              {Action::MoveLeft, SDL_SCANCODE_A},    {Action::MoveRight, SDL_SCANCODE_D},
              {Action::MoveUp, SDL_SCANCODE_SPACE},  {Action::MoveDown, SDL_SCANCODE_LSHIFT}};
}

bool InputSystem::update(bool *running, SystemManager &systemManager) {
  SDL_Event event;

  auto &windowSystem = systemManager.getSystem<WindowSystem>();
  auto &renderer = systemManager.getSystem<RenderSystem>().getRenderer();

  mouseXOffset = 0.0f;
  mouseYOffset = 0.0f;

  while (SDL_PollEvent(&event)) {
    ImGui_ImplSDL3_ProcessEvent(&event);

    switch (event.type) {
    case SDL_EVENT_QUIT:
      *running = false;
      break;

    case SDL_EVENT_KEY_DOWN:
      keys[event.key.scancode] = true;
      break;

    case SDL_EVENT_KEY_UP:
      keys[event.key.scancode] = false;
      break;

    case SDL_EVENT_MOUSE_MOTION:
      mouseXOffset += event.motion.xrel;
      mouseYOffset += event.motion.yrel;
      break;

    case SDL_EVENT_WINDOW_RESIZED:
      windowSystem.onResize(event.window.data1, event.window.data2, renderer);
      renderer.setViewportSize(event.window.data1, event.window.data2);
      break;

    default:
      break;
    }
  }

  bool togglePressed = isKeyPressed(SDL_SCANCODE_RALT);

  if (togglePressed && !toggleKeyLastState) {
    controlEnabled = !controlEnabled;
  }

  toggleKeyLastState = togglePressed;

  return !quitRequested;
}

bool InputSystem::isActionPressed(Action action) const {
  auto it = keyBinds.find(action);
  if (it != keyBinds.end()) {
    return keys[it->second];
  }
  return false;
}

void InputSystem::setKeyBind(Action action, SDL_Scancode keyCode) { keyBinds[action] = keyCode; }

float InputSystem::getMouseXOffset() const { return mouseXOffset; }

float InputSystem::getMouseYOffset() const { return mouseYOffset; }
bool InputSystem::getMove() const { return controlEnabled; }

bool InputSystem::isQuitRequested() const { return quitRequested; }

bool InputSystem::isKeyPressed(SDL_Scancode key) const { return keys[key]; }

void InputSystem::setMouseXOffset(float xOffset) { this->mouseXOffset = xOffset; }

void InputSystem::setMouseYOffset(float yOffset) { this->mouseYOffset = yOffset; }

void InputSystem::setQuitRequested(bool quit) { this->quitRequested = quit; }

void InputSystem::setKeyPressed(SDL_Scancode key, bool pressed) { this->keys[key] = pressed; }
