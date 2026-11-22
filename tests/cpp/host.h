#pragma once
// The App every test runs in: it prepares the engine for unattended runs, calls the test every frame, and ends the run
// when the test says it is over, fails, or takes too many frames.

#include "foundation/core/app.h"
#include <memory>
#include <string>

class Engine;

class TestHost : public App {
public:
  explicit TestHost(int maxFrames) : m_maxFrames(maxFrames) {}

  void setup(Engine &engine) final;
  void update(Engine &engine, float deltaTime) final;
  // A Stop freezes the game like Editing does, and a test has nobody to unfreeze it: it goes on at once
  void onPlayStopped(Engine &engine) final;

  int frame() const { return m_frame; }

protected:
  // The test's own start (after the engine is ready for it) and per-frame work; update returns true when it is over
  virtual void begin(Engine &engine) = 0;
  virtual bool step(Engine &engine) = 0;
  // What to say when the frame limit is reached first
  virtual std::string timeoutMessage() const = 0;

private:
  int m_maxFrames;
  int m_frame = 0;
  bool m_finished = false;
};

// bin/engine_tests <file.lua>: a Lua test script, with the `test` helper (tests/lua/test.lua) in its globals
std::unique_ptr<TestHost> makeLuaTest(const std::string &path, int maxFrames);
// bin/engine_tests <name>: a C++ test case registered with TEST_CASE
std::unique_ptr<TestHost> makeCppTest(const std::string &name, int maxFrames);
