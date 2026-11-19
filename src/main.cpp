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
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>

namespace {
constexpr const char *kUsage =
    "Usage: engine [example] [options]\n"
    "\n"
    "Examples:\n"
    "  aliens                 Space Aliens (the default)\n"
    "  blackhole              Black hole with gravitational lensing\n"
    "  stress                 2500 lit, shadowed boxes, for benchmarking\n"
    "  view <model.obj|.glb>  Any model, framed automatically\n"
    "  scene [file.json]      Scene files in assets/scenes; [ and ] switch between them\n"
    "\n"
    "Options:\n"
    "  --screenshot <file.bmp> [seconds]   Save a frame after that long (3 s by default) and quit\n"
    "  --benchmark [seconds]               Log frame-time statistics (10 s by default) and quit\n"
    "  --save-scene <file.json> [seconds]  Save what the example built (after 0 s by default) and quit\n"
    "  --deterministic                     Fixed 1/60 s frames and seeded randomness, for repeatable runs\n"
    "  --help                              This text\n";

std::string lowercase(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return std::tolower(c); });
  return text;
}
} // namespace

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
  bool exampleGiven = false;

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--help" || arg == "-h") {
      std::printf("%s", kUsage);
      return 0;
    } else if (arg == "--screenshot" && i + 1 < argc) {
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
    } else if (!exampleGiven && arg[0] != '-') {
      example = lowercase(arg);
      exampleGiven = true;
    } else {
      // Anything else was a typo: say so, rather than quietly starting the default example
      std::fprintf(stderr, "engine: unknown argument '%s'\n\n%s", arg.c_str(), kUsage);
      return 1;
    }
  }
  const char *const examples[] = {"aliens", "blackhole", "stress", "view", "scene"};
  if (std::find_if(std::begin(examples), std::end(examples), [&](const char *name) { return example == name; }) ==
      std::end(examples)) {
    std::fprintf(stderr, "engine: unknown example '%s'\n\n%s", example.c_str(), kUsage);
    return 1;
  }
  if (example == "view" && modelPath.empty()) {
    std::fprintf(stderr, "engine: view needs a model file, e.g. engine view ../assets/models/alien/alien.glb\n");
    return 1;
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
