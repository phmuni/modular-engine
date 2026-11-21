// Lua bindings: the save (saved data) and settings (the player's options) tables.
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

namespace {
// Saved data holds JSON: numbers, strings, booleans and tables of those (a table of 1..n is an array, any other an
// object with string keys). Returns false for anything else (functions, userdata, mixed keys, tables nested too deep).
bool luaToJson(const sol::object &value, nlohmann::json &out, int depth = 0) {
  switch (value.get_type()) {
  case sol::type::boolean:
    out = value.as<bool>();
    return true;
  case sol::type::string:
    out = value.as<std::string>();
    return true;
  case sol::type::number: {
    // Whole numbers stay integers, so a score of 12 comes back as 12 and not 12.0
    const double number = value.as<double>();
    if (std::isfinite(number) && number == std::floor(number) && std::abs(number) < 9007199254740992.0)
      out = static_cast<int64_t>(number);
    else if (std::isfinite(number))
      out = number;
    else
      return false;
    return true;
  }
  case sol::type::table: {
    if (depth > 16)
      return false;
    const sol::table table = value.as<sol::table>();
    const size_t length = table.size();
    size_t count = 0;
    for (const auto &entry : table) {
      (void)entry;
      ++count;
    }
    if (count == length) { // keys 1..n: an array
      out = nlohmann::json::array();
      for (size_t i = 1; i <= length; ++i) {
        nlohmann::json item;
        if (!luaToJson(table[i], item, depth + 1))
          return false;
        out.push_back(std::move(item));
      }
      return true;
    }
    out = nlohmann::json::object();
    for (const auto &[key, item] : table) {
      if (key.get_type() != sol::type::string)
        return false;
      nlohmann::json converted;
      if (!luaToJson(item, converted, depth + 1))
        return false;
      out[key.as<std::string>()] = std::move(converted);
    }
    return true;
  }
  default:
    return false;
  }
}

sol::object jsonToLua(sol::state_view lua, const nlohmann::json &value) {
  switch (value.type()) {
  case nlohmann::json::value_t::boolean:
    return sol::make_object(lua, value.get<bool>());
  case nlohmann::json::value_t::number_integer:
  case nlohmann::json::value_t::number_unsigned:
    return sol::make_object(lua, value.get<int64_t>());
  case nlohmann::json::value_t::number_float:
    return sol::make_object(lua, value.get<double>());
  case nlohmann::json::value_t::string:
    return sol::make_object(lua, value.get<std::string>());
  case nlohmann::json::value_t::array: {
    sol::table table = lua.create_table();
    for (const nlohmann::json &item : value)
      table.add(jsonToLua(lua, item));
    return table;
  }
  case nlohmann::json::value_t::object: {
    sol::table table = lua.create_table();
    for (const auto &[key, item] : value.items())
      table[key] = jsonToLua(lua, item);
    return table;
  }
  default:
    return sol::lua_nil;
  }
}
} // namespace

void LuaBindings::bindSettings() {
  // save: progress and settings that stay between runs (see SaveData); values are numbers, strings, booleans or
  // tables of those, nested
  sol::table save = lua.create_named_table("save");
  save.set_function("set", [this](const std::string &key, sol::object value) {
    nlohmann::json json;
    if (!luaToJson(value, json)) {
      report("save.set", "save.set(\"" + key + "\"): only numbers, strings, booleans and tables of those can be saved");
      return;
    }
    engine.saveData().setJson(key, json);
  });
  save.set_function("get", [this](const std::string &key, sol::object fallback) {
    const nlohmann::json json = engine.saveData().getJson(key);
    return json.is_null() ? fallback : jsonToLua(lua, json);
  });
  save.set_function("has", [this](const std::string &key) { return engine.saveData().has(key); });
  save.set_function("remove", [this](const std::string &key) { engine.saveData().remove(key); });
  save.set_function("write", [this]() { return engine.saveData().write(); });

  // settings: the player's options (see systems/playerSettings.h); a name that is not one is reported
  sol::table settings = lua.create_named_table("settings");
  settings.set_function("names", [this]() { return sol::as_table(engine.settings().names()); });
  settings.set_function("has", [this](const std::string &name) { return engine.settings().has(name); });
  settings.set_function("get", [this](const std::string &name) -> sol::object {
    if (!knownSetting("settings.get", name))
      return sol::lua_nil;
    return valueToLua(engine.settings().get(name));
  });
  settings.set_function("set", [this](const std::string &name, sol::object value) {
    if (!knownSetting("settings.set", name))
      return false;
    const std::optional<PlayerSettings::Value> converted = luaToValue(value);
    if (!converted) {
      report("settings.set", "settings.set(\"" + name + "\"): the value is a number, a boolean or a string");
      return false;
    }
    return engine.settings().set(name, *converted);
  });
  settings.set_function("step", [this](const std::string &name, sol::optional<int> direction) {
    return knownSetting("settings.step", name) && engine.settings().step(name, direction.value_or(1));
  });
  settings.set_function("choices", [this](const std::string &name) {
    return sol::as_table(engine.settings().choices(name));
  });
  settings.set_function("label", [this](const std::string &name) {
    return knownSetting("settings.label", name) ? engine.settings().label(name) : std::string();
  });
  settings.set_function("enabled", [this](const std::string &name) { return engine.settings().enabled(name); });
  settings.set_function("kind", [this](const std::string &name) {
    switch (engine.settings().kind(name)) {
    case PlayerSettings::Kind::Number:
      return "number";
    case PlayerSettings::Kind::Choice:
      return "choice";
    default:
      return "bool";
    }
  });
  settings.set_function("range", [this](const std::string &name) {
    const PlayerSettings::Range range = engine.settings().range(name);
    return std::make_tuple(range.min, range.max, range.step);
  });
  // settings.reset("msaa") for one, settings.reset() for all of them
  settings.set_function("reset", [this](sol::optional<std::string> name) {
    if (!name)
      engine.settings().resetAll();
    else if (knownSetting("settings.reset", *name))
      engine.settings().reset(*name);
  });
}
