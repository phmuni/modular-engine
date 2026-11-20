// MyGame: a starting point for a game on modular-engine. Walk with W A S D (or a gamepad's left stick), jump with
// Space (or its bottom button), push the crates around and pick up the gold coins.
//
// This is the same game as the Lua project template (assets/scripts/game.lua, player.lua and coin.lua), written
// against the same API: entity handles (position, move, jump...), engine.ui(), engine.camera(), engine.spawn(),
// engine.saveData(), paths relative to assets/. What differs is C++'s way of organizing it, and the comments say where:
// game state lives in components and members instead of the `game` table and `self`, and a system loops over every
// component of a kind instead of each entity running its own onUpdate.
//
// The level is a scene file, assets/scenes/level.json (its sky color too): press F2 for the editor, move things around
// (click to select, W/E/R/T for the gizmos) and save it from the Settings panel. The code below adds what the level
// file does not have once the level has loaded: the player, and the coins, copies of the prefab
// assets/prefabs/coin.json (a prefab is an entity saved to a file: in the editor, select a coin, change it and use Save
// as prefab).

#include "foundation/core/config.h"
#include "foundation/core/engine.h"
#include "foundation/core/logger.h"
#include "systems/inputSystem.h"
#include "systems/saveData.h"
#include <cmath>
#include <glm/gtc/constants.hpp>
#include <vector>

// Game components: plain structs. The editor shows the registered ones (see main), like a Lua script's `properties`.
struct Player {
  float speed = 6.0f;       // units per second
  float turnSpeed = 160.0f; // degrees per second
  float jumpSpeed = 6.0f;   // units per second, upward
};
struct Coin {
  float spin = 120.0f; // degrees per second
  float bob = 0.15f;   // how far it moves up and down, in units
  float baseY = 0.0f;  // where it bobs around and how long it has: not registered, so not saved in the prefab
  float age = 0.0f;
};

class MyGame : public App {
  static constexpr const char *LEVEL = "scenes/level.json";
  static constexpr const char *COIN = "prefabs/coin.json";
  static constexpr int COINS = 8;
  static constexpr float RING_RADIUS = 7.0f;
  static constexpr glm::vec3 START{0.0f, 0.6f, 3.0f};

  GameEntity player;
  // Lua keeps these in the shared `game` table
  int score = 0;
  int best = 0;
  int coinsLeft = 0;

  void spawnPlayer(Engine &engine) {
    // The Lua template's level file holds its player; here the code makes it, with the builder (the C++ way to add
    // components, including the game's own)
    player = engine.entity(engine.entities()
                               .create("Player")
                               .withModel(EngineConfig::MODEL_BOX, START, glm::vec3(0.0f), glm::vec3(0.8f))
                               .withSolidColor(glm::vec3(0.2f, 0.55f, 1.0f))
                               .withCharacter(0.4f, 0.8f) // physics: walks on the level, pushes the crates
                               .withModelCollision()       // and a box for picking up coins (forEachCollision below)
                               .withComponent<Player>()
                               .build());

    // The camera trails it from behind and above, looking a little ahead. The engine moves it after the physics and
    // the game's update, so it does not jitter; a bit of smoothing keeps it calm when the player turns. (Lua:
    // camera.follow(self, { offset = vec3(0, 4, 7), lookAhead = 2, smoothing = 0.1 }) in player.lua.)
    CameraRig rig;
    rig.offset = glm::vec3(0.0f, 4.0f, 7.0f);
    rig.lookAhead = 2.0f;
    rig.positionSmoothing = rig.rotationSmoothing = 0.1f;
    engine.camera().follow(player, rig);
  }

  // Every coin is a copy of prefabs/coin.json, so changing that file changes them all
  void spawnCoins(Engine &engine) {
    for (int i = 0; i < COINS; ++i) {
      const float angle = glm::two_pi<float>() * static_cast<float>(i) / COINS;
      const glm::vec3 position(std::cos(angle) * RING_RADIUS, 0.8f, std::sin(angle) * RING_RADIUS);
      const GameEntity coin = engine.spawn(COIN, position); // the prefab's Coin component comes with it
      if (!coin)
        return; // spawn logged why
      Coin &state = engine.get<Coin>(coin.id()); // Lua sets these on `self` in the coin's onStart
      state.baseY = position.y;
      state.age = position.x + position.z; // so the coins of the ring do not bob in step
    }
    coinsLeft = COINS;
  }

