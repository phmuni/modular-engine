// Main entry point for the application. Sets up the engine and runs the main loop with the provided application logic.
//
// Usage: engine [aliens|blackhole|stress|view <model.obj|.glb>|scene [file.json]] [--screenshot <file.bmp> [seconds]]
//               [--benchmark [seconds]] [--save-scene <file.json> [seconds]] [--deterministic]

#include "foundation/core/engine.h"
#include "game/blackHole.h"
#include "game/sceneBrowser.h"
#include "game/spaceAliens.h"
#include "game/stress.h"
#include "game/viewer.h"
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>

int main(int argc, char **argv) {
  std::string example = "aliens";
  std::string screenshotPath;
  float screenshotDelay = 3.0f;
  float benchmarkSeconds = 0.0f;
  std::string modelPath;
  std::string sceneFile;
  std::string saveScenePath;
  float saveSceneDelay = 0.0f;
  bool deterministic = false;

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--screenshot" && i + 1 < argc) {
      screenshotPath = argv[++i];
      if (i + 1 < argc && argv[i + 1][0] != '-')
        screenshotDelay = std::strtof(argv[++i], nullptr);
    } else if (arg == "--benchmark") {
      benchmarkSeconds = 10.0f;
      if (i + 1 < argc && argv[i + 1][0] != '-')
        benchmarkSeconds = std::strtof(argv[++i], nullptr);
    } else if (arg == "--deterministic") {
      deterministic = true;
    } else if (arg == "--save-scene" && i + 1 < argc) {
      saveScenePath = std::filesystem::absolute(argv[++i]).string();
      if (i + 1 < argc && argv[i + 1][0] != '-')
        saveSceneDelay = std::strtof(argv[++i], nullptr);
    } else if (example == "scene" && sceneFile.empty()) {
      sceneFile = std::filesystem::absolute(arg).string();
    } else if (example == "view" && modelPath.empty()) {
      modelPath = std::filesystem::absolute(arg).string(); // relative to where the engine was started
    } else {
      example = arg;
    }
  }

  Engine engine;
  if (!engine.initialize())
    return 1;
  // Before the app exists, so its setup already runs on fixed frames and seeded randomness
  if (deterministic)
    engine.setDeterministic();

  std::unique_ptr<App> application;
  if (example == "blackhole")
    application = std::make_unique<BlackHole>();
  else if (example == "stress")
    application = std::make_unique<Stress>();
  else if (example == "scene")
    application = std::make_unique<SceneBrowser>(sceneFile);
  else if (example == "view" && !modelPath.empty())
    application = std::make_unique<Viewer>(modelPath);
  else
    application = std::make_unique<SpaceAliens>();

  if (!screenshotPath.empty())
    engine.captureScreenshot(screenshotPath, screenshotDelay, true);

  // Saves what the example built after `seconds` of running, then quits
  if (!saveScenePath.empty())
    engine.saveSceneAfter(saveScenePath, saveSceneDelay, true);

  if (benchmarkSeconds > 0.0f)
    engine.runBenchmark(benchmarkSeconds);

  engine.run(*application);
  return 0;
}
