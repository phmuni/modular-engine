// The Lua runner: plays a Lua project, a game with no C++ of its own.
//
// A Lua project is a folder with project.json, assets/ and bin/, where this executable sits (renamed after the
// game, see tools/new_game.py). project.json says where the game starts:
//   { "name": "MyGame", "scene": "scenes/level.json", "script": "scripts/game.lua", "input": "input.json" }
// (paths relative to assets/). An optional "layers" sets the collision layers (see CollisionLayers::fromJson), and an
// optional "window" the window: { "width": 1280, "height": 720, "fullscreen": false, "renderScale": 1.0 }, every field
// optional (see Engine::configureWindow). The scene loads first, then the game script runs (onStart, then onUpdate every frame),
// and the entities' own scripts come with the scene. The editor (F2), Play and Stop work as in any game.

#include "foundation/core/config.h"
#include "foundation/core/engine.h"
#include "foundation/core/logger.h"
#include "systems/inputSystem.h"
#include "systems/scriptSystem.h"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <string>

namespace {
struct Project {
  std::string name = "Game";
  std::string scene;
  std::string script;
  std::string input;
  nlohmann::json layers; // null when the project does not set any
  Engine::WindowConfig window;
};

std::string inAssets(const std::string &path) { return path.empty() ? path : "../assets/" + path; }

bool readProject(Project &project, std::string &error) {
  const std::string path = EngineConfig::resolvePath("../project.json");
  std::ifstream file(path);
  if (!file) {
    error = "No project.json next to this program's folder (" + path +
            ").\nThis runner plays Lua projects: create one with the launcher or tools/new_game.py.";
    return false;
  }
  try {
    const nlohmann::json json = nlohmann::json::parse(file);
    project.name = json.value("name", project.name);
    project.scene = json.value("scene", "");
    project.script = json.value("script", "");
    project.input = json.value("input", "");
    if (json.contains("layers"))
      project.layers = json["layers"];
    if (json.contains("window") && json["window"].is_object()) {
      const nlohmann::json &window = json["window"];
      project.window.width = window.value("width", project.window.width);
      project.window.height = window.value("height", project.window.height);
      project.window.fullscreen = window.value("fullscreen", project.window.fullscreen);
      project.window.renderScale = window.value("renderScale", project.window.renderScale);
    }
  } catch (const std::exception &exception) {
    error = path + " is not valid JSON: " + exception.what();
    return false;
  }
  return true;
}

class LuaGame : public App {
public:
  explicit LuaGame(Project project) : m_project(std::move(project)) {}

  void setup(Engine &engine) override {
    engine.setWindowTitle(m_project.name);
    engine.setGameName(m_project.name); // where the save data is kept
    if (!m_project.layers.is_null())
      engine.layers().fromJson(m_project.layers); // before the scene: its bodies are made on these layers
    engine.setState(Toggle::CameraMovement, false); // the game's scripts drive the camera
    if (!m_project.input.empty() && std::filesystem::exists(EngineConfig::resolvePath(inAssets(m_project.input))))
      engine.input().loadBindings(inAssets(m_project.input));
    if (!m_project.scene.empty())
      engine.loadScene(inAssets(m_project.scene)); // the game script starts once it is in (onSceneLoaded)
    else
      startScript(engine);
  }

  void onSceneLoaded(Engine &engine, const std::string &) override {
    if (!m_started)
      startScript(engine);
  }

  void update(Engine &, float) override {} // the scripts do the work

private:
  Project m_project;
  bool m_started = false;

  void startScript(Engine &engine) {
    m_started = true;
    if (!m_project.script.empty())
      engine.scripts().runGameScript(m_project.script);
  }
};
} // namespace

int main(int argc, char **argv) {
  const Engine::CommandLine commandLine = Engine::parseCommandLine(argc, argv);
  Project project;
  std::string error;
  if (!readProject(project, error)) {
    std::fprintf(stderr, "%s\n", error.c_str());
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Cannot start the game", error.c_str(), nullptr);
    return 1;
  }

  Engine engine;
  engine.configureWindow(project.window); // before initialize(): the window is created at its size
  if (!engine.initialize())
    return 1;
  engine.applyCommandLine(commandLine);
  LuaGame game(project);
  engine.run(game);
  return 0;
}
