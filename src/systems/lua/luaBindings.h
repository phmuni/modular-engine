#pragma once
// The Lua bindings, private to ScriptSystem: the API scripts see, one file per area (bindEntity.cpp, bindEngine.cpp,
// bindGame.cpp, bindInput.cpp, bindPhysics.cpp, bindAudio.cpp, bindSettings.cpp, bindUi.cpp, bindMath.cpp), with what
// several of them share in bindHelpers.cpp.
//
// The rule for the bindings: a binding only converts types (Lua table <-> struct, nil <-> optional or empty handle,
// Lua function <-> std::function, entity <-> GameEntity) and calls the C++ game API (GameEntity, GameUI, GameCamera,
// GameTime, Engine and the systems). Behavior lives in C++, where C++ games reach it too, so the two languages cannot
// drift apart. tools/check_api_parity.py checks that every public method of the facade classes is bound here, and
// tools/check_lua_api.py that every binding is described for the editor in templates/common/lua/types/engine.lua.

#include "components/cameraRig.h"
#include "foundation/core/engine.h"
#include "foundation/core/gameEntity.h"
#include "systems/collisionSystem.h"
#include "systems/gameTween.h"
#include "systems/playerSettings.h"
#include <functional>
#include <optional>
#include <sol/sol.hpp>
#include <string>

// What the bindings need from the script system itself: its error reports, the Lua objects of entities and the
// coroutines. ScriptSystem implements it.
class LuaHost {
public:
  virtual ~LuaHost() = default;
  virtual void report(const std::string &path, const std::string &message) = 0;
  virtual sol::object entityObject(const GameEntity &entity) = 0;
  virtual sol::table &fieldsOf(Entity entity) = 0;
  virtual int startRoutine(const sol::protected_function &function, Entity owner, const char *where) = 0;
  virtual void stopRoutine(int id) = 0;
};

class LuaBindings {
public:
  LuaBindings(Engine &engine, sol::state &lua, LuaHost &host) : engine(engine), lua(lua), host(host) {}

  // Registers everything, usertypes before the functions that use them
  void bindAll() {
    bindMath();
    bindEntity();
    bindEngine();
    bindGame();
    bindInput();
    bindPhysics();
    bindAudio();
    bindSettings();
    bindUi();
  }

private:
  Engine &engine;
  sol::state &lua;
  LuaHost &host;

  // The script system's, under the names the bindings use
  void report(const std::string &path, const std::string &message) { host.report(path, message); }
  sol::object entityObject(const GameEntity &entity) { return host.entityObject(entity); }
  sol::table &fieldsOf(Entity entity) { return host.fieldsOf(entity); }
  int startRoutine(const sol::protected_function &function, Entity owner, const char *where) {
    return host.startRoutine(function, owner, where);
  }
  void stopRoutine(int id) { host.stopRoutine(id); }

  void bindMath();     // bindMath.cpp: vec3, Transform, Light
  void bindEntity();   // bindEntity.cpp: Entity
  void bindEngine();   // bindEngine.cpp: print, wait, engine
  void bindGame();     // bindGame.cpp: camera, time, tween
  void bindInput();    // bindInput.cpp: input
  void bindPhysics();  // bindPhysics.cpp: physics, nav
  void bindAudio();    // bindAudio.cpp: sound
  void bindSettings(); // bindSettings.cpp: save, settings
  void bindUi();       // bindUi.cpp: ui

  // Shared conversions and checks, in bindHelpers.cpp
  bool knownSetting(const char *where, const std::string &name);
  sol::object valueToLua(const PlayerSettings::Value &value);
  static std::optional<PlayerSettings::Value> luaToValue(const sol::object &value);
  std::function<void()> callback(sol::protected_function handler, std::string where);
  static std::optional<glm::vec3> vectorField(const sol::table &table, const char *key);
  static EntityDesc entityDesc(const sol::optional<sol::table> &options);
  uint32_t layerMaskOf(const sol::table &names);
  sol::table hitTable(const RayHit &hit);
  void requireLayer(const char *function, const std::string &name);
  static Ease easeNamed(const std::string &name);
  static TweenProperty tweenProperty(const std::string &name);
  static CameraRig cameraRigOptions(const sol::optional<sol::table> &options);
};
