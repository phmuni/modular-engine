// MyGame: a starting point for a game on modular-engine. Drive the box with W A S D and pick up the gold spheres.
//
// The level is a scene file, assets/scenes/level.json: press F2 for the editor, move things around (click to select,
// W/E/R/T for the gizmos) and save it from the Settings panel. The code below builds what the level file does not
// have (the player, the coins) once the level has loaded.

#include "components/camera.h"
#include "components/collision.h"
#include "foundation/core/config.h"
#include "foundation/core/engine.h"
#include "foundation/core/logger.h"
#include "systems/inputSystem.h"
#include <cmath>
#include <glm/gtc/constants.hpp>
#include <vector>

// Game components: plain structs
struct Player {
  float speed = 6.0f;       // units per second
  float turnSpeed = 160.0f; // degrees per second
};
struct Coin {
  float spin = 120.0f; // degrees per second
};

class MyGame : public App {
  static constexpr const char *LEVEL = "../assets/scenes/level.json";
  static constexpr glm::vec3 GOLD{1.0f, 0.78f, 0.25f};

  Entity player = -1;
  int coinsLeft = 0;

  void spawnPlayer(Engine &engine) {
    player = engine.entities()
                 .create("Player")
                 .withModel(EngineConfig::MODEL_BOX, glm::vec3(0.0f, 0.4f, 3.0f), glm::vec3(0.0f), glm::vec3(0.8f))
                 .withSolidColor(glm::vec3(0.2f, 0.55f, 1.0f))
                 .withModelCollision()
                 .withComponent<Player>()
                 .build();
  }

  void spawnCoins(Engine &engine) {
    coinsLeft = 0;
    for (int i = 0; i < 8; ++i) {
      const float angle = glm::two_pi<float>() * static_cast<float>(i) / 8.0f;
      const glm::vec3 position(std::cos(angle) * 7.0f, 0.6f, std::sin(angle) * 7.0f);
      engine.entities()
          .create("Coin")
          .withModel(EngineConfig::MODEL_SPHERE, position, glm::vec3(0.0f), glm::vec3(0.5f))
          .withSolidColor(GOLD)
          .withEmission(GOLD, 1.5f)
          .withModelCollision()
          .withComponent<Coin>()
          .build();
      ++coinsLeft;
    }
  }

  void updatePlayer(Engine &engine, float deltaTime) {
    auto &input = engine.input();
    auto &transform = engine.get<Transform>(player);
    const Player &settings = engine.get<Player>(player);

    float turn = 0.0f, drive = 0.0f;
    if (input.isKeyDown(SDL_SCANCODE_A) || input.isKeyDown(SDL_SCANCODE_LEFT))
      turn += 1.0f;
    if (input.isKeyDown(SDL_SCANCODE_D) || input.isKeyDown(SDL_SCANCODE_RIGHT))
      turn -= 1.0f;
    if (input.isKeyDown(SDL_SCANCODE_W) || input.isKeyDown(SDL_SCANCODE_UP))
      drive += 1.0f;
    if (input.isKeyDown(SDL_SCANCODE_S) || input.isKeyDown(SDL_SCANCODE_DOWN))
      drive -= 1.0f;

    transform.rotation.y += turn * settings.turnSpeed * deltaTime;
    const glm::vec3 previous = transform.position;
    transform.position += forward(transform) * drive * settings.speed * deltaTime;

    // Static boxes (the crates, the wall) stop it: undo the step if it now overlaps one
    for (const CollisionEvent &pair : engine.findCollisions()) {
      const Entity other = pair.a == player ? pair.b : pair.b == player ? pair.a : -1;
      if (other != -1 && engine.get<Collision>(other).isStatic) {
        transform.position = previous;
        break;
      }
    }
  }

  void updateCoins(Engine &engine, float deltaTime) {
    engine.getComponentManager().forEachComponent<Coin>([&](Entity coin, Coin &settings) {
      engine.get<Transform>(coin).rotation.y += settings.spin * deltaTime;
    });

    std::vector<Entity> collected;
    engine.forEachCollision<Player, Coin>([&](Entity, Player &, Entity coin, Coin &) { collected.push_back(coin); });
    for (Entity coin : collected) {
      engine.destroyEntity(coin);
      LOG_I("[MyGame] Coin! %d left", --coinsLeft);
    }
    if (!collected.empty() && coinsLeft == 0) {
      LOG_I("[MyGame] All coins collected. Here come more.");
      spawnCoins(engine);
    }
  }

  // Behind and above the player, looking ahead of it
  void updateCamera(Engine &engine) {
    Camera *camera = engine.getActiveCamera();
    if (!camera || engine.getState(Toggle::CameraMovement)) // the free camera (Settings) takes over
      return;
    const auto &transform = engine.get<Transform>(player);
    const glm::vec3 ahead = forward(transform);
    camera->lookAt(transform.position - ahead * 7.0f + glm::vec3(0.0f, 4.0f, 0.0f),
                   transform.position + ahead * 2.0f);
  }

  // Where a transform faces: -Z turned by its yaw
  static glm::vec3 forward(const Transform &transform) {
    const float yaw = glm::radians(transform.rotation.y);
    return {-std::sin(yaw), 0.0f, -std::cos(yaw)};
  }

public:
  void setup(Engine &engine) override {
    engine.setWindowTitle("MyGame");
    engine.setState(Toggle::CameraMovement, false); // the game drives the camera
    engine.loadScene(LEVEL);                         // its entities arrive in onSceneLoaded
  }

  void onSceneLoaded(Engine &engine, const std::string &) override {
    spawnPlayer(engine);
    spawnCoins(engine);
  }

  void update(Engine &engine, float deltaTime) override {
    if (!engine.isAlive(player))
      return; // the level is still loading
    updatePlayer(engine, deltaTime);
    updateCoins(engine, deltaTime);
    updateCamera(engine);
  }
};

int main(int argc, char **argv) {
  // --screenshot, --benchmark, --deterministic and --save-scene work in every game
  const Engine::CommandLine commandLine = Engine::parseCommandLine(argc, argv);

  Engine engine;
  if (!engine.initialize())
    return 1;
  engine.applyCommandLine(commandLine);

  MyGame game;
  engine.run(game);
  return 0;
}
