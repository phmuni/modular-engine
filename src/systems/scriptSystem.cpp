// Lua scripting with sol2: script instances per entity, hot reload, and the API scripts see.
//
// The bindings (the API scripts see) are in lua/, one file per area; the rule they follow is at the top of
// lua/luaBindings.h.

#include <sol/sol.hpp> // SOL_ALL_SAFETIES_ON comes from the build, the same for every file that includes sol2

#include "systems/scriptSystem.h"

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

} // namespace

struct ScriptSystem::Impl : LuaHost {
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
  // A second script beside the game's (the --script option): same globals and `game` table, its own onStart and
  // onUpdate, started after the game script's
  Instance sideScript;
  bool hasSideScript = false;
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

  LuaBindings bindings;

  explicit Impl(Engine &e) : engine(e), bindings(e, lua, *this) {
    lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::string, sol::lib::table, sol::lib::os,
                       sol::lib::io, sol::lib::coroutine, sol::lib::utf8, sol::lib::package);
    // require("enemies") finds assets/scripts/enemies.lua
    const std::string scripts = EngineConfig::resolvePath("../assets/scripts");
    lua["package"]["path"] = scripts + "/?.lua;" + scripts + "/?/init.lua";
    gameTable = lua.create_named_table("game");
    bindings.bindAll();
  }

  // --- Errors ---------------------------------------------------------------------------------------------------

  void report(const std::string &path, const std::string &message) override {
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

  sol::table &fieldsOf(Entity entity) override {
    auto it = fields.find(entity);
    if (it == fields.end())
      it = fields.emplace(entity, lua.create_table()).first;
    return it->second;
  }

  // A script sees an entity that is gone (or was never found) as nil
  sol::object entityObject(const GameEntity &entity) override {
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
    if (hasSideScript && !sideScript.started) {
      sideScript.started = true;
      call(sideScript, "onStart");
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
    if (hasSideScript)
      call(sideScript, "onUpdate", deltaTime);
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
    const std::vector<PhysicsContactEnd> ends = engine.physics().getContactEnds();
    for (const PhysicsContactEnd &end : ends) {
      notify(end.a, end.b, "onContactEnd");
      notify(end.b, end.a, "onContactEnd");
    }
    if (!instances.empty()) {
      const std::vector<CollisionEvent> overlaps = engine.findCollisions();
      for (const CollisionEvent &pair : overlaps) {
        notify(pair.a, pair.b, "onOverlap");
        notify(pair.b, pair.a, "onOverlap");
      }
    }
  }

  // An animation state machine moved to another state: the entity's script hears of it
  void animationStateChanged(Entity entity, const std::string &from, const std::string &to, const std::string &layer) {
    auto it = instances.find(entity);
    if (it != instances.end() && it->second.started && engine.isAlive(entity))
      call(it->second, "onStateChanged", engine.entity(entity), from, to, layer);
  }

  // An animation event of the entity's state machine: its script hears of it, like of a state change
  void animationEvent(Entity entity, const AnimationEvent &event) {
    auto it = instances.find(entity);
    if (it == instances.end() || !it->second.started || !engine.isAlive(entity))
      return;
    static const char *const phases[] = {"fire", "begin", "end"};
    call(it->second, "onAnimationEvent", engine.entity(entity), event.name, phases[static_cast<int>(event.phase)],
         event.layer);
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
  int startRoutine(const sol::protected_function &function, Entity owner, const char *where) override {
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

  void stopRoutine(int id) override {
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

bool ScriptSystem::runSideScript(const std::string &path) {
  Impl &impl = *m_impl;
  impl.sideScript = Impl::Instance{};
  impl.sideScript.path = EngineConfig::resolveAsset(path);
  impl.hasSideScript = true;
  return impl.load(impl.sideScript);
}

std::vector<ScriptProperty> ScriptSystem::getProperties(Entity entity) { return m_impl->propertiesOf(entity); }

sol::state &ScriptSystem::lua() { return m_impl->lua; }
const std::string &ScriptSystem::getLastError() const { return m_impl->lastError; }
void ScriptSystem::menuOpened(const std::string &menu) {
  Impl &impl = *m_impl;
  if (impl.hasGameScript)
    impl.call(impl.gameScript, "onMenuOpened", menu);
}
void ScriptSystem::menuClosed(const std::string &menu) {
  Impl &impl = *m_impl;
  if (impl.hasGameScript)
    impl.call(impl.gameScript, "onMenuClosed", menu);
}
void ScriptSystem::menuAction(const std::string &menu, const std::string &name) {
  Impl &impl = *m_impl;
  if (impl.hasGameScript)
    impl.call(impl.gameScript, "onMenuAction", menu, name);
}
void ScriptSystem::animationStateChanged(Entity entity, const std::string &from, const std::string &to,
                                         const std::string &layer) {
  m_impl->animationStateChanged(entity, from, to, layer);
}
void ScriptSystem::animationEvent(Entity entity, const AnimationEvent &event) { m_impl->animationEvent(entity, event); }
size_t ScriptSystem::getInstanceCount() const { return m_impl->instances.size(); }
