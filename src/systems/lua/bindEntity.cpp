// Lua bindings: the Entity type, the C++ GameEntity as scripts see it.
// A binding only converts types and calls the C++ game API: see the rule at the top of luaBindings.h.

#include <sol/sol.hpp> // SOL_ALL_SAFETIES_ON comes from the build, the same for every file that includes sol2

#include "systems/lua/luaBindings.h"

#include "components/animator.h"
#include "components/camera.h"
#include "components/characterController.h"
#include "components/hidden.h"
#include "components/joint.h"
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

namespace {
// The table of Entity:setJoint as a Joint: a key left out keeps the component's default; a type or motor that is not
// one of the names is an error
Joint luaToJoint(const sol::table &table) {
  Joint joint;
  auto get = [&table](const char *key, auto &field) {
    using T = std::decay_t<decltype(field)>;
    if (sol::optional<T> value = table[key])
      field = *value;
  };
  if (sol::optional<std::string> type = table["type"]) {
    if (*type == "hinge")
      joint.type = JointType::Hinge;
    else if (*type == "fixed")
      joint.type = JointType::Fixed;
    else if (*type == "distance")
      joint.type = JointType::Distance;
    else if (*type == "slider")
      joint.type = JointType::Slider;
    else
      throw sol::error("setJoint: type is \"hinge\", \"fixed\", \"distance\" or \"slider\", not \"" + *type + "\"");
  }
  if (sol::optional<std::string> motor = table["motor"]) {
    if (*motor == "off")
      joint.motor = JointMotor::Off;
    else if (*motor == "velocity")
      joint.motor = JointMotor::Velocity;
    else if (*motor == "position")
      joint.motor = JointMotor::Position;
    else
      throw sol::error("setJoint: motor is \"off\", \"velocity\" or \"position\", not \"" + *motor + "\"");
  }
  get("target", joint.target);
  get("anchor", joint.anchor);
  get("targetAnchor", joint.targetAnchor);
  get("axis", joint.axis);
  get("limits", joint.limits);
  get("min", joint.min);
  get("max", joint.max);
  get("minDistance", joint.minDistance);
  get("maxDistance", joint.maxDistance);
  get("motorSpeed", joint.motorSpeed);
  get("motorTarget", joint.motorTarget);
  get("maxMotorForce", joint.maxMotorForce);
  get("springFrequency", joint.springFrequency);
  get("springDamping", joint.springDamping);
  get("collideConnected", joint.collideConnected);
  return joint;
}

// Custom shader values (Entity:setShaderParam / shaderParam): a number, a vec3, a table of 2 to 4 numbers (vec2, vec3
// or vec4), or a texture's path
ShaderValue luaToShaderValue(const sol::object &value) {
  switch (value.get_type()) {
  case sol::type::number:
    return value.as<float>();
  case sol::type::string:
    return value.as<std::string>();
  case sol::type::userdata:
    if (value.is<glm::vec3>())
      return value.as<glm::vec3>();
    break;
  case sol::type::table: {
    const sol::table table = value.as<sol::table>();
    const size_t length = table.size();
    float v[4] = {0, 0, 0, 0};
    if (length >= 2 && length <= 4) {
      for (size_t i = 0; i < length; ++i) {
        const sol::object item = table[i + 1];
        if (item.get_type() != sol::type::number)
          throw std::runtime_error("setShaderParam: a vector is a table of numbers");
        v[i] = item.as<float>();
      }
      if (length == 2)
        return glm::vec2(v[0], v[1]);
      if (length == 3)
        return glm::vec3(v[0], v[1], v[2]);
      return glm::vec4(v[0], v[1], v[2], v[3]);
    }
    break;
  }
  default:
    break;
  }
  throw std::runtime_error("setShaderParam: the value is a number, a vec3, a table of 2 to 4 numbers or a texture path");
}

sol::object shaderValueToLua(sol::state_view lua, const std::optional<ShaderValue> &value) {
  if (!value)
    return sol::lua_nil;
  return std::visit(
      [&](const auto &v) -> sol::object {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, glm::vec2>)
          return sol::make_object(lua, lua.create_table_with(1, v.x, 2, v.y));
        else if constexpr (std::is_same_v<T, glm::vec4>)
          return sol::make_object(lua, lua.create_table_with(1, v.x, 2, v.y, 3, v.z, 4, v.w));
        else
          return sol::make_object(lua, v);
      },
      *value);
}
} // namespace

