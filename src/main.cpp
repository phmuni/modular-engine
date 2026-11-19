// Main entry point for the application. Sets up the engine and runs the main loop with the provided application logic.
//
// Usage: engine [aliens|blackhole|stress|view <model.obj|.glb>|scene [file.json]] [options]; engine --help lists them

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
constexpr const char *kUsage = "Usage: engine [example] [options]\n"
                               "\n"
                               "Examples:\n"
                               "  aliens                 Space Aliens (the default)\n"
                               "  blackhole              Black hole with gravitational lensing\n"
                               "  stress                 2500 lit, shadowed boxes, for benchmarking\n"
                               "  view <model.obj|.glb>  Any model, framed automatically\n"
                               "  scene [file.json]      Scene files in assets/scenes; [ and ] switch between them\n"
                               "\n"
                               "Options:\n";

void printUsage(FILE *stream) { std::fprintf(stream, "%s%s  --help                              This text\n", kUsage, Engine::commandLineHelp()); }

std::string lowercase(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return std::tolower(c); });
  return text;
}
} // namespace

int main(int argc, char **argv) {
  // The engine's options (--screenshot, --benchmark...) are read by the engine; the rest name the example
  const Engine::CommandLine commandLine = Engine::parseCommandLine(argc, argv);
  std::string example = "aliens";
  std::string fileArgument; // view's model, scene's scene file
  bool exampleGiven = false;
  for (const std::string &arg : commandLine.arguments) {
    if (arg == "--help" || arg == "-h") {
      printUsage(stdout);
      return 0;
    } else if (!exampleGiven && arg[0] != '-') {
      example = lowercase(arg);
      exampleGiven = true;
    } else if ((example == "view" || example == "scene") && fileArgument.empty() && arg[0] != '-') {
      fileArgument = std::filesystem::absolute(arg).string(); // relative to where the engine was started
    } else {
      // Anything else was a typo: say so, rather than quietly starting the default example
      std::fprintf(stderr, "engine: unknown argument '%s'\n\n", arg.c_str());
      printUsage(stderr);
      return 1;
    }
  }
  const char *const examples[] = {"aliens", "blackhole", "stress", "view", "scene"};
  if (std::find_if(std::begin(examples), std::end(examples), [&](const char *name) { return example == name; }) ==
      std::end(examples)) {
    std::fprintf(stderr, "engine: unknown example '%s'\n\n", example.c_str());
    printUsage(stderr);
    return 1;
  }
  if (example == "view" && fileArgument.empty()) {
    std::fprintf(stderr, "engine: view needs a model file, e.g. engine view ../assets/models/alien/alien.glb\n");
    return 1;
  }

  Engine engine;
  if (!engine.initialize())
    return 1;
  engine.applyCommandLine(commandLine);

  std::unique_ptr<App> application;
  if (example == "blackhole")
    application = std::make_unique<BlackHole>();
  else if (example == "stress")
    application = std::make_unique<Stress>();
  else if (example == "scene")
    application = std::make_unique<SceneBrowser>(fileArgument);
  else if (example == "view")
    application = std::make_unique<Viewer>(fileArgument);
  else
    application = std::make_unique<SpaceAliens>();

  engine.run(*application);
  return 0;
}
