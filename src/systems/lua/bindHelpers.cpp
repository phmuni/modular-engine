// Lua bindings: the conversions and checks several binding files share (see luaBindings.h).

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

bool LuaBindings::knownSetting(const char *where, const std::string &name) {
  if (engine.settings().has(name))
    return true;
  report(where, std::string(where) + "(\"" + name + "\"): there is no such setting (settings.names() lists them)");
  return false;
}

sol::object LuaBindings::valueToLua(const PlayerSettings::Value &value) {
  if (const bool *flag = std::get_if<bool>(&value))
    return sol::make_object(lua, *flag);
  if (const double *number = std::get_if<double>(&value))
    return sol::make_object(lua, *number);
  return sol::make_object(lua, std::get<std::string>(value));
}

std::optional<PlayerSettings::Value> LuaBindings::luaToValue(const sol::object &value) {
  if (value.is<bool>())
    return PlayerSettings::Value(value.as<bool>());
  if (value.is<double>())
    return PlayerSettings::Value(value.as<double>());
  if (value.is<std::string>())
    return PlayerSettings::Value(value.as<std::string>());
  return std::nullopt;
}

// A Lua function as a C++ callback: its errors are reported like any script's, and the game goes on
std::function<void()> LuaBindings::callback(sol::protected_function handler, std::string where) {
  return [this, handler = std::move(handler), where = std::move(where)]() {
    const sol::protected_function_result result = handler();
    if (!result.valid()) {
      const sol::error error = result;
      report(where, error.what());
    }
  };
}

// A vec3 field of a table, or nothing when it is absent or not a vec3
std::optional<glm::vec3> LuaBindings::vectorField(const sol::table &table, const char *key) {
  const sol::optional<glm::vec3> value = table[key];
  return value ? std::optional<glm::vec3>(*value) : std::nullopt;
}

// engine.create's table as the description the C++ side takes (see EntityDesc for the fields)
EntityDesc LuaBindings::entityDesc(const sol::optional<sol::table> &options) {
  EntityDesc desc;
  if (!options)
    return desc;
  const sol::table &o = *options;
  auto vectorOr = [&](const char *key, const glm::vec3 &fallback) {
    const sol::optional<glm::vec3> value = o[key];
    return value ? *value : fallback;
  };
  desc.model = o.get_or<std::string>("model", "");
  desc.position = vectorOr("position", desc.position);
  desc.rotation = vectorOr("rotation", desc.rotation);
  desc.scale = vectorOr("scale", desc.scale);
  desc.color = vectorField(o, "color");
  desc.emission = vectorField(o, "emission");
  desc.glow = o.get_or("glow", desc.glow);
  if (const sol::optional<std::string> body = o["body"])
    desc.body = *body == "static"      ? BodyType::Static
                : *body == "kinematic" ? BodyType::Kinematic
                                       : BodyType::Dynamic;
  const std::string shape = o.get_or<std::string>("shape", "box");
  desc.shape = shape == "sphere"       ? BodyShape::Sphere
               : shape == "capsule"    ? BodyShape::Capsule
               : shape == "convexHull" ? BodyShape::ConvexHull
               : shape == "mesh"       ? BodyShape::Mesh
                                       : BodyShape::Box;
  desc.mass = o.get_or("mass", desc.mass);
  desc.collision = o.get_or("collision", false);
  desc.script = o.get_or<std::string>("script", "");
  desc.layer = o.get_or<std::string>("layer", "");
  // character = true for the defaults, or { radius, height, maxSlope, stepHeight } (every field optional)
  if (const sol::optional<sol::table> character = o["character"]) {
    desc.character = CharacterController();
    desc.character->radius = character->get_or("radius", desc.character->radius);
    desc.character->height = character->get_or("height", desc.character->height);
    desc.character->maxSlope = character->get_or("maxSlope", desc.character->maxSlope);
    desc.character->stepHeight = character->get_or("stepHeight", desc.character->stepHeight);
  } else if (o.get_or("character", false)) {
    desc.character = CharacterController();
  }
  if (const sol::optional<sol::table> components = o["components"])
    for (const auto &entry : *components)
      if (entry.second.get_type() == sol::type::string)
        desc.components.push_back(entry.second.as<std::string>());
  if (const sol::optional<sol::table> tags = o["tags"])
    for (const auto &entry : *tags)
      if (entry.second.get_type() == sol::type::string)
        desc.tags.push_back(entry.second.as<std::string>());
  return desc;
}

// Layer names in a Lua list as the mask raycasts take; a name that is no layer is an error, not a silent miss
uint32_t LuaBindings::layerMaskOf(const sol::table &names) {
  std::vector<std::string> layers;
  for (const auto &entry : names)
    if (entry.second.get_type() == sol::type::string)
      layers.push_back(entry.second.as<std::string>());
  std::vector<std::string> unknown;
  const uint32_t mask = engine.layers().maskOf(layers, &unknown);
  if (!unknown.empty())
    throw std::runtime_error("physics.raycast: \"" + unknown.front() + "\" is not a collision layer");
  return mask;
}