void LuaBindings::bindEntity() {
  // The Lua `Entity` is the C++ GameEntity: its members are bound as they are, so a script and a C++ game do the
  // same things the same way. Only the script's own fields (self.speed = 5) are Lua's: they live here, by entity.
  lua.new_usertype<GameEntity>(
      "Entity", sol::no_constructor,
      sol::meta_function::equal_to, [](const GameEntity &a, const GameEntity &b) { return a == b; },
      sol::meta_function::to_string, &GameEntity::describe,
      // Unknown keys are the entity's own fields: self.speed = 5, other.health. Taken as objects, not stack
      // references: inside a coroutine the stack is the coroutine's, not the state these tables belong to.
      sol::meta_function::index,
      [this](GameEntity &e, sol::object key) -> sol::object { return fieldsOf(e.id())[key]; },
      sol::meta_function::new_index,
      [this](GameEntity &e, sol::object key, sol::object value) { fieldsOf(e.id())[key] = value; },

      "id", sol::readonly_property(&GameEntity::id),
      "valid", sol::readonly_property(&GameEntity::valid),
      "runtime", sol::readonly_property(&GameEntity::isRuntime),
      "name", sol::property(&GameEntity::name, &GameEntity::setName),
      "transform", sol::readonly_property(&GameEntity::transform),
      "position", sol::property(&GameEntity::position, &GameEntity::setPosition),
      "rotation", sol::property(&GameEntity::rotation, &GameEntity::setRotation),
      "scale", sol::property(&GameEntity::scale, &GameEntity::setScale),
      "forward", sol::readonly_property(&GameEntity::forward),
      "right", sol::readonly_property(&GameEntity::right),
      "light", sol::readonly_property(&GameEntity::light),
      // The entity itself when it has a Script component, else nil
      "script", sol::readonly_property([this](GameEntity &e) -> sol::object {
        return e.hasScript() ? sol::make_object(lua, e) : sol::lua_nil;
      }),

      "destroy", &GameEntity::destroy,
      "has", &GameEntity::has,
      "parent", [this](GameEntity &e) { return entityObject(e.parent()); },
      "setParent", [](GameEntity &e, sol::optional<GameEntity> parent) {
        return e.setParent(parent.value_or(GameEntity()));
      },
      "children", [](GameEntity &e) { return sol::as_table(e.children()); },

      // A coroutine that ends with the entity: self:run(function() wait(1) self:setColor(...) end)
      "run", [this](GameEntity &e, sol::protected_function function) {
        return startRoutine(function, e.id(), "self:run");
      },

      // Tags
      "addTag", &GameEntity::addTag,
      "removeTag", &GameEntity::removeTag,
      "hasTag", &GameEntity::hasTag,
      "tags", [](GameEntity &e) { return sol::as_table(e.tags()); },

      // Looks
      // The optional last argument limits the look to one part of the model: a submesh number (from 0) or the
      // name of an OBJ object/group or glTF node; without it every part changes
      "setColor", [](GameEntity &e, const glm::vec3 &color, sol::optional<sol::object> part) {
        if (part && part->is<std::string>())
          e.setColor(color, part->as<std::string>());
        else
          e.setColor(color, part && part->is<int>() ? part->as<int>() : -1);
      },
      "color", &GameEntity::color,
      "setEmission", [](GameEntity &e, const glm::vec3 &color, sol::optional<float> strength,
                        sol::optional<sol::object> part) {
        if (part && part->is<std::string>())
          e.setEmission(color, strength.value_or(1.0f), part->as<std::string>());
        else
          e.setEmission(color, strength.value_or(1.0f), part && part->is<int>() ? part->as<int>() : -1);
      },
      "setVisible", &GameEntity::setVisible,
      "visible", sol::readonly_property(&GameEntity::isVisible),
      "setTexture", [](GameEntity &e, const std::string &slot, const std::string &path, sol::optional<sol::object> part) {
        if (part && part->is<std::string>())
          e.setTexture(slot, path, part->as<std::string>());
        else
          e.setTexture(slot, path, part && part->is<int>() ? part->as<int>() : -1);
      },
      "setMetallic", [](GameEntity &e, float metallic, sol::optional<sol::object> part) {
        if (part && part->is<std::string>())
          e.setMetallic(metallic, part->as<std::string>());
        else
          e.setMetallic(metallic, part && part->is<int>() ? part->as<int>() : -1);
      },
      "setRoughness", [](GameEntity &e, float roughness, sol::optional<sol::object> part) {
        if (part && part->is<std::string>())
          e.setRoughness(roughness, part->as<std::string>());
        else
          e.setRoughness(roughness, part && part->is<int>() ? part->as<int>() : -1);
      },
      "setUnlit", [](GameEntity &e, bool unlit, sol::optional<sol::object> part) {
        if (part && part->is<std::string>())
          e.setUnlit(unlit, part->as<std::string>());
        else
          e.setUnlit(unlit, part && part->is<int>() ? part->as<int>() : -1);
      },

      // Collision boxes: fitCollision(fit, isStatic) sizes its box to its model; isTouching(other) tests it against another's
      "fitCollision", [](GameEntity &e, sol::optional<float> fit, sol::optional<bool> isStatic) {
        e.fitCollision(fit.value_or(1.0f), isStatic.value_or(false));
      },
      "isTouching", [](GameEntity &e, sol::optional<GameEntity> other) { return other && e.isTouching(*other); },

      // Custom surface shaders; the optional last argument is a part, as for setColor: none means every part
      "setShader", [](GameEntity &e, sol::optional<std::string> path, sol::optional<sol::object> part) {
        if (part && part->is<std::string>())
          e.setShader(path.value_or(""), part->as<std::string>());
        else
          e.setShader(path.value_or(""), part && part->is<int>() ? part->as<int>() : -1);
      },
      "shader", [](GameEntity &e, sol::optional<sol::object> part) {
        if (part && part->is<std::string>())
          return e.shader(part->as<std::string>());
        return e.shader(part && part->is<int>() ? part->as<int>() : 0);
      },
      "setShaderParam", [](GameEntity &e, const std::string &name, const sol::object &value,
                           sol::optional<sol::object> part) {
        if (part && part->is<std::string>())
          e.setShaderParam(name, luaToShaderValue(value), part->as<std::string>());
        else
          e.setShaderParam(name, luaToShaderValue(value), part && part->is<int>() ? part->as<int>() : -1);
      },
      "shaderParam", [](sol::this_state state, GameEntity &e, const std::string &name, sol::optional<sol::object> part) {
        if (part && part->is<std::string>())
          return shaderValueToLua(sol::state_view(state), e.shaderParam(name, part->as<std::string>()));
        return shaderValueToLua(sol::state_view(state), e.shaderParam(name, part && part->is<int>() ? part->as<int>() : 0));
      },

      // Physics bodies
      "velocity", sol::property(&GameEntity::velocity, &GameEntity::setVelocity),
      "impulse", &GameEntity::impulse,
      "force", &GameEntity::force,
      "angularVelocity", sol::property(&GameEntity::angularVelocity, &GameEntity::setAngularVelocity),
      "torque", &GameEntity::torque,
      "angularImpulse", &GameEntity::angularImpulse,
      "impulseAt", &GameEntity::impulseAt,
      "forceAt", &GameEntity::forceAt,

      // Joints (see components/joint.h)
      "setJoint", [](GameEntity &e, const sol::table &options) { e.setJoint(luaToJoint(options)); },
      "removeJoint", &GameEntity::removeJoint,
      "hasJoint", &GameEntity::hasJoint,
      "jointAngle", &GameEntity::jointAngle,
      "jointPosition", &GameEntity::jointPosition,
      "setJointMotorSpeed", &GameEntity::setJointMotorSpeed,
      "setJointMotorTarget", &GameEntity::setJointMotorTarget,
      "stopJointMotor", &GameEntity::stopJointMotor,

      // The collision layer, by name (see physics.addLayer); also settable in engine.create
      "layer", sol::property(&GameEntity::layer, &GameEntity::setLayer),

      // Characters
      "move", &GameEntity::move,
      "jump", &GameEntity::jump,
      "onGround", sol::readonly_property(&GameEntity::onGround),

      // Animation and sound
      "play", [](GameEntity &e, const std::string &clip, sol::optional<float> fade, sol::optional<bool> loop) {
        e.play(clip, fade.value_or(GameEntity::kDefaultFade), loop.value_or(true));
      },
      "clips", [](GameEntity &e) { return sol::as_table(e.clips()); },
      // Animation state machines (see the README): the JSON file, parameters (a number or a boolean), triggers and
      // the state it is in
      // Reflection probes: bake to the file, capture again in memory, whether it is out of date
      "bakeReflectionProbe", &GameEntity::bakeReflectionProbe,
      "refreshReflectionProbe", &GameEntity::refreshReflectionProbe,
      "isReflectionProbeStale", &GameEntity::isReflectionProbeStale,
      "setAnimator", &GameEntity::setAnimator,
      "setParam", [](GameEntity &e, const std::string &name, const sol::object &value) {
        if (value.is<bool>())
          e.setParam(name, value.as<bool>());
        else if (value.is<float>())
          e.setParam(name, value.as<float>());
        else
          throw sol::error("setParam: the value must be a number or a boolean");
      },
      "param", &GameEntity::param,
      "trigger", &GameEntity::trigger,
      "animationState", sol::readonly_property(&GameEntity::animationState),
      "animationLayerState", &GameEntity::animationLayerState,
      "inAnimationWindow", &GameEntity::inAnimationWindow,
      // The entity's AudioSource: audioPlaying is read-only, the others read and write
      "playAudio", &GameEntity::playAudio,
      "stopAudio", &GameEntity::stopAudio,
      "pauseAudio", &GameEntity::pauseAudio,
      "audioPlaying", sol::readonly_property(&GameEntity::isAudioPlaying),
      "audioVolume", sol::property(&GameEntity::audioVolume, &GameEntity::setAudioVolume),
      "audioPitch", sol::property(&GameEntity::audioPitch, &GameEntity::setAudioPitch),
      "audioLoop", sol::property(&GameEntity::audioLoop, &GameEntity::setAudioLoop),
      "sound", [](GameEntity &e, const std::string &path, sol::optional<float> volume) {
        return e.sound(path, volume.value_or(1.0f));
      });
}
