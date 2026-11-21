// Lua bindings: the camera, time and tween tables (GameCamera, GameTime, GameTween).
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

void LuaBindings::bindGame() {
  // camera: the active one
  sol::table camera = lua.create_named_table("camera");
  camera.set_function("lookAt", &GameCamera::lookAt, &engine.camera());
  camera.set_function("position", &GameCamera::position, &engine.camera());
  camera.set_function("forward", &GameCamera::forward, &engine.camera());
  // camera.follow(player, { offset = vec3(0, 4, 7), lookAhead = 2, smoothing = 0.15, orbit = true }): trails the
  // entity from behind (offset: x right, y up, z behind it), after physics and scripts; see GameCamera::follow
  camera.set_function("follow", [this](sol::optional<GameEntity> target, sol::optional<sol::table> options) {
    if (target)
      engine.camera().follow(*target, cameraRigOptions(options));
  });
  camera.set_function("stopFollow", &GameCamera::stopFollow, &engine.camera());
  camera.set_function("isFollowing", &GameCamera::isFollowing, &engine.camera());
  // The field of view in degrees
  camera.set_function("fov", &GameCamera::fov, &engine.camera());
  camera.set_function("setFov", &GameCamera::setFov, &engine.camera());

  // time: the scale and pause, the game clock, timers
  sol::table time = lua.create_named_table("time");
  time.set_function("scale", &GameTime::scale, &engine.time());
  time.set_function("setScale", &GameTime::setScale, &engine.time());
  time.set_function("isPaused", &GameTime::isPaused, &engine.time());
  time.set_function("setPaused", &GameTime::setPaused, &engine.time());
  time.set_function("elapsed", &GameTime::elapsed, &engine.time());
  time.set_function("delta", &GameTime::delta, &engine.time());
  // time.after(2, function() ... end) once, time.every(1, fn) repeatedly; both return a handle for time.cancel
  time.set_function("after", [this](float seconds, sol::protected_function action) {
    return engine.time().after(seconds, callback(std::move(action), "time.after"));
  });
  time.set_function("every", [this](float seconds, sol::protected_function action) {
    return engine.time().every(seconds, callback(std::move(action), "time.every"));
  });
  time.set_function("cancel", &GameTime::cancel, &engine.time());

  // tween: animates an entity's "position", "rotation", "scale", "color" or "emission" (a vec3), its light's "intensity"
  // (a number), or a plain number, on game time
  // tween.to(self, "position", vec3(0, 5, 0), 0.5, "easeOutQuad", function() ... end), tween.value(0, 1, 2, "linear", fn)
  sol::table tween = lua.create_named_table("tween");
  tween.set_function("to", [this](sol::optional<GameEntity> entity, const std::string &property, sol::object value,
                                  float seconds, sol::optional<std::string> ease,
                                  sol::optional<sol::protected_function> onComplete) {
    glm::vec3 target(0.0f);
    if (value.is<glm::vec3>())
      target = value.as<glm::vec3>();
    else if (value.get_type() == sol::type::number)
      target = glm::vec3(value.as<float>()); // a number for all three axes: tween.to(self, "scale", 2, 0.3)
    else
      throw std::runtime_error("tween.to: the value is a vec3 (or a number)");
    if (!entity)
      return 0;
    return engine.tween().to(*entity, tweenProperty(property), target, seconds, easeNamed(ease.value_or("linear")),
                             onComplete ? callback(*onComplete, "tween.to") : std::function<void()>());
  });
  tween.set_function("value", [this](float from, float to, float seconds, sol::optional<std::string> ease,
                                     sol::protected_function onValue, sol::optional<sol::protected_function> onComplete) {
    return engine.tween().value(
        from, to, seconds, easeNamed(ease.value_or("linear")),
        [this, onValue](float value) {
          const sol::protected_function_result result = onValue(value);
          if (!result.valid()) {
            const sol::error error = result;
            report("tween.value", error.what());
          }
        },
        onComplete ? callback(*onComplete, "tween.value") : std::function<void()>());
  });
  tween.set_function("cancel", &GameTween::cancel, &engine.tween());
}
