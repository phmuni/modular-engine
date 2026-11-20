#pragma once
// Input system for handling keyboard/mouse input and mapping to actions.

#include "SDL3/SDL_scancode.h"
#include "foundation/ecs/systemManager.h"
#include "systems/stateSystem.h"
#include <SDL3/SDL.h>
#include <glm/glm.hpp>
#include <map>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

enum class Action { MoveForward, MoveBackward, MoveLeft, MoveRight, MoveUp, MoveDown };

// Values match SDL_BUTTON_LEFT/MIDDLE/RIGHT
enum class MouseButton { Left = 1, Middle = 2, Right = 3 };

// Something an action or an axis reads. Gamepad buttons and axes use SDL's names: SDL_GAMEPAD_BUTTON_SOUTH is the
// bottom face button (A on Xbox, Cross on PlayStation), SDL_GAMEPAD_AXIS_LEFTX the left stick's x, and so on.
struct InputBinding {
  enum class Kind { Key, KeyPair, MouseButton, GamepadButton, GamepadAxis };
  Kind kind = Kind::Key;
  SDL_Scancode key = SDL_SCANCODE_UNKNOWN;         // Key; KeyPair: the one toward -1
  SDL_Scancode positiveKey = SDL_SCANCODE_UNKNOWN; // KeyPair: the one toward +1
  MouseButton mouseButton = MouseButton::Left;
  SDL_GamepadButton gamepadButton = SDL_GAMEPAD_BUTTON_INVALID;
  SDL_GamepadAxis gamepadAxis = SDL_GAMEPAD_AXIS_INVALID;
  float scale = 1.0f; // GamepadAxis: -1 turns it around (a stick's y is positive downward)

  static InputBinding keyboard(SDL_Scancode key);
  // An axis from two keys: `negative` gives -1, `positive` +1, e.g. keys(SDL_SCANCODE_A, SDL_SCANCODE_D)
  static InputBinding keys(SDL_Scancode negative, SDL_Scancode positive);
  static InputBinding mouse(MouseButton button);
  static InputBinding gamepad(SDL_GamepadButton button);
  static InputBinding gamepad(SDL_GamepadAxis axis, float scale = 1.0f);
};

class InputSystem : public BaseSystem {
public:
  InputSystem();

  void update(bool *running, SystemManager &systemManager);

  // While blocked (a menu is open), the game sees no keys, clicks, actions or gamepad input: everything reads as
  // idle, and presses that arrive meanwhile are dropped. The engine's toggle keys (F-keys) are blocked too.
  void setGameInputBlocked(bool blocked) { m_gameInputBlocked = blocked; }
  bool isGameInputBlocked() const { return m_gameInputBlocked; }
  // Escape went down this frame and nothing else wanted it: not the game UI (a focused field), nor the editor's
  // panels (a text field, or a focused window). The pause menu's key.
  bool wasEscapePressed() const { return m_escapePressed; }

  // Held down now
  bool isKeyDown(SDL_Scancode key) const;
  // Went down / came up during this frame (once per press: keyboard auto-repeat does not count)
  bool wasKeyPressed(SDL_Scancode key) const;
  bool wasKeyReleased(SDL_Scancode key) const;
  bool isActionPressed(Action action) const;

  // Mouse, for the game: clicks the game UI or the editor handled are left out
  bool isMouseButtonDown(MouseButton button) const;
  bool wasMouseButtonPressed(MouseButton button) const;
  bool wasMouseButtonReleased(MouseButton button) const;
  // Cursor position in window pixels (top-left origin); wheel movement this frame (positive is away from the user)
  glm::vec2 getMousePosition() const { return m_mousePosition; }
  float getMouseWheel() const { return m_mouseWheel; }

  float getMouseXOffset() const;
  float getMouseYOffset() const;

