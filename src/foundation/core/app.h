#pragma once
// Base class for user applications.

#include <string>

class Engine;

class App {
public:
  virtual ~App() = default;

  virtual void setup(Engine &engine) = 0;
  virtual void update(Engine &engine, float deltaTime) = 0;
  // After Engine::loadScene replaced the entities; entities the app kept handles to are gone
  virtual void onSceneLoaded(Engine &engine, const std::string &path) {}
  // After the editor's Stop put back the entities and components it saved on Play. Entity handles taken before Play
  // are valid again; state the app keeps in its own members (not in components) may want resetting here.
  virtual void onPlayStopped(Engine &engine) {}
};
