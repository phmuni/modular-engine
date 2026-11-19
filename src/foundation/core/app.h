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
};