  // Named actions (pressed or not) and axes (-1 to 1), each read from any of its bindings, so the game asks for
  // "jump" rather than a key, and a gamepad works next to the keyboard:
  //   bindAction("jump", {InputBinding::keyboard(SDL_SCANCODE_SPACE), InputBinding::gamepad(SDL_GAMEPAD_BUTTON_SOUTH)});
  //   bindAxis("moveX", {InputBinding::keys(SDL_SCANCODE_A, SDL_SCANCODE_D), InputBinding::gamepad(SDL_GAMEPAD_AXIS_LEFTX)});
  // Binding a name again replaces its bindings. An axis binding read as an action counts past half way (a trigger);
  // a key or button read as an axis gives 1.
  void bindAction(const std::string &name, std::vector<InputBinding> bindings);
  void bindAxis(const std::string &name, std::vector<InputBinding> bindings);
  bool isActionDown(std::string_view name) const;
  bool wasActionPressed(std::string_view name) const; // went down this frame
  bool wasActionReleased(std::string_view name) const;
  float getAxis(std::string_view name) const; // the strongest of its bindings
  // Bindings from a JSON file, so players or designers can change them without a build:
  //   { "actions": { "jump": ["Space", "gamepad:south"], "fire": ["mouse:left", "gamepad:righttrigger"] },
  //     "axes":    { "moveX": ["A/D", "Left/Right", "gamepad:leftx"], "moveY": ["S/W", "gamepad:-lefty"] } }
  // Keys by SDL's names ("Space", "Left Shift"), "neg/pos" pairs for axes, "gamepad:" + a button (south, east, west,
  // north for the face buttons, or SDL's names: start, leftshoulder, dpup...) or an axis (leftx, lefty, rightx,
  // righty, lefttrigger, righttrigger; "-" in front turns it around), "mouse:left|middle|right". False if the file
  // cannot be read.
  bool loadBindings(const std::string &path);

  // Gamepads, connected or not at any time; with several, their buttons and sticks add up
  int getGamepadCount() const { return static_cast<int>(m_gamepads.size()); }
  bool isGamepadButtonDown(SDL_GamepadButton button) const;
  bool wasGamepadButtonPressed(SDL_GamepadButton button) const;
  float getGamepadAxis(SDL_GamepadAxis axis) const; // -1 to 1 (triggers 0 to 1), past the dead zone
  void setGamepadDeadZone(float deadZone) { m_deadZone = deadZone; }

  void setKeyBind(Action action, SDL_Scancode keyCode);
  void bindToggleKey(SDL_Scancode key, Toggle toggle);

private:
  bool m_gameInputBlocked = false;
  bool m_escapePressed = false; // this frame
  bool m_keys[SDL_SCANCODE_COUNT]{false};
  bool m_pressed[SDL_SCANCODE_COUNT]{false};  // this frame
  bool m_released[SDL_SCANCODE_COUNT]{false}; // this frame

  static constexpr int kMouseButtons = 6; // indexed by SDL button number (1-5)
  bool m_mouseDown[kMouseButtons]{false};
  bool m_mousePressed[kMouseButtons]{false};
  bool m_mouseReleased[kMouseButtons]{false};
  glm::vec2 m_mousePosition{0.0f};
  float m_mouseWheel = 0.0f;

  float m_mouseXOffset = 0.0f;
  float m_mouseYOffset = 0.0f;

  std::unordered_map<Action, SDL_Scancode> m_keyBinds;
  void setDefaultKeyBinds();
  void setDefaultToggleKeyBinds();

  std::unordered_map<SDL_Scancode, Toggle> m_toggleKeyBinds;
  std::unordered_map<SDL_Scancode, bool> m_prevKeyState;

  void processToggleKeys(SystemManager &systemManager);

  struct GamepadState {
    SDL_Gamepad *gamepad = nullptr;
    bool buttons[SDL_GAMEPAD_BUTTON_COUNT]{false};
    float axes[SDL_GAMEPAD_AXIS_COUNT]{0.0f};
  };
  std::map<SDL_JoystickID, GamepadState> m_gamepads;
  bool m_gamepadPressed[SDL_GAMEPAD_BUTTON_COUNT]{false}; // this frame, on any gamepad
  float m_deadZone = 0.2f;
  void openGamepad(SDL_JoystickID id);

  std::map<std::string, std::vector<InputBinding>, std::less<>> m_actions;
  std::map<std::string, std::vector<InputBinding>, std::less<>> m_axes;
  // Each action's state this frame and the last, for pressed / released
  std::map<std::string, std::pair<bool, bool>, std::less<>> m_actionStates;
  bool readAction(const std::vector<InputBinding> &bindings) const;
  float readBinding(const InputBinding &binding) const;
};
