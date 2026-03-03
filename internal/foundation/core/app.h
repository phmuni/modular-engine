#pragma once

class Engine;

class App {
public:
  virtual ~App() = default;

  virtual void setup(Engine &engine) = 0;
  virtual void update(Engine &engine, float deltaTime) = 0;
};
