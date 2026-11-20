// MyGame: a starting point for a game on modular-engine in C++ and Lua. Walk with W A S D (or a gamepad's left
// stick), jump with Space (or its bottom button), push the crates around and pick up the gold coins.
//
// C++ does the player (below); Lua does the coins, copies of the prefab assets/prefabs/coin.json, and the score
// (assets/scripts/coin.lua and game.lua), and calls back into C++ through celebrate(), which setup() hands to the
// scripts. Scripts reload when saved, no rebuild needed. Both languages use the same API: the entity handle below
// (player.move, player.setEmission...) is the `Entity` a script sees, engine.camera() is Lua's `camera`, and the
// paths are relative to assets/ in both.
//
// The level is a scene file, assets/scenes/level.json (its sky color too): press F2 for the editor, move things around
// (click to select, W/E/R/T for the gizmos) and save it from the Settings panel.

#include "foundation/core/engine.h"
#include "foundation/core/logger.h"
#include "systems/inputSystem.h"
#include "systems/scriptSystem.h"
#include <glm/gtc/constants.hpp>
#include <sol/sol.hpp> // to hand C++ functions to Lua scripts

// Game components: plain structs
struct Player {
  float speed = 6.0f;       // units per second
  float turnSpeed = 160.0f; // degrees per second
  float jumpSpeed = 6.0f;   // units per second, upward
};

class MyGame : public App {
  static constexpr const char *LEVEL = "scenes/level.json";
  static constexpr glm::vec3 START{0.0f, 0.6f, 3.0f};

  GameEntity player;
  float celebration = 0.0f; // seconds of glowing left

  void spawnPlayer(Engine &engine) {
    // The Lua template's level file holds its player; here the code makes it with engine.create, from a description of
    // what it has (Lua's engine.create takes the same as a table). The character controller walks on the level and
    // pushes the crates; the collision box is what the coins notice it with (coin.lua's onOverlap); "Player" is the game
    // component registered in main.
    EntityDesc desc;
    desc.model = "box";
    desc.position = START;
    desc.scale = glm::vec3(0.8f);
    desc.color = glm::vec3(0.2f, 0.55f, 1.0f);
    desc.character = CharacterController();
    desc.character->radius = 0.4f;
    desc.character->height = 0.8f;
    desc.collision = true;
    desc.components = {"Player"};
    player = engine.create("Player", desc);

    // The camera trails it from behind and above, looking a little ahead. The engine moves it after the physics and
    // the game's update, so it does not jitter; a bit of smoothing keeps it calm when the player turns. (Lua:
    // camera.follow(self, { offset = vec3(0, 4, 7), lookAhead = 2, smoothing = 0.1 }) in player.lua.)
    CameraRig rig;
    rig.offset = glm::vec3(0.0f, 4.0f, 7.0f);
    rig.lookAhead = 2.0f;
    rig.positionSmoothing = rig.rotationSmoothing = 0.1f;
    engine.camera().follow(player, rig);
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

  // The player glows for a moment after celebrate() (called from game.lua)
  void updateCelebration(float deltaTime) {
    if (celebration <= 0.0f)
      return;
    celebration -= deltaTime;
    player.setEmission(glm::vec3(1.0f, 0.8f, 0.3f), celebration > 0.0f ? 3.0f * celebration : 0.0f);
  }

public:
  void setup(Engine &engine) override {
    engine.setWindowTitle("MyGame");
    engine.setGameName("MyGame"); // the folder the saved data (save.get and save.set in Lua) is kept in

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

    // A C++ function for the scripts: game.lua calls celebrate() when every coin is picked up
    engine.scripts().lua()["celebrate"] = [this] { celebration = 1.5f; };

    engine.setState(Toggle::FreeCamera, false); // the game drives the camera
    engine.loadScene(LEVEL);                         // its entities arrive in onSceneLoaded
  }

  void onSceneLoaded(Engine &engine, const std::string &) override {
    spawnPlayer(engine);
    // The game script, once the level is in: the HUD and the score (ui.pauseMenu and the best score in save.get
    // too), and the coins it spawns from the prefab
    engine.scripts().runGameScript("scripts/game.lua");
  }

  void update(Engine &engine, float deltaTime) override {
    if (!player.valid())
      return; // the level is still loading
    updatePlayer(engine, deltaTime);
    updateCelebration(deltaTime);
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
