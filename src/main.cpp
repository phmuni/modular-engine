// Main entry point for the application. Sets up the engine and runs the main loop with the provided application logic.

#include "foundation/core/engine.h"
#include "game/spaceAliens.h"

int main() {
  Engine engine;
  if (!engine.initialize())
    return 1;
  SpaceAliens application;
  engine.run(application);
  return 0;
}