// The options of a raycast or an overlap query: { layers = {"enemy"}, ignore = entity or {entities}, characters = true }
QueryOptions LuaBindings::queryOptions(const sol::optional<sol::table> &table) {
  QueryOptions options;
  if (!table)
    return options;
  if (const sol::optional<sol::table> layers = (*table)["layers"])
    options.layerMask = layerMaskOf(*layers);
  options.characters = table->get_or("characters", options.characters);
  const sol::object ignore = (*table)["ignore"];
  if (ignore.is<GameEntity>()) {
    options.ignore.push_back(ignore.as<GameEntity>().id());
  } else if (ignore.is<sol::table>()) {
    for (const auto &entry : ignore.as<sol::table>()) {
      if (!entry.second.is<GameEntity>())
        throw std::runtime_error("physics queries: ignore is an entity or a list of entities");
      options.ignore.push_back(entry.second.as<GameEntity>().id());
    }
  } else if (ignore.valid() && ignore.get_type() != sol::type::lua_nil) {
    throw std::runtime_error("physics queries: ignore is an entity or a list of entities");
  }
  return options;
}

// A ray hit as Lua sees it: { entity, point, normal, distance }
sol::table LuaBindings::hitTable(const RayHit &hit) {
  sol::table result = lua.create_table();
  result["entity"] = entityObject(engine.entity(hit.entity));
  result["point"] = hit.point;
  result["normal"] = hit.normal;
  result["distance"] = hit.distance;
  return result;
}

// A Lua list of entity handles
sol::table LuaBindings::entityList(const std::vector<Entity> &entities) {
  sol::table result = lua.create_table();
  for (Entity entity : entities)
    result.add(entityObject(engine.entity(entity)));
  return result;
}

void LuaBindings::requireLayer(const char *function, const std::string &name) {
  if (engine.layers().indexOf(name) < 0)
    throw std::runtime_error(std::string(function) + ": \"" + name + "\" is not a collision layer (physics.addLayer defines one)");
}

// "easeOutQuad" and friends, as tween.to takes them; a name nothing answers to is an error, not a silent linear
Ease LuaBindings::easeNamed(const std::string &name) {
  static const std::pair<const char *, Ease> names[] = {
      {"linear", Ease::Linear},          {"easeInQuad", Ease::EaseInQuad},   {"easeOutQuad", Ease::EaseOutQuad},
      {"easeInOutQuad", Ease::EaseInOutQuad}, {"easeInCubic", Ease::EaseInCubic}, {"easeOutCubic", Ease::EaseOutCubic},
      {"easeInOutCubic", Ease::EaseInOutCubic}, {"easeInSine", Ease::EaseInSine}, {"easeOutSine", Ease::EaseOutSine},
      {"easeInOutSine", Ease::EaseInOutSine}, {"easeInBack", Ease::EaseInBack}, {"easeOutBack", Ease::EaseOutBack},
      {"easeInOutBack", Ease::EaseInOutBack}, {"easeInElastic", Ease::EaseInElastic},
      {"easeOutElastic", Ease::EaseOutElastic}, {"easeInOutElastic", Ease::EaseInOutElastic},
      {"easeInBounce", Ease::EaseInBounce}, {"easeOutBounce", Ease::EaseOutBounce},
      {"easeInOutBounce", Ease::EaseInOutBounce}};
  for (const auto &entry : names)
    if (name == entry.first)
      return entry.second;
  throw std::runtime_error("unknown easing \"" + name +
                           "\" (linear, then easeIn, easeOut and easeInOut plus Quad, Cubic, Sine, Back, Elastic or "
                           "Bounce: easeOutBack...)");
}

TweenProperty LuaBindings::tweenProperty(const std::string &name) {
  if (name == "position")
    return TweenProperty::Position;
  if (name == "rotation")
    return TweenProperty::Rotation;
  if (name == "scale")
    return TweenProperty::Scale;
  if (name == "color")
    return TweenProperty::Color;
  if (name == "emission")
    return TweenProperty::Emission;
  if (name == "intensity")
    return TweenProperty::Intensity;
  throw std::runtime_error("tween.to: unknown property \"" + name +
                           "\" (position, rotation, scale, color, emission, intensity)");
}

// camera.follow's options table as the CameraRig the C++ side takes (see components/cameraRig.h for the fields)
CameraRig LuaBindings::cameraRigOptions(const sol::optional<sol::table> &options) {
  CameraRig rig;
  if (!options)
    return rig;
  const sol::table &o = *options;
  rig.offset = vectorField(o, "offset").value_or(rig.offset);
  rig.lookAhead = o.get_or("lookAhead", rig.lookAhead);
  const float smoothing = o.get_or("smoothing", 0.0f); // both at once; the specific ones win
  rig.positionSmoothing = o.get_or("positionSmoothing", smoothing);
  rig.rotationSmoothing = o.get_or("rotationSmoothing", smoothing);
  rig.followRotation = o.get_or("followRotation", rig.followRotation);
  rig.orbit = o.get_or("orbit", rig.orbit);
  rig.orbitSensitivity = o.get_or("orbitSensitivity", rig.orbitSensitivity);
  rig.orbitSpeed = o.get_or("orbitSpeed", rig.orbitSpeed);
  return rig;
}
