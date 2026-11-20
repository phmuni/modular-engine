
// Input system implementation for handling keyboard and mouse input, including action mapping and toggle states.

#include "systems/inputSystem.h"
#include <algorithm>
#include <iterator>
#include "foundation/core/config.h"
#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"
#include "imgui/imgui_impl_sdl3.h"
#include "systems/gameUISystem.h"
#include "systems/renderSystem.h"
#include "systems/stateSystem.h"
#include "systems/windowSystem.h"
#include <cmath>
#include <fstream>
#include <nlohmann/json.hpp>

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
  std::fill(std::begin(m_gamepadPressed), std::end(m_gamepadPressed), false);
  m_escapePressed = false;

  SDL_Event event;
  while (SDL_PollEvent(&event)) {
    ImGui_ImplSDL3_ProcessEvent(&event);

    // The game UI gets what the debug panels do not want; with the cursor locked, the mouse belongs to the camera
    const ImGuiIO &io = ImGui::GetIO();
    bool mouseAllowed = !state.isToggled(Toggle::CursorLock) && !io.WantCaptureMouse;
    bool usedByUI = gameUI.processEvent(event, mouseAllowed, !io.WantCaptureKeyboard);
    // Escape belongs to the panels while one of them is focused (closing a popup, leaving a field), and to the game UI
    // when it used the key; otherwise it is the pause menu's. Seen before the block below, so it can close the menu.
    if (event.type == SDL_EVENT_KEY_DOWN && event.key.scancode == SDL_SCANCODE_ESCAPE && !event.key.repeat &&
        !usedByUI && !io.WantCaptureKeyboard &&
        !(state.isToggled(Toggle::ShowUI) && GImGui->NavWindow))
      m_escapePressed = true;
    // A menu takes everything: the game gets no key presses or clicks (releases still count, so nothing sticks down)
    usedByUI = usedByUI || m_gameInputBlocked;
    // The game gets clicks whether the cursor is locked or not (a locked cursor is how a shooter aims and fires),
    // except those the game UI or the editor's panels took
    const bool mouseForGame = !usedByUI && !io.WantCaptureMouse;

    switch (event.type) {
    case SDL_EVENT_QUIT:
      *running = false;
      break;

    case SDL_EVENT_KEY_DOWN:
      // A key the UI used (typing in a field, Enter on a button) is not a game key press, nor is the editor's undo and
      // redo (Ctrl+Z, Ctrl+Y). Releases always count.
      if (!usedByUI) {
        const SDL_Scancode key = event.key.scancode;
        if (io.KeyCtrl && (key == SDL_SCANCODE_Z || key == SDL_SCANCODE_Y) && state.isToggled(Toggle::ShowUI))
          break;
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

    // Gamepads: the ones already plugged in also arrive as "added" at start-up
    case SDL_EVENT_GAMEPAD_ADDED:
      openGamepad(event.gdevice.which);
      break;
    case SDL_EVENT_GAMEPAD_REMOVED: {
      auto it = m_gamepads.find(event.gdevice.which);
      if (it != m_gamepads.end()) {
        SDL_CloseGamepad(it->second.gamepad);
        m_gamepads.erase(it);
        LOG_I("[Input] Gamepad disconnected (%d left)", getGamepadCount());
      }
      break;
    }
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
    case SDL_EVENT_GAMEPAD_BUTTON_UP: {
      auto it = m_gamepads.find(event.gbutton.which);
      const int button = event.gbutton.button;
      if (it != m_gamepads.end() && button >= 0 && button < SDL_GAMEPAD_BUTTON_COUNT) {
        const bool down = event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN;
        if (down && m_gameInputBlocked)
          break;
        if (down && !isGamepadButtonDown(static_cast<SDL_GamepadButton>(button)))
          m_gamepadPressed[button] = true;
        it->second.buttons[button] = down;
      }
      break;
    }
    case SDL_EVENT_GAMEPAD_AXIS_MOTION: {
      auto it = m_gamepads.find(event.gaxis.which);
      const int axis = event.gaxis.axis;
      if (it != m_gamepads.end() && axis >= 0 && axis < SDL_GAMEPAD_AXIS_COUNT)
        it->second.axes[axis] = std::clamp(static_cast<float>(event.gaxis.value) / 32767.0f, -1.0f, 1.0f);
      break;
    }

    case SDL_EVENT_WINDOW_RESIZED:
      windowSystem.onResize(event.window.data1, event.window.data2, renderer);
      renderer.setViewportSize(event.window.data1, event.window.data2);
      break;

    default:
      break;
    }
  }

  processToggleKeys(systemManager);

  // Actions now and a frame ago, for pressed and released
  for (auto &[name, bindings] : m_actions) {
    auto &state = m_actionStates[name];
    state.second = state.first;
    state.first = readAction(bindings);
  }
}

