// bin/engine_tests: runs one behavior test of the game API, deterministically, and exits non-zero if it fails.
//
//   engine_tests --list              the C++ tests (TEST_CASE) by name
//   engine_tests <name>              one C++ test
//   engine_tests <file.lua>          one Lua test script (tests/lua), with the `test` helper
//   options: --frames <n>            the frame limit (1200 by default, 20 s of game time) before a test counts as hung
//
// One test per process: the engine opens a window and a GL context. tools/run_tests.py runs them all.

#include "foundation/core/engine.h"
#include "host.h"
#include "systems/audioSystem.h"
#include "systems/saveData.h"
#include "systems/windowSystem.h"
#include "testing.h"
#include <cstdio>
#include <cstdlib>
#include <string>

int main(int argc, char **argv) {
  Engine::CommandLine commandLine = Engine::parseCommandLine(argc, argv);
  commandLine.deterministic = true; // fixed 1/60 s frames and seeded randomness: the tests count frames
  int maxFrames = 1200;
  std::string test;
  for (size_t i = 0; i < commandLine.arguments.size(); ++i) {
    const std::string &arg = commandLine.arguments[i];
    if (arg == "--list") {
      for (const testing::TestCase &testCase : testing::registry())
        std::printf("%s\n", testCase.name);
      return 0;
    } else if (arg == "--frames" && i + 1 < commandLine.arguments.size()) {
      maxFrames = std::atoi(commandLine.arguments[++i].c_str());
    } else if (test.empty() && arg[0] != '-') {
      test = arg;
    } else {
      std::fprintf(stderr, "engine_tests: unknown argument '%s'\n", arg.c_str());
      return 2;
    }
  }
  if (test.empty()) {
    std::fprintf(stderr, "Usage: engine_tests --list | <cpp test name> | <file.lua> [--frames <n>]\n");
    return 2;
  }

  const bool isLua = test.size() > 4 && test.compare(test.size() - 4, 4, ".lua") == 0;
  std::unique_ptr<TestHost> host = isLua ? makeLuaTest(test, maxFrames) : makeCppTest(test, maxFrames);
  if (!host) {
    std::fprintf(stderr, "engine_tests: no C++ test named '%s' (--list shows them)\n", test.c_str());
    return 2;
  }

  // Nobody sees or hears a test: no sound card to open, and no window to wait for the system to bring to the front
  WindowSystem::setUnattended(true);
  AudioSystem::setNullBackend(true);
  Engine engine;
  if (!engine.initialize()) {
    std::fprintf(stderr, "engine_tests: the engine could not start (no window or OpenGL 4.1?)\n");
    return 2;
  }
  engine.applyCommandLine(commandLine);
  for (const auto &[name, startup] : testing::startups())
    if (name == test)
      startup(engine, engine.saveData()); // before run(): what the game would have configured, and what was saved
  engine.run(*host);

  const std::vector<std::string> &failures = testing::context().failures;
  if (failures.empty()) {
    std::printf("PASS %s (%d frames)\n", test.c_str(), host->frame());
    return 0;
  }
  for (const std::string &failure : failures)
    std::printf("  %s\n", failure.c_str());
  std::printf("FAIL %s: %zu failure%s\n", test.c_str(), failures.size(), failures.size() == 1 ? "" : "s");
  return 1;
}
