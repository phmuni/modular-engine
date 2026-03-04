// Input system: SDL event handling and configurable key binds.
#include "systems/inputSystem.h"
#include "foundation/core/config.h"
#include "imgui/imgui_impl_sdl3.h"
#include "systems/renderSystem.h"
#include "systems/stateSystem.h"
#include "systems/windowSystem.h"

InputSystem::InputSystem() {
  setDefaultKeyBinds();
  setDefaultToggleKeyBinds();
}

void InputSystem::setDefaultKeyBinds() {
  m_keyBinds = {
      {Action::MoveForward, SDL_SCANCODE_W}, {Action::MoveBackward, SDL_SCANCODE_S},
      {Action::MoveLeft, SDL_SCANCODE_A},    {Action::MoveRight, SDL_SCANCODE_D},
      {Action::MoveUp, SDL_SCANCODE_SPACE},  {Action::MoveDown, SDL_SCANCODE_LSHIFT},
  };
}

void InputSystem::setDefaultToggleKeyBinds() {
  for (int i = 0; i < EngineConfig::DEFAULT_TOGGLE_KEYBINDS_COUNT; ++i) {
    m_toggleKeyBinds[EngineConfig::DEFAULT_TOGGLE_KEYBINDS[i].first] = EngineConfig::DEFAULT_TOGGLE_KEYBINDS[i].second;
    m_prevKeyState[EngineConfig::DEFAULT_TOGGLE_KEYBINDS[i].first] = false;
  }
}

void InputSystem::update(bool *running, SystemManager &systemManager) {
  auto &windowSystem = systemManager.getSystem<WindowSystem>();
  auto &renderer = systemManager.getSystem<RenderSystem>().getRenderer();

  m_mouseXOffset = 0.0f;
  m_mouseYOffset = 0.0f;

  SDL_Event event;
  while (SDL_PollEvent(&event)) {
    ImGui_ImplSDL3_ProcessEvent(&event);

    switch (event.type) {
    case SDL_EVENT_QUIT:
      *running = false;
      break;

    case SDL_EVENT_KEY_DOWN:
      m_keys[event.key.scancode] = true;
      break;

    case SDL_EVENT_KEY_UP:
      m_keys[event.key.scancode] = false;
      break;

    case SDL_EVENT_MOUSE_MOTION:
      m_mouseXOffset += event.motion.xrel;
      m_mouseYOffset += event.motion.yrel;
      break;

    case SDL_EVENT_WINDOW_RESIZED:
      windowSystem.onResize(event.window.data1, event.window.data2, renderer);
      renderer.setViewportSize(event.window.data1, event.window.data2);
      break;

    default:
      break;
    }
  }

  processToggleKeys(systemManager);
}

void InputSystem::processToggleKeys(SystemManager &systemManager) {
  auto &state = systemManager.getSystem<StateSystem>();

  for (auto &[key, toggle] : m_toggleKeyBinds) {
    bool pressed = m_keys[key];
    bool wasPrev = m_prevKeyState[key];

    if (pressed && !wasPrev) {
      state.flipToggle(toggle);
    }

    m_prevKeyState[key] = pressed;
  }
}

void InputSystem::bindToggleKey(SDL_Scancode key, Toggle toggle) {
  m_toggleKeyBinds[key] = toggle;
  m_prevKeyState[key] = false;
}

bool InputSystem::isKeyPressed(SDL_Scancode key) const { return m_keys[key]; }

bool InputSystem::isActionPressed(Action action) const {
  auto it = m_keyBinds.find(action);
  return it != m_keyBinds.end() && m_keys[it->second];
}

void InputSystem::setKeyBind(Action action, SDL_Scancode keyCode) { m_keyBinds[action] = keyCode; }

float InputSystem::getMouseXOffset() const { return m_mouseXOffset; }
float InputSystem::getMouseYOffset() const { return m_mouseYOffset; }