void InputSystem::openGamepad(SDL_JoystickID id) {
  if (m_gamepads.count(id))
    return;
  SDL_Gamepad *gamepad = SDL_OpenGamepad(id);
  if (!gamepad) {
    LOG_W("[Input] Could not open gamepad %u: %s", static_cast<unsigned>(id), SDL_GetError());
    return;
  }
  m_gamepads[id].gamepad = gamepad;
  const char *name = SDL_GetGamepadName(gamepad);
  LOG_I("[Input] Gamepad connected: %s", name ? name : "unnamed");
}

// --- Bindings ---------------------------------------------------------------------------------------------------

InputBinding InputBinding::keyboard(SDL_Scancode key) {
  InputBinding binding;
  binding.kind = Kind::Key;
  binding.key = key;
  return binding;
}

InputBinding InputBinding::keys(SDL_Scancode negative, SDL_Scancode positive) {
  InputBinding binding;
  binding.kind = Kind::KeyPair;
  binding.key = negative;
  binding.positiveKey = positive;
  return binding;
}

InputBinding InputBinding::mouse(MouseButton button) {
  InputBinding binding;
  binding.kind = Kind::MouseButton;
  binding.mouseButton = button;
  return binding;
}

InputBinding InputBinding::gamepad(SDL_GamepadButton button) {
  InputBinding binding;
  binding.kind = Kind::GamepadButton;
  binding.gamepadButton = button;
  return binding;
}

InputBinding InputBinding::gamepad(SDL_GamepadAxis axis, float scale) {
  InputBinding binding;
  binding.kind = Kind::GamepadAxis;
  binding.gamepadAxis = axis;
  binding.scale = scale;
  return binding;
}

void InputSystem::bindAction(const std::string &name, std::vector<InputBinding> bindings) {
  m_actions[name] = std::move(bindings);
  m_actionStates[name] = {false, false};
}

void InputSystem::bindAxis(const std::string &name, std::vector<InputBinding> bindings) {
  m_axes[name] = std::move(bindings);
}

float InputSystem::readBinding(const InputBinding &binding) const {
  if (m_gameInputBlocked)
    return 0.0f;
  auto valid = [](SDL_Scancode key) { return key > SDL_SCANCODE_UNKNOWN && key < SDL_SCANCODE_COUNT; };
  switch (binding.kind) {
  case InputBinding::Kind::Key:
    return valid(binding.key) && m_keys[binding.key] ? 1.0f : 0.0f;
  case InputBinding::Kind::KeyPair:
    return (valid(binding.positiveKey) && m_keys[binding.positiveKey] ? 1.0f : 0.0f) -
           (valid(binding.key) && m_keys[binding.key] ? 1.0f : 0.0f);
  case InputBinding::Kind::MouseButton:
    return isMouseButtonDown(binding.mouseButton) ? 1.0f : 0.0f;
  case InputBinding::Kind::GamepadButton:
    return isGamepadButtonDown(binding.gamepadButton) ? 1.0f : 0.0f;
  case InputBinding::Kind::GamepadAxis:
    return getGamepadAxis(binding.gamepadAxis) * binding.scale;
  }
  return 0.0f;
}