  void collect(Engine &engine) {
    GameUI &ui = engine.ui();
    ++score;
    --coinsLeft;
    ui.text("score", score);
    if (score > best) {
      best = score;
      engine.saveData().set("bestScore", best); // written when the game closes; saveData().write() saves it right now
      ui.text("best", best);
    }
    if (coinsLeft == 0) {
      LOG_I("[MyGame] All coins collected. Here come more.");
      spawnCoins(engine);
    }
  }

  void updatePlayer(Engine &engine, float deltaTime) {
    auto &input = engine.input();
    const Player &settings = engine.get<Player>(player.id());

    // Turning is ours; walking goes through the character controller, which keeps it on the ground and out of walls.
    // Actions and axes, not keys: the bindings (setup, assets/input.json) decide what drives them.
    glm::vec3 rotation = player.rotation();
    rotation.y -= input.getAxis("moveX") * settings.turnSpeed * deltaTime;
    player.setRotation(rotation);
    player.move(player.forward() * input.getAxis("moveY") * settings.speed);
    if (input.wasActionPressed("jump") && player.onGround())
      player.jump(settings.jumpSpeed);

    // Fell off the level: back to the start
    if (player.position().y < -10.0f)
      player.setPosition(START);
  }

  void updateCoins(Engine &engine, float deltaTime) {
    // One loop for every coin: where Lua gives each coin its own onUpdate. The prefab stands each disc on its edge
    // (rotation.x = 90), so turning around Y spins it like a coin on a table.
    engine.getComponentManager().forEachComponent<Coin>([&](Entity id, Coin &settings) {
      GameEntity coin = engine.entity(id);
      settings.age += deltaTime;
      glm::vec3 rotation = coin.rotation();
      rotation.y += settings.spin * deltaTime;
      coin.setRotation(rotation);
      glm::vec3 position = coin.position();
      position.y = settings.baseY + std::sin(settings.age * 3.0f) * settings.bob;
      coin.setPosition(position);
    });

    // A coin's collision box overlaps the player's (Lua: the coin's onOverlap). Collect first, then destroy: not
    // while iterating.
    std::vector<GameEntity> collected;
    engine.forEachCollision<Player, Coin>(
        [&](Entity, Player &, Entity coin, Coin &) { collected.push_back(engine.entity(coin)); });
    for (GameEntity coin : collected) {
      coin.destroy();
      collect(engine);
    }
  }

public:
  void setup(Engine &engine) override {
    engine.setWindowTitle("MyGame");
    engine.setGameName("MyGame");  // the folder the saved data (engine.saveData()) is kept in
    engine.ui().pauseMenu(true);   // Escape: Resume, Options (volumes, fullscreen, vsync) and Quit

    // Default bindings, keyboard and gamepad; assets/input.json (if there) replaces them, no rebuild needed
    auto &input = engine.input();
    input.bindAxis("moveX", {InputBinding::keys(SDL_SCANCODE_A, SDL_SCANCODE_D),
                             InputBinding::keys(SDL_SCANCODE_LEFT, SDL_SCANCODE_RIGHT),
                             InputBinding::gamepad(SDL_GAMEPAD_AXIS_LEFTX)});
    input.bindAxis("moveY", {InputBinding::keys(SDL_SCANCODE_S, SDL_SCANCODE_W),
                             InputBinding::keys(SDL_SCANCODE_DOWN, SDL_SCANCODE_UP),
                             InputBinding::gamepad(SDL_GAMEPAD_AXIS_LEFTY, -1.0f)}); // stick up is negative
    input.bindAction("jump", {InputBinding::keyboard(SDL_SCANCODE_SPACE), InputBinding::gamepad(SDL_GAMEPAD_BUTTON_SOUTH)});
    input.loadBindings("input.json");

    // The score on screen, and the best score kept between runs: still there the next time the game runs
    best = static_cast<int>(engine.saveData().getNumber("bestScore", 0));
    engine.ui().load("ui/hud.rml");
    engine.ui().text("score", score);
    engine.ui().text("best", best);

    engine.setState(Toggle::CameraMovement, false); // the game drives the camera
    engine.loadScene(LEVEL);                         // its entities arrive in onSceneLoaded
  }

  void onSceneLoaded(Engine &engine, const std::string &) override {
    spawnPlayer(engine);
    spawnCoins(engine);
  }

  void update(Engine &engine, float deltaTime) override {
    if (!player.valid())
      return; // the level is still loading
    updatePlayer(engine, deltaTime);
    updateCoins(engine, deltaTime);
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
  // The coin prefab keeps these two
  engine.registerComponent<Coin>("Coin").field("spin", &Coin::spin).field("bob", &Coin::bob);

  MyGame game;
  engine.run(game);
  return 0;
}
