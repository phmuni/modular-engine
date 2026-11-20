// MyGame: a starting point for a game on modular-engine. Walk with W A S D (or a gamepad's left stick), jump with
// Space (or its bottom button), push the crates around and pick up the gold spheres.
//
// The level is a scene file, assets/scenes/level.json: press F2 for the editor, move things around (click to select,
// W/E/R/T for the gizmos) and save it from the Settings panel. The code below builds what the level file does not
// have (the player, the coins) once the level has loaded.

#include "components/camera.h"
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
  float jumpSpeed = 6.0f;   // units per second, upward
};
struct Coin {
  float spin = 120.0f; // degrees per second
};

class MyGame : public App {
  static constexpr const char *LEVEL = "../assets/scenes/level.json";
  static constexpr glm::vec3 GOLD{1.0f, 0.78f, 0.25f};
  static constexpr glm::vec3 START{0.0f, 0.6f, 3.0f};

  Entity player = -1;
  int coinsLeft = 0;

  void spawnPlayer(Engine &engine) {
    player = engine.entities()
                 .create("Player")
                 .withModel(EngineConfig::MODEL_BOX, START, glm::vec3(0.0f), glm::vec3(0.8f))
                 .withSolidColor(glm::vec3(0.2f, 0.55f, 1.0f))
                 .withCharacter(0.4f, 0.8f) // physics: walks on the level, pushes the crates
                 .withModelCollision()       // and a box for picking up coins (forEachCollision below)
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

    // Actions and axes, not keys: the bindings (setup, assets/input.json) decide what drives them
    const float turn = -input.getAxis("moveX");
    const float drive = input.getAxis("moveY");

    // Turning is ours; walking goes through the character controller, which keeps it on the ground and out of walls
    transform.rotation.y += turn * settings.turnSpeed * deltaTime;
    auto &character = engine.get<CharacterController>(player);
    character.move = forward(transform) * drive * settings.speed;
    if (input.wasActionPressed("jump") && character.onGround)
      character.jump = settings.jumpSpeed;

    // Fell off the level: back to the start (moving the Transform moves the character there)
    if (transform.position.y < -10.0f)
      transform.position = START;
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
    engine.setGameName("MyGame");  // the folder the saved data (engine.saveData()) is kept in
    engine.enablePauseMenu(true); // Escape: Resume, Options (volumes, fullscreen, vsync) and Quit

    // Default bindings, keyboard and gamepad; assets/input.json (if there) replaces them, no rebuild needed
    auto &input = engine.input();
    input.bindAxis("moveX", {InputBinding::keys(SDL_SCANCODE_A, SDL_SCANCODE_D),
                             InputBinding::keys(SDL_SCANCODE_LEFT, SDL_SCANCODE_RIGHT),
                             InputBinding::gamepad(SDL_GAMEPAD_AXIS_LEFTX)});
    input.bindAxis("moveY", {InputBinding::keys(SDL_SCANCODE_S, SDL_SCANCODE_W),
                             InputBinding::keys(SDL_SCANCODE_DOWN, SDL_SCANCODE_UP),
                             InputBinding::gamepad(SDL_GAMEPAD_AXIS_LEFTY, -1.0f)}); // stick up is negative
    input.bindAction("jump", {InputBinding::keyboard(SDL_SCANCODE_SPACE), InputBinding::gamepad(SDL_GAMEPAD_BUTTON_SOUTH)});
    input.loadBindings("../assets/input.json");

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

  // Registered components are saved in scene files and editable in the Properties panel (F2 shows the editor)
  engine.registerComponent<Player>("Player")
      .field("speed", &Player::speed)
      .field("turnSpeed", &Player::turnSpeed)
      .field("jumpSpeed", &Player::jumpSpeed);

  MyGame game;
  engine.run(game);
  return 0;
}