bool InputSystem::readAction(const std::vector<InputBinding> &bindings) const {
  for (const InputBinding &binding : bindings) {
    const float value = readBinding(binding);
    if (binding.kind == InputBinding::Kind::GamepadAxis ? value > 0.5f : value != 0.0f)
      return true;
  }
  return false;
}

bool InputSystem::isActionDown(std::string_view name) const {
  auto it = m_actionStates.find(name);
  return it != m_actionStates.end() && it->second.first;
}

bool InputSystem::wasActionPressed(std::string_view name) const {
  auto it = m_actionStates.find(name);
  return it != m_actionStates.end() && it->second.first && !it->second.second;
}

bool InputSystem::wasActionReleased(std::string_view name) const {
  auto it = m_actionStates.find(name);
  return it != m_actionStates.end() && !it->second.first && it->second.second;
}

float InputSystem::getAxis(std::string_view name) const {
  auto it = m_axes.find(name);
  if (it == m_axes.end())
    return 0.0f;
  float strongest = 0.0f;
  for (const InputBinding &binding : it->second) {
    const float value = readBinding(binding);
    if (std::abs(value) > std::abs(strongest))
      strongest = value;
  }
  return std::clamp(strongest, -1.0f, 1.0f);
}

bool InputSystem::isGamepadButtonDown(SDL_GamepadButton button) const {
  if (m_gameInputBlocked || button < 0 || button >= SDL_GAMEPAD_BUTTON_COUNT)
    return false;
  for (const auto &[id, state] : m_gamepads)
    if (state.buttons[button])
      return true;
  return false;
}

bool InputSystem::wasGamepadButtonPressed(SDL_GamepadButton button) const {
  return button >= 0 && button < SDL_GAMEPAD_BUTTON_COUNT && m_gamepadPressed[button];
}

float InputSystem::getGamepadAxis(SDL_GamepadAxis axis) const {
  if (m_gameInputBlocked || axis < 0 || axis >= SDL_GAMEPAD_AXIS_COUNT)
    return 0.0f;
  float strongest = 0.0f;
  for (const auto &[id, state] : m_gamepads)
    if (std::abs(state.axes[axis]) > std::abs(strongest))
      strongest = state.axes[axis];
  // Sticks rest a little off center: nothing inside the dead zone, the rest stretched back to the full range
  const float magnitude = std::abs(strongest);
  if (magnitude <= m_deadZone)
    return 0.0f;
  return std::copysign((magnitude - m_deadZone) / (1.0f - m_deadZone), strongest);
}

