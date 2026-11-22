// The test host and the C++ test runner: see host.h.

#include "host.h"

#include "foundation/core/engine.h"
#include "foundation/core/logger.h"
#include "testing.h"
#include <SDL3/SDL.h>
#include <exception>

void TestHost::setup(Engine &engine) {
  // Not waiting for the screen: vsync would make a 600-frame test last ten seconds
  engine.setState(Toggle::VSync, false);
  // A small window: the frames are drawn in software on a machine with no GPU
  int count = 0;
  if (SDL_Window **windows = SDL_GetWindows(&count)) {
    for (int i = 0; i < count; ++i)
      SDL_SetWindowSize(windows[i], 320, 180);
    SDL_free(windows);
  }
  engine.setState(Toggle::FreeCamera, false); // the free camera would fight the game's, as the Lua runner knows
  engine.setMouseCaptured(false);
  engine.createCameraEntity("TestCamera", glm::vec3(0.0f, 2.0f, 8.0f));
  try {
    begin(engine);
  } catch (const std::exception &error) {
    testing::context().fail(std::string("uncaught exception: ") + error.what());
    m_finished = true;
    engine.quit();
  }
}

void TestHost::update(Engine &engine, float) {
  if (m_finished)
    return;
  ++m_frame;
  try {
    if (step(engine))
      m_finished = true;
  } catch (const std::exception &error) {
    // Engine::run would show a dialog for this, which nobody is there to close
    testing::context().fail(std::string("uncaught exception: ") + error.what());
    m_finished = true;
  }
  if (!m_finished && m_frame >= m_maxFrames) {
    testing::context().fail(timeoutMessage());
    m_finished = true;
  }
  if (m_finished)
    engine.quit();
}

void TestHost::onPlayStopped(Engine &engine) { engine.resume(); }

namespace {
class CppTest final : public TestHost {
public:
  CppTest(testing::TestCase test, int maxFrames) : TestHost(maxFrames), m_test(test) {}

protected:
  void begin(Engine &engine) override { m_test.function(engine, m_steps); }
  bool step(Engine &) override { return m_steps.tick(); }
  std::string timeoutMessage() const override {
    return std::string("timed out after ") + std::to_string(frame()) + " frames";
  }

private:
  testing::TestCase m_test;
  testing::Steps m_steps;
};
} // namespace

std::unique_ptr<TestHost> makeCppTest(const std::string &name, int maxFrames) {
  for (const testing::TestCase &test : testing::registry())
    if (name == test.name)
      return std::make_unique<CppTest>(test, maxFrames);
  return nullptr;
}
