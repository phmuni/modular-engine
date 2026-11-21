// Lua bindings: the input table (actions, keys, mouse, text entry, gamepad).
// A binding only converts types and calls the C++ game API: see the rule at the top of luaBindings.h.

#include <sol/sol.hpp> // SOL_ALL_SAFETIES_ON comes from the build, the same for every file that includes sol2

#include "systems/lua/luaBindings.h"

#include "components/animator.h"
#include "components/camera.h"
#include "components/characterController.h"
#include "components/hidden.h"
#include "components/light.h"
#include "components/model.h"
#include "components/name.h"
#include "components/rigidBody.h"
#include "components/script.h"
#include "components/transform.h"
#include "foundation/core/config.h"
#include "foundation/core/engine.h"
#include "foundation/core/gameEntity.h"
#include "foundation/core/logger.h"
#include "systems/inputSystem.h"
#include "systems/navigationSystem.h"
#include "systems/physicsSystem.h"
#include "systems/playerSettings.h"
#include "systems/renderSystem.h"
#include "systems/saveData.h"
#include <RmlUi/Core.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <stdexcept>
#include <optional>
#include <tuple>
#include <nlohmann/json.hpp>
#include <unordered_map>
#include <variant>
#include <vector>

void LuaBindings::bindInput() {
  // input: actions and axes (see InputSystem::bindAction), keys by SDL's names ("Space", "Left Shift")
  sol::table input = lua.create_named_table("input");
  input.set_function("axis", [this](const std::string &name) { return engine.input().getAxis(name); });
  input.set_function("down", [this](const std::string &name) { return engine.input().isActionDown(name); });
  input.set_function("pressed", [this](const std::string &name) { return engine.input().wasActionPressed(name); });
  input.set_function("released", [this](const std::string &name) { return engine.input().wasActionReleased(name); });
  input.set_function("key", [this](const std::string &name) { return engine.input().isKeyDown(name); });
  input.set_function("keyPressed", [this](const std::string &name) { return engine.input().wasKeyPressed(name); });
  input.set_function("keyReleased", [this](const std::string &name) { return engine.input().wasKeyReleased(name); });
  input.set_function("mouse", [this]() {
    const glm::vec2 position = engine.input().getMousePosition();
    return std::make_tuple(position.x, position.y);
  });
  // Buttons: 1 left (the default), 2 middle, 3 right
  input.set_function("click", [this](sol::optional<int> button) {
    return engine.input().wasMouseButtonPressed(static_cast<MouseButton>(button.value_or(1)));
  });
  input.set_function("mouseReleased", [this](sol::optional<int> button) {
    return engine.input().wasMouseButtonReleased(static_cast<MouseButton>(button.value_or(1)));
  });
  input.set_function("mouseDown", [this](sol::optional<int> button) {
    return engine.input().isMouseButtonDown(static_cast<MouseButton>(button.value_or(1)));
  });
  input.set_function("wheel", [this]() { return engine.input().getMouseWheel(); });
  // How far the mouse moved this frame, in pixels: dx, dy (for mouse-look, with the mouse captured)
  input.set_function("mouseDelta", [this]() {
    const glm::vec2 delta = engine.input().getMouseDelta();
    return std::make_tuple(delta.x, delta.y);
  });
  // input.captureMouse(true) hides and holds the cursor for mouse-look; false frees it for clicking (see
  // Engine::setMouseCaptured). input.showCursor(false) hides it while it is free.
  input.set_function("captureMouse", [this](bool captured) { engine.setMouseCaptured(captured); });
  input.set_function("isMouseCaptured", [this]() { return engine.isMouseCaptured(); });
  input.set_function("showCursor", [this](bool visible) { engine.setCursorVisible(visible); });
  input.set_function("isCursorVisible", [this]() { return engine.isCursorVisible(); });
  // Text entry: input.startText("", 12) takes the typed characters (and Backspace) into input.text() instead of the
  // game's keys, until input.stopText(); input.typed() is only this frame's characters
  input.set_function("startText", [this](sol::optional<std::string> initial, sol::optional<size_t> maxLength) {
    engine.input().startTextInput(initial.value_or(""), maxLength.value_or(0));
  });
  input.set_function("stopText", [this]() { engine.input().stopTextInput(); });
  input.set_function("isTextActive", [this]() { return engine.input().isTextInputActive(); });
  input.set_function("text", [this]() { return engine.input().getText(); });
  input.set_function("setText", [this](const std::string &text) { engine.input().setText(text); });
  input.set_function("typed", [this]() { return engine.input().getTypedText(); });
  // Gamepad rumble: low (big motor) and high (small motor) strengths 0 to 1, for some seconds; true if a gamepad took it
  input.set_function("rumble", [this](float low, float high, sol::optional<float> seconds) {
    return engine.input().rumble(low, high, seconds.value_or(0.2f));
  });
  input.set_function("stopRumble", [this]() { engine.input().stopRumble(); });
  input.set_function("bindings", [this](const std::string &path) { return engine.input().loadBindings(path); });
}