bool InputSystem::loadBindings(const std::string &path) {
  const std::string resolved = EngineConfig::resolvePath(path);
  std::ifstream file(resolved);
  if (!file) {
    LOG_W("[Input] Could not open %s", resolved.c_str());
    return false;
  }
  nlohmann::json bindings;
  try {
    bindings = nlohmann::json::parse(file);
  } catch (const std::exception &error) {
    LOG_W("[Input] %s is not valid JSON: %s", resolved.c_str(), error.what());
    return false;
  }

  auto parse = [&](const std::string &spec, InputBinding &binding) {
    if (spec.rfind("mouse:", 0) == 0) {
      const std::string button = spec.substr(6);
      if (button == "left" || button == "middle" || button == "right") {
        binding = InputBinding::mouse(button == "left" ? MouseButton::Left
                                      : button == "middle" ? MouseButton::Middle
                                                           : MouseButton::Right);
        return true;
      }
      return false;
    }
    if (spec.rfind("gamepad:", 0) == 0) {
      std::string name = spec.substr(8);
      float scale = 1.0f;
      if (!name.empty() && name[0] == '-') {
        scale = -1.0f;
        name = name.substr(1);
      }
      // The face buttons by position too, the same on every brand of gamepad (SDL calls them a, b, x, y)
      const std::pair<const char *, SDL_GamepadButton> faces[] = {{"south", SDL_GAMEPAD_BUTTON_SOUTH},
                                                                    {"east", SDL_GAMEPAD_BUTTON_EAST},
                                                                    {"west", SDL_GAMEPAD_BUTTON_WEST},
                                                                    {"north", SDL_GAMEPAD_BUTTON_NORTH}};
      for (const auto &[face, faceButton] : faces)
        if (name == face) {
          binding = InputBinding::gamepad(faceButton);
          return true;
        }
      const SDL_GamepadButton button = SDL_GetGamepadButtonFromString(name.c_str());
      if (button != SDL_GAMEPAD_BUTTON_INVALID) {
        binding = InputBinding::gamepad(button);
        return true;
      }
      const SDL_GamepadAxis axis = SDL_GetGamepadAxisFromString(name.c_str());
      if (axis != SDL_GAMEPAD_AXIS_INVALID) {
        binding = InputBinding::gamepad(axis, scale);
        return true;
      }
      return false;
    }
    const size_t slash = spec.find('/');
    if (slash != std::string::npos && slash > 0 && slash + 1 < spec.size()) {
      const SDL_Scancode negative = SDL_GetScancodeFromName(spec.substr(0, slash).c_str());
      const SDL_Scancode positive = SDL_GetScancodeFromName(spec.substr(slash + 1).c_str());
      if (negative != SDL_SCANCODE_UNKNOWN && positive != SDL_SCANCODE_UNKNOWN) {
        binding = InputBinding::keys(negative, positive);
        return true;
      }
    }
    const SDL_Scancode key = SDL_GetScancodeFromName(spec.c_str());
    if (key == SDL_SCANCODE_UNKNOWN)
      return false;
    binding = InputBinding::keyboard(key);
    return true;
  };
  auto read = [&](const char *section, bool axes) {
    if (!bindings.contains(section) || !bindings[section].is_object())
      return;
    for (const auto &[name, specs] : bindings[section].items()) {
      std::vector<InputBinding> list;
      for (const auto &spec : specs) {
        InputBinding binding;
        if (spec.is_string() && parse(spec.get<std::string>(), binding))
          list.push_back(binding);
        else
          LOG_W("[Input] %s: %s in %s is not a key, mouse button or gamepad input", resolved.c_str(),
                spec.dump().c_str(), name.c_str());
      }
      if (axes)
        bindAxis(name, std::move(list));
      else
        bindAction(name, std::move(list));
    }
  };
  read("actions", false);
  read("axes", true);
  return true;
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

bool InputSystem::isKeyDown(SDL_Scancode key) const { return !m_gameInputBlocked && m_keys[key]; }
bool InputSystem::wasKeyPressed(SDL_Scancode key) const { return m_pressed[key]; }
bool InputSystem::wasKeyReleased(SDL_Scancode key) const { return m_released[key]; }

bool InputSystem::isMouseButtonDown(MouseButton button) const {
  return !m_gameInputBlocked && m_mouseDown[static_cast<int>(button)];
}
bool InputSystem::wasMouseButtonPressed(MouseButton button) const { return m_mousePressed[static_cast<int>(button)]; }
bool InputSystem::wasMouseButtonReleased(MouseButton button) const {
  return m_mouseReleased[static_cast<int>(button)];
}

bool InputSystem::isActionPressed(Action action) const {
  auto it = m_keyBinds.find(action);
  return !m_gameInputBlocked && it != m_keyBinds.end() && m_keys[it->second];
}

void InputSystem::setKeyBind(Action action, SDL_Scancode keyCode) { m_keyBinds[action] = keyCode; }

float InputSystem::getMouseXOffset() const { return m_mouseXOffset; }
float InputSystem::getMouseYOffset() const { return m_mouseYOffset; }
