// Lua scripting with sol2: script instances per entity, hot reload, and the API scripts see.
//
// The rule for the bindings (bindEntity, bindGlobals): a binding only converts types (Lua table <-> struct, nil <->
// optional or empty handle, Lua function <-> std::function, entity <-> GameEntity) and calls the C++ game API
// (GameEntity, GameUI, GameCamera, GameTime, Engine and the systems). Behavior lives in C++, where C++ games reach it
// too, so the two languages cannot drift apart. tools/check_api_parity.py checks that every public method of the
// facade classes is bound here, and tools/check_lua_api.py that every binding is described for the editor in
// templates/common/lua/types/engine.lua.

#include <sol/sol.hpp> // SOL_ALL_SAFETIES_ON comes from the build, the same for every file that includes sol2

#include "systems/scriptSystem.h"

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
#include "systems/renderSystem.h"
#include "systems/saveData.h"
#include <RmlUi/Core.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <stdexcept>
#include <tuple>
#include <nlohmann/json.hpp>
#include <unordered_map>
#include <variant>
#include <vector>

namespace {
// "scripts/coin.lua" for ".../assets/scripts/coin.lua"
std::string displayName(const std::string &path) {
  const size_t assets = path.rfind("/assets/");
  return assets == std::string::npos ? path : path.substr(assets + 8);
}

std::filesystem::file_time_type modifiedTime(const std::string &path) {
  std::error_code error;
  const auto time = std::filesystem::last_write_time(path, error);
  return error ? std::filesystem::file_time_type{} : time;
}

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

struct ScriptSystem::Impl {
  Engine &engine;
  sol::state lua;
  sol::table gameTable; // the shared `game` table

  // A loaded script: its file's functions live in `environment` (falling back to the globals)
  struct Instance {
    std::string path; // resolved
    std::string written; // Script::path as given, to notice when it changes
    sol::environment environment;
    std::filesystem::file_time_type loadedTime{};
    std::vector<ScriptProperty> properties; // from the file's `properties` table, by name
    bool started = false;
    bool broken = false; // failed to load or run: waits for the file to change
  };
  std::unordered_map<Entity, Instance> instances;
  // Scripts read only to list their properties for the editor, by resolved path (no entity runs them)
  std::unordered_map<std::string, Instance> declared;
  // What was last put on each entity's `self` for its properties, so only changes are written again
  std::unordered_map<Entity, std::map<std::string, ScriptValue>> appliedProperties;
  Instance gameScript;
  bool hasGameScript = false;
  std::string gameScriptPath;

  // Each entity's own Lua fields (self.speed = 5), kept apart from its script so reloading keeps them
  std::unordered_map<Entity, sol::table> fields;

  // Coroutines started with engine.run / self:run: Lua threads that `wait` suspends. Each wait is a GameTime timer that
  // resumes the thread, so they follow the time scale and pause, and an error in one is reported like any script's.
  // This is the one thing here that is Lua's own rather than the C++ game API: C++ has no coroutines (use timers).
  struct Routine {
    int id = 0;
    Entity owner = -1; // an entity's routine ends with it; -1: the game's, which outlives scenes
    sol::thread thread; // keeps the Lua thread alive
    int timer = 0;      // the time.after that resumes it
    bool finished = false;
    std::string where;
  };
  std::unordered_map<int, std::shared_ptr<Routine>> routines;
  int nextRoutine = 1;

  std::string lastError;
  std::string lastErrorPath;
  float reloadCheck = 0.0f;

  explicit Impl(Engine &e) : engine(e) {
    lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::string, sol::lib::table, sol::lib::os,
                       sol::lib::io, sol::lib::coroutine, sol::lib::utf8, sol::lib::package);
    // require("enemies") finds assets/scripts/enemies.lua
    const std::string scripts = EngineConfig::resolvePath("../assets/scripts");
    lua["package"]["path"] = scripts + "/?.lua;" + scripts + "/?/init.lua";
    gameTable = lua.create_named_table("game");
    bindApi();
  }

  // --- Errors ---------------------------------------------------------------------------------------------------

  void report(const std::string &path, const std::string &message) {
    lastError = message;
    lastErrorPath = path;
    LOG_E("[Script] %s", message.c_str());
  }

  // Runs a script function if the script defines it; false (and reported) if it raised an error
  template <typename... Args> bool call(Instance &instance, const char *name, Args &&...args) {
    if (instance.broken)
      return false;
    sol::object function = instance.environment.raw_get<sol::object>(name);
    if (!function.is<sol::protected_function>())
      return true;
    sol::protected_function_result result = function.as<sol::protected_function>()(std::forward<Args>(args)...);
    if (result.valid())
      return true;
    const sol::error error = result;
    report(instance.path, error.what());
    instance.broken = true;
    return false;
  }

  // --- Loading --------------------------------------------------------------------------------------------------

