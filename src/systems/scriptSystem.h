#pragma once
// Lua scripting: entity scripts (the Script component), a game script, and the engine's API for both.
//
// Scripts run every frame after the app's update (not while the editor is editing), are reloaded when their file
// changes (keeping the state on `self`), and report errors with their file and line without stopping the game. C++
// can add its own functions and types for scripts through lua() (include <sol/sol.hpp> for that), e.g.
//   engine.scripts().lua()["pathfind"] = [](glm::vec3 from, glm::vec3 to) { return findPath(from, to); };

#include "components/script.h"
#include "foundation/ecs/entityManager.h"
#include "foundation/ecs/systemManager.h"
#include <memory>
#include <string>
#include <vector>

class Engine;
namespace sol {
class state;
}

// A value a script declares in its `properties` table, with the script's own value as the default
struct ScriptProperty {
  std::string name;
  ScriptValue value;
};

class ScriptSystem : public BaseSystem {
public:
  explicit ScriptSystem(Engine &engine);
  ~ScriptSystem();

  void update(float deltaTime);
  // Forgets every script's state: entity scripts start again (onStart), and the game script runs again from the
  // top (onStart). For when the entities were replaced wholesale (the editor's Stop).
  void reset();

  // A script for the whole game, not tied to an entity: onStart() once, onUpdate(dt) every frame. Its globals and
  // the shared `game` table are seen by every script.
  bool runGameScript(const std::string &path);

  // The properties the entity's script declares, by name, with their defaults (empty if it has no script, or the file
  // is missing or broken). Reads the file if the game has not started it (the editor asks while editing), running its
  // top level once; a changed file is read again.
  std::vector<ScriptProperty> getProperties(Entity entity);

  // Tells the game script that the pause menu opened or closed (onPauseMenuOpened, onPauseMenuClosed), or that a button
  // of its document asked for an action (onPauseMenuAction(name))
  void pauseMenuOpened();
  void pauseMenuClosed();
  void pauseMenuAction(const std::string &name);

  // Tells the entity's script (onStateChanged) that its animation state machine moved from one state to another
  void animationStateChanged(Entity entity, const std::string &from, const std::string &to);

  // The Lua state, to give scripts more functions or types from C++ (with sol2)
  sol::state &lua();

  // The last error (file:line: message), until that script loads and runs cleanly again; empty when all is well
  const std::string &getLastError() const;
  size_t getInstanceCount() const;

private:
  struct Impl;
  std::unique_ptr<Impl> m_impl;
};
