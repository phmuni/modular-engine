
// Input system implementation for handling keyboard and mouse input, including action mapping and toggle states.

#include "systems/inputSystem.h"
#include <algorithm>
#include <iterator>
#include "foundation/core/config.h"
#include "imgui/imgui.h"
#include "imgui/imgui_impl_sdl3.h"
#include "systems/gameUISystem.h"
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
  auto &gameUI = systemManager.getSystem<GameUISystem>();
  const auto &state = systemManager.getSystem<StateSystem>();

  m_mouseXOffset = 0.0f;
  m_mouseYOffset = 0.0f;
  m_mouseWheel = 0.0f;
  std::fill(std::begin(m_pressed), std::end(m_pressed), false);
  std::fill(std::begin(m_released), std::end(m_released), false);
  std::fill(std::begin(m_mousePressed), std::end(m_mousePressed), false);
  std::fill(std::begin(m_mouseReleased), std::end(m_mouseReleased), false);

  SDL_Event event;
  while (SDL_PollEvent(&event)) {
    ImGui_ImplSDL3_ProcessEvent(&event);

    // The game UI gets what the debug panels do not want; with the cursor locked, the mouse belongs to the camera
    const ImGuiIO &io = ImGui::GetIO();
    bool mouseAllowed = !state.isToggled(Toggle::CursorLock) && !io.WantCaptureMouse;
    bool usedByUI = gameUI.processEvent(event, mouseAllowed, !io.WantCaptureKeyboard);
    // The game gets clicks whether the cursor is locked or not (a locked cursor is how a shooter aims and fires),
    // except those the game UI or the editor's panels took
    const bool mouseForGame = !usedByUI && !io.WantCaptureMouse;

    switch (event.type) {
    case SDL_EVENT_QUIT:
      *running = false;
      break;

    case SDL_EVENT_KEY_DOWN:
      // A key the UI used (typing in a field, Enter on a button) is not a game key press. Releases always count.
      if (!usedByUI) {
        const SDL_Scancode key = event.key.scancode;
        m_pressed[key] = m_pressed[key] || (!event.key.repeat && !m_keys[key]);
        m_keys[key] = true;
      }
      break;

    case SDL_EVENT_KEY_UP:
      m_released[event.key.scancode] = m_released[event.key.scancode] || m_keys[event.key.scancode];
      m_keys[event.key.scancode] = false;
      break;

    case SDL_EVENT_MOUSE_MOTION:
      m_mouseXOffset += event.motion.xrel;
      m_mouseYOffset += event.motion.yrel;
      m_mousePosition = {event.motion.x, event.motion.y};
      break;

    case SDL_EVENT_MOUSE_BUTTON_DOWN:
      if (mouseForGame && event.button.button < kMouseButtons) {
        m_mousePressed[event.button.button] = true;
        m_mouseDown[event.button.button] = true;
      }
      break;

    case SDL_EVENT_MOUSE_BUTTON_UP:
      if (event.button.button < kMouseButtons) {
        m_mouseReleased[event.button.button] = m_mouseReleased[event.button.button] || m_mouseDown[event.button.button];
        m_mouseDown[event.button.button] = false;
      }
      break;

    case SDL_EVENT_MOUSE_WHEEL:
      if (mouseForGame)
        m_mouseWheel += event.wheel.y;
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

bool InputSystem::isKeyDown(SDL_Scancode key) const { return m_keys[key]; }
bool InputSystem::wasKeyPressed(SDL_Scancode key) const { return m_pressed[key]; }
bool InputSystem::wasKeyReleased(SDL_Scancode key) const { return m_released[key]; }

bool InputSystem::isMouseButtonDown(MouseButton button) const { return m_mouseDown[static_cast<int>(button)]; }
bool InputSystem::wasMouseButtonPressed(MouseButton button) const { return m_mousePressed[static_cast<int>(button)]; }
bool InputSystem::wasMouseButtonReleased(MouseButton button) const {
  return m_mouseReleased[static_cast<int>(button)];
}

bool InputSystem::isActionPressed(Action action) const {
  auto it = m_keyBinds.find(action);
  return it != m_keyBinds.end() && m_keys[it->second];
}

void InputSystem::setKeyBind(Action action, SDL_Scancode keyCode) { m_keyBinds[action] = keyCode; }

float InputSystem::getMouseXOffset() const { return m_mouseXOffset; }
float InputSystem::getMouseYOffset() const { return m_mouseYOffset; }