  bool load(Instance &instance) {
    instance.loadedTime = modifiedTime(instance.path);
    if (!std::filesystem::exists(instance.path)) {
      report(instance.path, displayName(instance.path) + ": file not found");
      instance.broken = true;
      return false;
    }
    if (!instance.environment.valid())
      instance.environment = sol::environment(lua, sol::create, lua.globals());
    // Read here rather than by Lua, to name it by its short path in error messages ("scripts/coin.lua:12: ...")
    std::ifstream file(instance.path, std::ios::binary);
    const std::string source((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    sol::load_result chunk = lua.load(source, "@" + displayName(instance.path));
    if (!chunk.valid()) {
      const sol::error error = chunk;
      report(instance.path, error.what());
      instance.broken = true;
      return false;
    }
    sol::protected_function body = chunk;
    sol::set_environment(instance.environment, body);
    // The environment outlives a reload: a `properties` table the file no longer has must not linger
    instance.environment.raw_set("properties", sol::lua_nil);
    sol::protected_function_result result = body();
    if (!result.valid()) {
      const sol::error error = result;
      report(instance.path, error.what());
      instance.broken = true;
      return false;
    }
    instance.broken = false;
    readProperties(instance);
    if (lastErrorPath == instance.path)
      lastError.clear(); // this file is fine again
    return true;
  }

  // The file's `properties = { speed = 6, ... }`: numbers, booleans, strings and vec3s; other values are ignored
  void readProperties(Instance &instance) {
    instance.properties.clear();
    const sol::object table = instance.environment.raw_get<sol::object>("properties");
    if (!table.is<sol::table>())
      return;
    for (const auto &[key, value] : table.as<sol::table>()) {
      if (key.get_type() != sol::type::string)
        continue;
      ScriptProperty property{key.as<std::string>(), {}};
      if (value.get_type() == sol::type::boolean)
        property.value = value.as<bool>();
      else if (value.get_type() == sol::type::number)
        property.value = value.as<float>();
      else if (value.get_type() == sol::type::string)
        property.value = value.as<std::string>();
      else if (value.is<glm::vec3>())
        property.value = value.as<glm::vec3>();
      else
        continue;
      instance.properties.push_back(std::move(property));
    }
    // Lua tables have no order; by name is at least stable
    std::sort(instance.properties.begin(), instance.properties.end(),
              [](const ScriptProperty &a, const ScriptProperty &b) { return a.name < b.name; });
  }

  // Puts the properties on the entity's `self`: its own value where it has one of the right kind, else the script's.
  // Only what differs from last time is written, so a script that changes self.speed keeps its value, while editing
  // it in the inspector (or the default in the file) takes effect at once.
  void applyProperties(Entity entity, const Instance &instance) {
    const Script *script = engine.tryGet<Script>(entity);
    std::map<std::string, ScriptValue> &applied = appliedProperties[entity];
    for (const ScriptProperty &property : instance.properties) {
      const ScriptValue *value = &property.value;
      if (script) {
        auto own = script->properties.find(property.name);
        if (own != script->properties.end() && own->second.index() == property.value.index())
          value = &own->second;
      }
      auto last = applied.find(property.name);
      if (last != applied.end() && last->second == *value)
        continue;
      applied[property.name] = *value;
      fieldsOf(entity)[property.name] = std::visit([&](const auto &v) { return sol::make_object(lua, v); }, *value);
    }
  }

  std::vector<ScriptProperty> propertiesOf(Entity entity) {
    const Script *script = engine.tryGet<Script>(entity);
    if (!script || script->path.empty())
      return {};
    auto running = instances.find(entity);
    if (running != instances.end() && running->second.written == script->path && !running->second.broken)
      return running->second.properties;
    const std::string path = EngineConfig::resolveAsset(script->path);
    if (!std::filesystem::exists(path))
      return {};
    Instance &instance = declared[path];
    if (instance.path.empty() || modifiedTime(path) != instance.loadedTime) {
      instance.path = path;
      load(instance);
    }
    return instance.broken ? std::vector<ScriptProperty>{} : instance.properties;
  }

  // Scripts whose file changed are loaded again: their functions are replaced, entities keep their fields
  void reloadChanged() {
    auto reload = [&](Instance &instance, const sol::object &self) {
      if (modifiedTime(instance.path) == instance.loadedTime)
        return;
      LOG_I("[Script] Reloaded %s", displayName(instance.path).c_str());
      if (load(instance) && instance.started)
        call(instance, "onReload", self);
    };
    for (auto &[entity, instance] : instances)
      reload(instance, sol::make_object(lua, engine.entity(entity)));
    if (hasGameScript)
      reload(gameScript, sol::lua_nil);
  }

  // --- Entities -------------------------------------------------------------------------------------------------

  sol::table &fieldsOf(Entity entity) {
    auto it = fields.find(entity);
    if (it == fields.end())
      it = fields.emplace(entity, lua.create_table()).first;
    return it->second;
  }

  // A script sees an entity that is gone (or was never found) as nil
  sol::object entityObject(const GameEntity &entity) {
    return entity.valid() ? sol::make_object(lua, entity) : sol::lua_nil;
  }

  // Instances for entities that got a Script, gone for those that lost it (or were destroyed)
  void syncInstances() {
    for (auto it = instances.begin(); it != instances.end();) {
      const Script *script = engine.tryGet<Script>(it->first);
      if (!script || script->path != it->second.written)
        it = instances.erase(it);
      else
        ++it;
    }
    for (auto it = fields.begin(); it != fields.end();)
      it = engine.isAlive(it->first) ? std::next(it) : fields.erase(it);
    for (auto it = appliedProperties.begin(); it != appliedProperties.end();)
      it = instances.count(it->first) ? std::next(it) : appliedProperties.erase(it);

    engine.getComponentManager().forEachComponent<Script>([&](Entity entity, Script &script) {
      if (script.path.empty() || instances.count(entity))
        return;
      Instance &instance = instances[entity];
      instance.written = script.path;
      instance.path = EngineConfig::resolveAsset(script.path);
      load(instance);
    });
  }

  void update(float deltaTime) {
    reloadCheck -= deltaTime > 0.0f ? deltaTime : 1.0f / 60.0f;
    if (reloadCheck <= 0.0f) {
      reloadCheck = 0.5f;
      reloadChanged();
    }
    syncInstances();
    if (!routines.empty())
      pruneRoutines();

    if (hasGameScript && !gameScript.started) {
      gameScript.started = true;
      call(gameScript, "onStart");
    }
    // A copy of the list: scripts may create and destroy entities (with scripts) while running
    std::vector<Entity> entities;
    entities.reserve(instances.size());
    for (auto &[entity, instance] : instances)
      entities.push_back(entity);
    std::sort(entities.begin(), entities.end());

    for (Entity entity : entities) {
      auto it = instances.find(entity);
      if (it == instances.end() || !engine.isAlive(entity))
        continue;
      applyProperties(entity, it->second);
      if (!it->second.started) {
        it->second.started = true;
        call(it->second, "onStart", engine.entity(entity));
      }
    }
    if (hasGameScript)
      call(gameScript, "onUpdate", deltaTime);
    for (Entity entity : entities) {
      auto it = instances.find(entity);
      if (it != instances.end() && engine.isAlive(entity))
        call(it->second, "onUpdate", engine.entity(entity), deltaTime);
    }

    // Contacts and overlaps, to both entities' scripts
    auto notify = [&](Entity self, Entity other, const char *name, auto &&...extra) {
      auto it = instances.find(self);
      if (it != instances.end() && engine.isAlive(self) && engine.isAlive(other))
        call(it->second, name, engine.entity(self), engine.entity(other), extra...);
    };
    const std::vector<PhysicsContact> contacts = engine.physics().getContacts();
    for (const PhysicsContact &contact : contacts) {
      notify(contact.a, contact.b, "onContact", contact.point, contact.normal);
      notify(contact.b, contact.a, "onContact", contact.point, -contact.normal);
    }
    if (!instances.empty()) {
      const std::vector<CollisionEvent> overlaps = engine.findCollisions();
      for (const CollisionEvent &pair : overlaps) {
        notify(pair.a, pair.b, "onOverlap");
        notify(pair.b, pair.a, "onOverlap");
      }
    }
  }

  // --- Coroutines -----------------------------------------------------------------------------------------------

  void finishRoutine(const std::shared_ptr<Routine> &routine) {
    routine->finished = true;
    engine.time().cancel(routine->timer);
    routines.erase(routine->id);
  }

  // Runs the routine until it waits, ends or fails
  void resumeRoutine(const std::shared_ptr<Routine> &routine) {
    if (routine->finished)
      return;
    if (routine->owner != -1 && !engine.isAlive(routine->owner)) {
      finishRoutine(routine);
      return;
    }
    lua_State *thread = routine->thread.thread_state();
    int results = 0;
    const int status = lua_resume(thread, lua.lua_state(), 0, &results);
    if (status == LUA_YIELD) {
      // What it yielded is the seconds to wait (wait() with nothing, or a bare coroutine.yield(): the next frame)
      const float seconds = results > 0 && lua_isnumber(thread, -results)
                                ? static_cast<float>(lua_tonumber(thread, -results))
                                : 0.0f;
      lua_pop(thread, results);
      if (!routine->finished) // not stopped from inside itself
        routine->timer = engine.time().after(seconds, [this, routine]() { resumeRoutine(routine); });
      return;
    }
    if (status != LUA_OK) {
      const char *message = lua_tostring(thread, -1);
      report(routine->where, message ? message : "error in a coroutine");
    }
    finishRoutine(routine);
  }

  // Starts `function` as a coroutine, running it now up to its first wait; returns a handle for engine.stop
  int startRoutine(const sol::protected_function &function, Entity owner, const char *where) {
    auto routine = std::make_shared<Routine>();
    routine->id = nextRoutine++;
    routine->owner = owner;
    routine->where = where;
    routine->thread = sol::thread::create(lua.lua_state());
    function.push(routine->thread.thread_state());
    routines[routine->id] = routine;
    const int id = routine->id;
    resumeRoutine(routine);
    return id;
  }

  void stopRoutine(int id) {
    auto it = routines.find(id);
    if (it != routines.end())
      finishRoutine(it->second);
  }

  // Routines of entities that are gone end now, not when their wait is over
  void pruneRoutines() {
    std::vector<std::shared_ptr<Routine>> gone;
    for (const auto &entry : routines)
      if (entry.second->owner != -1 && !engine.isAlive(entry.second->owner))
        gone.push_back(entry.second);
    for (const auto &routine : gone)
      finishRoutine(routine);
  }

  // --- The API --------------------------------------------------------------------------------------------------

  void bindApi() {
    bindMath();
    bindEntity();
    bindGlobals();
  }

  void bindMath() {
    lua.new_usertype<glm::vec3>(
        "vec3",
        sol::call_constructor,
        sol::factories([] { return glm::vec3(0.0f); }, [](float v) { return glm::vec3(v); },
                       [](float x, float y, float z) { return glm::vec3(x, y, z); }),
        "x", &glm::vec3::x, "y", &glm::vec3::y, "z", &glm::vec3::z,
        sol::meta_function::addition, [](const glm::vec3 &a, const glm::vec3 &b) { return a + b; },
        sol::meta_function::subtraction, [](const glm::vec3 &a, const glm::vec3 &b) { return a - b; },
        sol::meta_function::multiplication,
        sol::overload([](const glm::vec3 &a, float s) { return a * s; },
                      [](float s, const glm::vec3 &a) { return a * s; },
                      [](const glm::vec3 &a, const glm::vec3 &b) { return a * b; }),
        sol::meta_function::division, [](const glm::vec3 &a, float s) { return a / s; },
        sol::meta_function::unary_minus, [](const glm::vec3 &a) { return -a; },
        sol::meta_function::equal_to, [](const glm::vec3 &a, const glm::vec3 &b) { return a == b; },
        sol::meta_function::to_string,
        [](const glm::vec3 &v) {
          char text[96];
          std::snprintf(text, sizeof(text), "vec3(%.3f, %.3f, %.3f)", v.x, v.y, v.z);
          return std::string(text);
        },
        "length", [](const glm::vec3 &v) { return glm::length(v); },
        "normalized",
        [](const glm::vec3 &v) {
          const float length = glm::length(v);
          return length > 0.0f ? v / length : v;
        },
        "dot", [](const glm::vec3 &a, const glm::vec3 &b) { return glm::dot(a, b); },
        "cross", [](const glm::vec3 &a, const glm::vec3 &b) { return glm::cross(a, b); },
        "distance", [](const glm::vec3 &a, const glm::vec3 &b) { return glm::distance(a, b); },
        "lerp", [](const glm::vec3 &a, const glm::vec3 &b, float t) { return glm::mix(a, b, t); });

    lua.new_usertype<Transform>("Transform", sol::no_constructor, "position", &Transform::position, "rotation",
                                &Transform::rotation, "scale", &Transform::scale);
    lua.new_usertype<Light>("Light", sol::no_constructor, "color", &Light::color, "intensity", &Light::intensity);
  }

  void bindEntity() {
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
        "setColor", &GameEntity::setColor,
        "getColor", &GameEntity::color, // not `color`: a script may keep its own field of that name
        "setEmission", [](GameEntity &e, const glm::vec3 &color, sol::optional<float> strength) {
          e.setEmission(color, strength.value_or(1.0f));
        },
        "setVisible", &GameEntity::setVisible,
        "visible", sol::readonly_property(&GameEntity::isVisible),
        "setTexture", &GameEntity::setTexture,
        "setShininess", &GameEntity::setShininess,

        // Physics bodies
        "velocity", sol::property(&GameEntity::velocity, &GameEntity::setVelocity),
        "impulse", &GameEntity::impulse,
        "force", &GameEntity::force,

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

  // A Lua function as a C++ callback: its errors are reported like any script's, and the game goes on
  std::function<void()> callback(sol::protected_function handler, std::string where) {
    return [this, handler = std::move(handler), where = std::move(where)]() {
      const sol::protected_function_result result = handler();
      if (!result.valid()) {
        const sol::error error = result;
        report(where, error.what());
      }
    };
  }

  // A vec3 field of a table, or nothing when it is absent or not a vec3
  static std::optional<glm::vec3> vectorField(const sol::table &table, const char *key) {
    const sol::optional<glm::vec3> value = table[key];
    return value ? std::optional<glm::vec3>(*value) : std::nullopt;
  }

  // engine.create's table as the description the C++ side takes (see EntityDesc for the fields)
  static EntityDesc entityDesc(const sol::optional<sol::table> &options) {
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
  uint32_t layerMaskOf(const sol::table &names) {
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

  void requireLayer(const char *function, const std::string &name) {
    if (engine.layers().indexOf(name) < 0)
      throw std::runtime_error(std::string(function) + ": \"" + name + "\" is not a collision layer (physics.addLayer defines one)");
  }

  // "easeOutQuad" and friends, as tween.to takes them; a name nothing answers to is an error, not a silent linear
  static Ease easeNamed(const std::string &name) {
    static const std::pair<const char *, Ease> names[] = {
        {"linear", Ease::Linear},          {"easeInQuad", Ease::EaseInQuad},   {"easeOutQuad", Ease::EaseOutQuad},
        {"easeInOutQuad", Ease::EaseInOutQuad}, {"easeInCubic", Ease::EaseInCubic}, {"easeOutCubic", Ease::EaseOutCubic},
        {"easeInOutCubic", Ease::EaseInOutCubic}};
    for (const auto &entry : names)
      if (name == entry.first)
        return entry.second;
    throw std::runtime_error("unknown easing \"" + name +
                             "\" (linear, easeInQuad, easeOutQuad, easeInOutQuad, easeInCubic, easeOutCubic, "
                             "easeInOutCubic)");
  }

  static TweenProperty tweenProperty(const std::string &name) {
    if (name == "position")
      return TweenProperty::Position;
    if (name == "rotation")
      return TweenProperty::Rotation;
    if (name == "scale")
      return TweenProperty::Scale;
    if (name == "color")
      return TweenProperty::Color;
    throw std::runtime_error("tween.to: unknown property \"" + name + "\" (position, rotation, scale, color)");
  }

  // camera.follow's options table as the CameraRig the C++ side takes (see components/cameraRig.h for the fields)
  static CameraRig cameraRigOptions(const sol::optional<sol::table> &options) {
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

  void bindGlobals() {
    // print goes to the engine's log
    lua.set_function("print", [](sol::variadic_args args, sol::this_state state) {
      sol::state_view view(state);
      std::string line;
      for (auto arg : args) {
        if (!line.empty())
          line += "  ";
        line += view["tostring"](arg.get<sol::object>()).get<std::string>();
      }
      LOG_I("[Lua] %s", line.c_str());
    });

    // wait(seconds) suspends the coroutine it is called in (see engine.run) for that much game time; wait() until the
    // next frame
    lua.set_function("wait", sol::yielding([](sol::optional<float> seconds, sol::this_state state) {
      if (!lua_isyieldable(state.L))
        throw std::runtime_error("wait() only works inside a coroutine: engine.run(function() ... end) or "
                                 "self:run(function() ... end)");
      return seconds.value_or(0.0f);
    }));

    // engine: entities, scenes, the sky
    sol::table api = lua.create_named_table("engine");
    // engine.run(function() ... wait(1) ... end) runs a function that can wait; returns a handle for engine.stop. It
    // lasts across scene loads; use self:run for one that should end with an entity
    api.set_function("run", [this](sol::protected_function function) { return startRoutine(function, -1, "engine.run"); });
    api.set_function("stop", [this](int handle) { stopRoutine(handle); });
    api.set_function("find", [this](const std::string &name) { return entityObject(engine.find(name)); });
    // The entities named so, or with a component ("Model", "Script", a game component's name), by entity number
    api.set_function("findAll", [this](const std::string &name) { return sol::as_table(engine.findAll(name)); });
    // The entities with a tag (self:addTag("enemy"), or tags = {"enemy"} in engine.create), by entity number
    api.set_function("findTagged", [this](const std::string &tag) { return sol::as_table(engine.findTagged(tag)); });
    api.set_function("findWithComponent",
                     [this](const std::string &component) { return sol::as_table(engine.findWithComponent(component)); });
    api.set_function("spawn", [this](const std::string &prefab, sol::optional<glm::vec3> position,
                                     sol::optional<glm::vec3> rotation) {
      return entityObject(engine.spawn(prefab, position.value_or(glm::vec3(0.0f)), rotation.value_or(glm::vec3(0.0f))));
    });
    // engine.create("Coin", { model = "models/coin.glb", position = vec3(0, 1, 0), scale = vec3(0.5),
    //                         color = vec3(1, 0.8, 0.2), body = "dynamic", shape = "sphere", script = "scripts/coin.lua" })
    // engine.create(name, { model = "box" | "sphere" | "cylinder" | path, position, rotation, scale, color, emission, glow,
    //                       body = "static" | "dynamic" | "kinematic", shape, mass, collision = true, script,
    //                       tags = {"enemy"}, character = { radius = 0.4, height = 1.8 }, components = {"Player"} })
    api.set_function("create", [this](const std::string &name, sol::optional<sol::table> options) {
      return entityObject(engine.create(name, entityDesc(options)));
    });
    api.set_function("destroy", [](sol::optional<GameEntity> e) {
      if (e)
        e->destroy();
    });
    api.set_function("loadScene", [this](const std::string &path) { engine.loadScene(path); });
    api.set_function("setTitle", [this](const std::string &title) { engine.setWindowTitle(title); });
    api.set_function("quit", [this]() { engine.quit(); });
    // The window and the resolution the scene renders at (see Engine::setWindowSize, Engine::setRenderScale).
    // engine.windowSize() and engine.renderResolution() return width and height; engine.resolutions() a list of
    // { width = 1280, height = 720 } that fit the display.
    api.set_function("setWindowSize", [this](int width, int height) { engine.setWindowSize(width, height); });
    api.set_function("windowSize", [this]() {
      const glm::ivec2 size = engine.windowSize();
      return std::make_tuple(size.x, size.y);
    });
    api.set_function("resolutions", [this]() {
      sol::table result = lua.create_table();
      for (const glm::ivec2 &size : engine.resolutions()) {
        sol::table entry = lua.create_table();
        entry["width"] = size.x;
        entry["height"] = size.y;
        result.add(entry);
      }
      return result;
    });
    api.set_function("setRenderScale", [this](float scale) { engine.setRenderScale(scale); });
    api.set_function("renderScale", [this]() { return engine.renderScale(); });
    api.set_function("renderResolution", [this]() {
      const glm::ivec2 size = engine.renderResolution();
      return std::make_tuple(size.x, size.y);
    });
    api.set_function("setFullscreen", [this](bool enabled) { engine.setFullscreen(enabled); });
    api.set_function("isFullscreen", [this]() { return engine.isFullscreen(); });
    // The engine's switches by name (see TOGGLE_NAMES): engine.setToggle("freeCamera", true)
    const auto toggleNamed = [](const std::string &name) {
      if (const auto toggle = toggleFromName(name))
        return *toggle;
      std::string known;
      for (const auto &entry : TOGGLE_NAMES)
        known += (known.empty() ? "" : ", ") + std::string(entry.first);
      throw std::runtime_error("engine.setToggle: \"" + name + "\" is not a toggle (" + known + ")");
    };
    api.set_function("setToggle", [this, toggleNamed](const std::string &name, bool on) {
      engine.setState(toggleNamed(name), on);
    });
    api.set_function("isToggled", [this, toggleNamed](const std::string &name) {
      return engine.getState(toggleNamed(name));
    });
    // The color the image is cleared to, behind the skybox panorama; kept in the scene (see Engine::setSkyColor)
    api.set_function("setSkyColor", [this](const glm::vec3 &color) { engine.setSkyColor(color); });
    api.set_function("getSkyColor", [this]() { return engine.getSkyColor(); });
    // Fog: engine.setFog({ color = vec3(0.6, 0.7, 0.8), start = 20, finish = 90 }) turns it on, with only the fields given
    // changed (mode = "linear" or "exponential", density for the exponential one; without `color` it takes the sky color);
    // engine.setFog(false) turns it off. engine.getFog() reads it back.
    api.set_function("setFog", [this](sol::object settings) {
      Fog fog = engine.getFog();
      if (settings.is<bool>()) {
        fog.enabled = settings.as<bool>();
      } else if (settings.is<sol::table>()) {
        const sol::table o = settings.as<sol::table>();
        fog.enabled = o.get_or("enabled", true);
        if (const sol::optional<glm::vec3> color = o["color"]) {
          fog.color = *color;
          fog.matchSky = false;
        }
        fog.matchSky = o.get_or("matchSky", fog.matchSky);
        if (const sol::optional<std::string> mode = o["mode"]) {
          if (*mode != "linear" && *mode != "exponential")
            throw std::runtime_error("engine.setFog: mode is \"linear\" or \"exponential\"");
          fog.mode = *mode == "exponential" ? FogMode::Exponential : FogMode::Linear;
        }
        fog.start = o.get_or("start", fog.start);
        fog.finish = o.get_or("finish", fog.finish);
        fog.density = o.get_or("density", fog.density);
      } else {
        throw std::runtime_error("engine.setFog: a table of settings, or false");
      }
      engine.setFog(fog);
    });
    api.set_function("getFog", [this]() {
      const Fog fog = engine.getFog();
      sol::table result = lua.create_table();
      result["enabled"] = fog.enabled;
      result["mode"] = fog.mode == FogMode::Exponential ? "exponential" : "linear";
      result["matchSky"] = fog.matchSky;
      result["color"] = fog.matchSky ? engine.getSkyColor() : fog.color;
      result["start"] = fog.start;
      result["finish"] = fog.finish;
      result["density"] = fog.density;
      return result;
    });
    // Light added to everything lit, on top of each Light's own ambient: engine.setAmbient(vec3(0.5, 0.6, 0.8), 0.3)
    // (intensity 1 without one; engine.setAmbient(vec3(1), 0) is off). engine.getAmbient() returns the color and intensity.
    api.set_function("setAmbient", [this](const glm::vec3 &color, sol::optional<float> intensity) {
      engine.setAmbient(color, intensity.value_or(1.0f));
    });
    api.set_function("getAmbient", [this]() { return std::make_tuple(engine.getAmbientColor(), engine.getAmbientIntensity()); });

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
    camera.set_function("following", &GameCamera::isFollowing, &engine.camera());
    // camera.fov() reads the field of view, camera.fov(70) sets it (degrees)
    camera.set_function("fov", [this](sol::optional<float> degrees) {
      if (degrees)
        engine.camera().setFov(*degrees);
      return engine.camera().fov();
    });

    // time: the scale and pause, the game clock, timers
    sol::table time = lua.create_named_table("time");
    time.set_function("scale", [this](sol::optional<float> scale) {
      if (scale)
        engine.time().setScale(*scale);
      return engine.time().scale();
    });
    time.set_function("paused", [this](sol::optional<bool> paused) {
      if (paused)
        engine.time().setPaused(*paused);
      return engine.time().isPaused();
    });
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

    // tween: animates an entity's "position", "rotation", "scale" or "color" (a vec3), or a number, on game time
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

    // input: actions and axes (see InputSystem::bindAction), keys by SDL's names ("Space", "Left Shift")
    sol::table input = lua.create_named_table("input");
    input.set_function("axis", [this](const std::string &name) { return engine.input().getAxis(name); });
    input.set_function("down", [this](const std::string &name) { return engine.input().isActionDown(name); });
    input.set_function("pressed", [this](const std::string &name) { return engine.input().wasActionPressed(name); });
    input.set_function("released", [this](const std::string &name) { return engine.input().wasActionReleased(name); });
    input.set_function("key", [this](const std::string &name) { return engine.input().isKeyDown(name); });
    input.set_function("keyPressed", [this](const std::string &name) { return engine.input().wasKeyPressed(name); });
    input.set_function("mouse", [this]() {
      const glm::vec2 position = engine.input().getMousePosition();
      return std::make_tuple(position.x, position.y);
    });
    // Buttons: 1 left (the default), 2 middle, 3 right
    input.set_function("click", [this](sol::optional<int> button) {
      return engine.input().wasMouseButtonPressed(static_cast<MouseButton>(button.value_or(1)));
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

    // physics
    sol::table physics = lua.create_named_table("physics");
    // physics.raycast(origin, direction, distance, { layers = {"enemy", "static"} }): the nearest body, or nil. Only bodies on
    // the listed layers count (all of them without `layers`); a name that is no layer is an error.
    physics.set_function("raycast", [this](const glm::vec3 &origin, const glm::vec3 &direction,
                                           sol::optional<float> distance, sol::optional<sol::table> options) -> sol::object {
      uint32_t layerMask = CollisionLayers::kAllLayers;
      if (options)
        if (const sol::optional<sol::table> layers = (*options)["layers"])
          layerMask = layerMaskOf(*layers);
      const auto hit = engine.physics().raycast(origin, direction, distance.value_or(1000.0f), nullptr, layerMask);
      if (!hit)
        return sol::lua_nil;
      sol::table result = lua.create_table();
      result["entity"] = entityObject(engine.entity(hit->entity));
      result["point"] = hit->point;
      result["normal"] = hit->normal;
      result["distance"] = hit->distance;
      return result;
    });
    // Collision layers: bodies, characters and Collision boxes are on one (entity.layer, or layer = in engine.create), and
    // layers that do not collide pass through each other. "default", "static", "player", "enemy", "projectile" and
    // "trigger" exist; addLayer defines more (32 in all). Everything collides until setLayersCollide says otherwise.
    physics.set_function("addLayer", [this](const std::string &name) {
      if (engine.layers().add(name) < 0)
        throw std::runtime_error("physics.addLayer: cannot define \"" + name + "\" (the name is empty or all 32 layers are taken)");
    });
    physics.set_function("layers", [this]() { return sol::as_table(std::vector<std::string>(engine.layers().names())); });
    physics.set_function("setLayersCollide", [this](const std::string &a, const std::string &b, bool collide) {
      requireLayer("physics.setLayersCollide", a);
      requireLayer("physics.setLayersCollide", b);
      engine.layers().setCollide(a, b, collide);
    });
    physics.set_function("layersCollide", [this](const std::string &a, const std::string &b) {
      requireLayer("physics.layersCollide", a);
      requireLayer("physics.layersCollide", b);
      return engine.layers().collide(a, b);
    });
    physics.set_function("gravity", [this](sol::optional<glm::vec3> gravity) {
      if (gravity)
        engine.physics().setGravity(*gravity);
      return engine.physics().getGravity();
    });

    // navigation
    // nav.build({ min = vec3, max = vec3, cellSize = 0.5, agentRadius = 0.35, agentHeight = 1.8, maxSlope = 45,
    //             stepHeight = 0.35 }): every field is optional; without min and max the map covers the static bodies
    sol::table nav = lua.create_named_table("nav");
    nav.set_function("build", [this](sol::optional<sol::table> options) {
      NavigationSettings settings;
      std::optional<glm::vec3> min, max;
      if (options) {
        const sol::table &o = *options;
        settings.cellSize = o.get_or("cellSize", settings.cellSize);
        settings.agentRadius = o.get_or("agentRadius", settings.agentRadius);
        settings.agentHeight = o.get_or("agentHeight", settings.agentHeight);
        settings.maxSlope = o.get_or("maxSlope", settings.maxSlope);
        settings.stepHeight = o.get_or("stepHeight", settings.stepHeight);
        min = vectorField(o, "min");
        max = vectorField(o, "max");
      }
      return engine.navigation().buildIn(min, max, settings);
    });
    // The points to walk, on the ground, or nil when there is no way (or no map)
    nav.set_function("path", [this](const glm::vec3 &from, const glm::vec3 &to) -> sol::object {
      const std::vector<glm::vec3> points = engine.navigation().findPath(from, to);
      if (points.empty())
        return sol::lua_nil;
      return sol::make_object(lua, sol::as_table(points));
    });
    nav.set_function("walkable", [this](const glm::vec3 &point) { return engine.navigation().isWalkable(point); });

    // sound: play and playAt return a handle (0: nothing played) for stop, setVolume and isPlaying
    sol::table sound = lua.create_named_table("sound");
    sound.set_function("play", [this](const std::string &path, sol::optional<float> volume, sol::optional<float> pitch) {
      return engine.playSound(path, volume.value_or(1.0f), pitch.value_or(1.0f));
    });
    sound.set_function("playAt", [this](const std::string &path, const glm::vec3 &position, sol::optional<float> volume) {
      return engine.playSoundAt(path, position, volume.value_or(1.0f));
    });
    sound.set_function("stop", [this](uint32_t handle) { engine.stopSound(handle); });
    sound.set_function("setVolume", [this](uint32_t handle, float volume) { engine.setSoundVolume(handle, volume); });
    sound.set_function("isPlaying", [this](uint32_t handle) { return engine.isSoundPlaying(handle); });
    sound.set_function("preload", [this](const std::string &path) { engine.preloadSound(path); });
    // music returns a handle like play does (0: nothing plays); the others take it to leave newer music alone, or
    // none for the music playing now
    sound.set_function("music", [this](const std::string &path, sol::optional<float> volume, sol::optional<bool> loop) {
      return engine.playMusic(path, volume.value_or(1.0f), loop.value_or(true));
    });
    sound.set_function("stopMusic", [this](sol::optional<uint32_t> handle) { engine.stopMusic(handle.value_or(0)); });
    sound.set_function("musicVolume", [this](float volume, sol::optional<uint32_t> handle) {
      engine.setMusicVolume(volume, handle.value_or(0));
    });
    sound.set_function("isMusicPlaying", [this](sol::optional<uint32_t> handle) {
      return engine.isMusicPlaying(handle.value_or(0));
    });

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

    // ui: RmlUi documents, changed by element id
    sol::table ui = lua.create_named_table("ui");
    ui.set_function("load", &GameUI::load, &engine.ui());
    // The ready-made pause menu: Escape opens it once enabled (see Engine::enablePauseMenu)
    ui.set_function("pauseMenu", [this](sol::optional<bool> enabled) { engine.ui().pauseMenu(enabled.value_or(true)); });
    ui.set_function("openPauseMenu", &GameUI::openPauseMenu, &engine.ui());
    ui.set_function("closePauseMenu", &GameUI::closePauseMenu, &engine.ui());
    ui.set_function("isPaused", &GameUI::isPaused, &engine.ui());
    // Any value, written as Lua's tostring does (12, 1.5, true)
    ui.set_function("text", [this](const std::string &id, sol::object value) {
      engine.ui().text(id, lua["tostring"](value).get<std::string>());
    });
    ui.set_function("show", &GameUI::show, &engine.ui());
    ui.set_function("class", &GameUI::setClass, &engine.ui());
    // ui.style("panel", "background-color", "#204060"): one RCSS property, "" to go back to the stylesheet's
    ui.set_function("style", &GameUI::style, &engine.ui());
    ui.set_function("attribute", &GameUI::attribute, &engine.ui());
    ui.set_function("onClick", [this](const std::string &id, sol::protected_function handler) {
      return engine.ui().onClick(id, callback(std::move(handler), "ui.onClick"));
    });
  }
};

ScriptSystem::ScriptSystem(Engine &engine) : m_impl(std::make_unique<Impl>(engine)) {}
ScriptSystem::~ScriptSystem() = default;

void ScriptSystem::update(float deltaTime) { m_impl->update(deltaTime); }

void ScriptSystem::reset() {
  Impl &impl = *m_impl;
  impl.instances.clear();
  while (!impl.routines.empty())
    impl.finishRoutine(impl.routines.begin()->second);
  impl.fields.clear();
  impl.appliedProperties.clear();
  impl.gameTable = impl.lua.create_named_table("game"); // shared state starts over too
  impl.lua.collect_garbage();
  if (impl.hasGameScript) {
    impl.gameScript = Impl::Instance{};
    impl.gameScript.path = impl.gameScriptPath;
    impl.load(impl.gameScript);
  }
}

bool ScriptSystem::runGameScript(const std::string &path) {
  Impl &impl = *m_impl;
  impl.gameScriptPath = EngineConfig::resolveAsset(path);
  impl.gameScript = Impl::Instance{};
  impl.gameScript.path = impl.gameScriptPath;
  impl.hasGameScript = true;
  return impl.load(impl.gameScript);
}

std::vector<ScriptProperty> ScriptSystem::getProperties(Entity entity) { return m_impl->propertiesOf(entity); }

sol::state &ScriptSystem::lua() { return m_impl->lua; }
const std::string &ScriptSystem::getLastError() const { return m_impl->lastError; }
size_t ScriptSystem::getInstanceCount() const { return m_impl->instances.size(); }
