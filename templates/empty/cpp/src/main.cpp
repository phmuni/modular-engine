// MyGame: an empty project on modular-engine. The level (assets/scenes/level.json) has a camera, a light, a sky and a
// floor; press F2 while it runs for the editor (click to select, W/E/R/T for the gizmos, Settings to save the scene).
// The functions below are the game's lifecycle: fill them in.

#include "foundation/core/engine.h"
#include "foundation/core/logger.h"
#include "systems/inputSystem.h"

class MyGame : public App {
public:
  // Once, after the engine started: window, input, UI, then the first scene.
  void setup(Engine &engine) override {
    engine.setWindowTitle("MyGame");
    engine.setGameName("MyGame"); // the folder the saved data (engine.saveData()) is kept in
    // engine.ui().pauseMenu(true); // uncomment for an Escape menu: assets/ui/pauseMenu.rml (Resume, Options, Quit)

    // Bindings of actions and axes (engine.input().bindAction / bindAxis) go here; assets/input.json (if there)
    // replaces them, no rebuild needed
    engine.input().loadBindings("input.json");

    engine.loadScene("scenes/level.json"); // its entities arrive in onSceneLoaded
  }

  // After a scene has loaded: find its entities, spawn more (engine.create, engine.spawn).
  void onSceneLoaded(Engine &engine, const std::string &scene) override {
    (void)engine;
    LOG_I("[MyGame] Loaded %s", scene.c_str());
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

  // Game components (plain structs) are registered here with engine.registerComponent<T>("Name"): they are then saved
  // in scene files and editable in the Properties panel.

  MyGame game;
  engine.run(game);
  return 0;
}
