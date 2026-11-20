// MyGame: an empty project on modular-engine in C++ and Lua. The level (assets/scenes/level.json) has a camera, a
// light, a sky and a floor; press F2 while it runs for the editor. The game script is assets/scripts/game.lua (it
// reloads when saved, no rebuild); this file is the C++ side. Both use the same API.

#include "foundation/core/engine.h"
#include "foundation/core/logger.h"
#include "systems/inputSystem.h"
#include "systems/scriptSystem.h"
#include <sol/sol.hpp> // to hand C++ functions to Lua scripts

class MyGame : public App {
public:
  // Once, after the engine started: window, input, functions for the scripts, then the first scene.
  void setup(Engine &engine) override {
    engine.setWindowTitle("MyGame");
    engine.setGameName("MyGame"); // the folder the saved data (save.get and save.set in Lua) is kept in

    engine.input().loadBindings("input.json"); // keys and gamepad: assets/input.json

    // A C++ function for the scripts, which call it as hello():
    // engine.scripts().lua()["hello"] = [] { LOG_I("[MyGame] Hello from C++"); };

    engine.loadScene("scenes/level.json"); // its entities arrive in onSceneLoaded
  }

  // After a scene has loaded: the game script starts (its onStart, then its onUpdate every frame).
  void onSceneLoaded(Engine &engine, const std::string &) override {
    engine.scripts().runGameScript("scripts/game.lua");
  }

  // Every frame; deltaTime is in seconds.
  void update(Engine &engine, float deltaTime) override {
    (void)engine;
    (void)deltaTime;
  }
};

int main(int argc, char **argv) {
  // --screenshot, --benchmark, --deterministic and --save-scene work in every game
  const Engine::CommandLine commandLine = Engine::parseCommandLine(argc, argv);

  Engine engine;
  if (!engine.initialize())
    return 1;
  engine.applyCommandLine(commandLine);

  // Game components (plain structs) are registered here with engine.registerComponent<T>("Name")

  MyGame game;
  engine.run(game);
  return 0;
}
