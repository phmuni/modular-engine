// Main entry point for the application. Sets up the engine and runs the main loop with the provided application logic.
//
// Usage: engine [aliens|blackhole] [--screenshot <file.bmp> [seconds]] [--benchmark [seconds]]

#include "foundation/core/engine.h"
#include "game/blackHole.h"
#include "game/spaceAliens.h"
#include <cstdlib>
#include <memory>
#include <string>

int main(int argc, char **argv) {
  std::string example = "aliens";
  std::string screenshotPath;
  float screenshotDelay = 3.0f;
  float benchmarkSeconds = 0.0f;

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
    } else {
      example = arg;
    }
  }

  Engine engine;
  if (!engine.initialize())
    return 1;

  std::unique_ptr<App> application;
  if (example == "blackhole")
    application = std::make_unique<BlackHole>();
  else
    application = std::make_unique<SpaceAliens>();

  if (!screenshotPath.empty())
    engine.captureScreenshot(screenshotPath, screenshotDelay, true);

  if (benchmarkSeconds > 0.0f)
    engine.runBenchmark(benchmarkSeconds);

  engine.run(*application);
  return 0;
}
