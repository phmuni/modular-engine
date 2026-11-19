#pragma once
// Space Aliens example game built on top of the engine.
//
// A chase camera follows the ship from behind. The ship moves up, down and sideways; waves of aliens fly in from
// the far end into formations at different heights, sway there, shoot at the ship and now and then dive at it.

#include "components/camera.h"
#include "components/collision.h"
#include "components/particleEmitter.h"
#include "components/transform.h"
#include "foundation/core/config.h"
#include "foundation/core/engine.h"
#include "foundation/core/logger.h"
#include "foundation/core/random.h"
#include "game/components.h"
#include "systems/inputSystem.h"
#include "systems/stateSystem.h"
#include <RmlUi/Core.h>
#include <array>
#include <cmath>
#include <glm/gtc/constants.hpp>
#include <random>
#include <unordered_set>
#include <vector>

class SpaceAliens : public App {
  Entity player = -1;
  Entity gameManager = -1;
  std::array<Entity, 4> reticle{-1, -1, -1, -1}; // shows where the shots go, ahead of the ship

  std::mt19937 &rng = Random::generator();
  std::uniform_real_distribution<float> pitchJitter{0.92f, 1.08f};

  static constexpr const char *SOUND_LASER = "../assets/sounds/laser.wav";
  static constexpr const char *SOUND_ENEMY_SHOT = "../assets/sounds/enemy_shot.wav";
  static constexpr const char *SOUND_EXPLOSION = "../assets/sounds/explosion.wav";
  static constexpr const char *SOUND_PLAYER_HIT = "../assets/sounds/player_hit.wav";
  static constexpr const char *MUSIC = "../assets/sounds/music.wav";
  static constexpr const char *SOUND_UI_HOVER = "../assets/sounds/ui_hover.wav";
  static constexpr const char *SOUND_UI_CLICK = "../assets/sounds/ui_click.wav";
  static constexpr const char *MODEL_ALIEN = "../assets/models/alien/alien.glb";
  static constexpr const char *MODEL_SHIP = "../assets/models/spaceship/spaceship.obj";
  static constexpr const char *SKYBOX = "../assets/textures/skybox/space.png";

  // The arena, along -Z away from the camera
  static constexpr float PLAYER_Z = 6.0f;
  static constexpr float FORMATION_Z = -22.0f; // the formation's center
  static constexpr float SPAWN_Z = -110.0f;    // where aliens enter from
  static constexpr glm::vec3 PLAYER_START{0.0f, 4.0f, PLAYER_Z};
  static constexpr glm::vec3 ALIEN_SCALE{1.5f, 1.5f, 1.5f};
  static constexpr glm::vec3 ALIEN_ROTATION{30.0f, 0.0f, 0.0f}; // the dome tipped toward the player
  static constexpr int WAVE_COUNT = 4;
  // The saucer's box, tilted, has empty corners; trimmed a little so shots that look like misses miss
  static constexpr float ALIEN_FIT = 0.92f;
  static constexpr glm::vec3 RETICLE_COLOR{0.3f, 1.0f, 0.85f};

  // What the HUD document (assets/ui/aliens/hud.rml) shows, bound by name in createHud
  struct Hud {
    int score = 0;
    int lives = 3;
    int wave = 1;
    int remaining = 0;
    float progress = 100.0f; // invaders left, in percent
    bool bump = false;       // briefly true when the score goes up
    bool hintHidden = false;
    bool paused = false;
    bool gameOver = false;
    bool won = false;
    int restartIn = 0;
  } hud;
  Rml::DataModelHandle hudModel;
  Rml::ElementDocument *hudDocument = nullptr;
  // Set when a scene file replaced the game's entities (Engine::loadScene, e.g. from the Settings panel)
  bool sceneReplaced = false;
  float bumpTimer = 0.0f;
  float hintTimer = 6.0f;
  bool restartRequested = false;
  Engine *engine = nullptr;
  int totalInvaders = 0;
  glm::vec3 cameraPosition = PLAYER_START + glm::vec3(0.0f, 3.0f, 9.0f);
  glm::vec3 cameraTarget = PLAYER_START + glm::vec3(0.0f, 0.0f, -20.0f);

  // ---- Waves -------------------------------------------------------------------------------------------------

  struct Slot {
    glm::vec3 offset; // from the formation's center; z > 0 is nearer the player
    int row;          // 0 = the front row, worth the least
  };

  // Each wave's formation, centered on (0, 0); heights are added to the formation center's
  static std::vector<Slot> formation(int wave) {
    std::vector<Slot> slots;
    switch (wave) {
    case 0: // a wall of three rows at different heights, the back rows further away
      for (int row = 0; row < 3; ++row)
        for (int col = 0; col < 6; ++col)
          slots.push_back({{(col - 2.5f) * 3.2f, (row - 1.0f) * 2.6f, -row * 3.0f}, row});
      break;
    case 1: // a V pointing at the player, rising at the wings
      for (int i = -5; i <= 5; ++i) {
        const float side = static_cast<float>(std::abs(i));
        slots.push_back({{i * 1.6f, -2.5f + side * 1.0f, -side * 1.6f}, std::abs(i) / 2});
      }
      break;
    case 2: // a ring around a pair at its center
      for (int i = 0; i < 12; ++i) {
        const float angle = glm::two_pi<float>() * static_cast<float>(i) / 12.0f;
        slots.push_back({{std::cos(angle) * 7.5f, std::sin(angle) * 3.4f, std::sin(angle * 2.0f) * 2.0f}, 1});
      }
      slots.push_back({{-1.3f, 0.0f, -2.0f}, 2});
      slots.push_back({{1.3f, 0.0f, -2.0f}, 2});
      break;
    default: // two crossing diagonals, deep
      for (int i = 0; i < 7; ++i) {
        const float t = static_cast<float>(i) - 3.0f;
        slots.push_back({{t * 2.6f, t * 1.1f, -std::abs(t) * 2.0f - 1.0f}, i % 3});
        if (i != 3)
          slots.push_back({{t * 2.6f, -t * 1.1f, -std::abs(t) * 2.0f - 1.0f}, (i + 1) % 3});
      }
      break;
    }
    return slots;
  }

  static glm::vec3 rowColor(int row) {
    static const glm::vec3 colors[] = {{0.2f, 0.85f, 0.25f}, {0.95f, 0.75f, 0.1f}, {1.0f, 0.15f, 0.12f}};
    return colors[glm::clamp(row, 0, 2)];
  }

  // Later waves dive and shoot more often
  static float waveDiveInterval(int wave) { return glm::max(3.6f - wave * 0.6f, 1.2f); }
  static float waveShootInterval(int wave) { return glm::max(1.5f - wave * 0.25f, 0.55f); }

  // The formation sways around (0, FORMATION_Y, FORMATION_Z) by up to FORMATION_SWAY
  static constexpr float FORMATION_Y = 5.0f;
  static constexpr glm::vec3 FORMATION_SWAY{2.5f, 1.0f, 2.0f};
  glm::vec3 formationCenter(float time) const {
    return glm::vec3(0.0f, FORMATION_Y, FORMATION_Z) +
           FORMATION_SWAY * glm::vec3(std::sin(time * 0.45f), std::sin(time * 0.8f), std::sin(time * 0.3f));
  }

  // Every alien must be reachable: in front of some position the ship can fly to, even at the far end of the sway.
  // Measured at the middle of its saucer, which sits about half a unit above its origin.
  static void checkFormationsReachable() {
    const Player ship;
    const glm::vec2 middle(0.0f, FORMATION_Y + 0.5f);
    for (int wave = 0; wave < WAVE_COUNT; ++wave)
      for (const Slot &slot : formation(wave)) {
        const glm::vec2 lowest = glm::vec2(slot.offset) + middle - glm::vec2(FORMATION_SWAY);
        const glm::vec2 highest = glm::vec2(slot.offset) + middle + glm::vec2(FORMATION_SWAY);
        if (glm::any(glm::lessThan(lowest, ship.boundsMin)) || glm::any(glm::greaterThan(highest, ship.boundsMax)))
          LOG_W("[SpaceAliens] Wave %d has an alien at (%.1f, %.1f) the ship cannot always reach", wave + 1,
                slot.offset.x, slot.offset.y);
      }
  }

  void spawnWave(Engine &engine, GameState &state) {
    const std::vector<Slot> slots = formation(state.wave);
    std::uniform_real_distribution<float> side(-1.0f, 1.0f);
    for (size_t i = 0; i < slots.size(); ++i) {
      const Slot &slot = slots[i];
      Invader invader;
      invader.color = rowColor(slot.row);
      invader.scoreValue = (slot.row + 1) * 10 * (state.wave + 1);
      invader.slot = slot.offset;
      invader.delay = static_cast<float>(i) * 0.12f;
      invader.start = glm::vec3(slot.offset.x * 2.5f + side(rng) * 6.0f, 4.5f + side(rng) * 6.0f, SPAWN_Z);
      invader.curve = glm::vec3(side(rng) * 18.0f, side(rng) * 8.0f, 0.0f);
      engine.entities()
          .create("Invader")
          .withModel(MODEL_ALIEN, invader.start, ALIEN_ROTATION, ALIEN_SCALE)
          .withModelCollision(ALIEN_FIT)
          .withSolidColor(invader.color)
          .withComponent<Invader>(invader)
          .build();
    }
    state.diveTimer = waveDiveInterval(state.wave) + 2.0f; // time to settle in first
    state.enemyShootTimer = 2.0f;
  }

  // ---- Entities ----------------------------------------------------------------------------------------------

  void createPlayer(Engine &engine) {
    constexpr glm::vec3 scale{1.4f, 0.6f, 1.4f};
    player = engine.entities()
                 .create("Player")
                 .withModel(MODEL_SHIP, PLAYER_START, glm::vec3(0.0f), scale)
                 .withModelCollision(0.6f) // smaller than the ship: grazing shots miss
                 .withComponent<Player>()
                 .withSolidColor(glm::vec3(0.8f, 0.8f, 0.8f))
                 .build();
  }

  void createReticle(Engine &engine) {
    for (Entity &dot : reticle)
      dot = engine.entities()
                .create("Reticle")
                .withModel(EngineConfig::MODEL_BOX, glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 45.0f),
                           glm::vec3(0.32f, 0.32f, 0.02f))
                .withEmission(RETICLE_COLOR, 3.0f)
                .build();
  }

  // Streaks flying past the camera, which sell the speed against the still sky
  void createStarfield(Engine &engine) {
    Entity starfield = engine.entities()
                           .create("Starfield")
                           .withTransform(glm::vec3(0.0f, 4.0f, -90.0f))
                           .withComponent<ParticleEmitter>()
                           .build();
    auto &p = engine.get<ParticleEmitter>(starfield);
    p.emissionShape = EmissionShape::Box;
    p.boxHalfExtents = {40.0f, 24.0f, 2.0f};
    p.emitRate = 140.0f;
    p.particleLifetime = 3.6f;
    p.speed = 30.0f;
    p.speedVariance = 8.0f;
    p.size = 0.07f;
    p.sizeDecay = 0.0f;
    p.gravity = {0.0f, 0.0f, 0.0f};
    p.startColor = {0.85f, 0.85f, 1.0f, 0.8f};
    p.endColor = {0.85f, 0.85f, 1.0f, 0.0f};
    p.emitDirection = {0.0f, 0.0f, 1.0f};
    p.spread = 0.0f;
    p.maxParticles = 700;
    p.additiveBlending = true;
  }

  void createGameManager(Engine &engine) {
    gameManager = engine.entities().create("GameManager").withComponent<GameState>().build();
  }

  void createExplosion(Engine &engine, const glm::vec3 &position, const glm::vec3 &color, float scale = 1.0f) {
    Entity expl = engine.entities()
                      .create("Explosion")
                      .withTransform(position)
                      .withComponent<ParticleEmitter>()
                      .withComponent<Explosion>(0.1f, 0.55f * scale)
                      .build();
    auto &p = engine.get<ParticleEmitter>(expl);
    p.emissionShape = EmissionShape::Sphere;
    p.emitRate = 350.0f;
    p.particleLifetime = 0.55f * scale;
    p.speed = 5.5f * scale;
    p.speedVariance = 3.0f * scale;
    p.size = 0.35f * scale;
    p.sizeDecay = 0.9f;
    p.gravity = {0.0f, 0.0f, 0.0f};
    p.startColor = {color.r, color.g, color.b, 1.0f};
    p.endColor = {color.r * 0.3f, color.g * 0.3f, color.b * 0.3f, 0.0f};
    p.emitDirection = {0.0f, 0.0f, 1.0f};
    p.spread = 1.0f;
    p.maxParticles = 80;
    p.additiveBlending = true;
  }

  void spawnBullet(Engine &engine, const glm::vec3 &pos, const glm::vec3 &vel, bool friendly) {
    const glm::vec3 scale = friendly ? glm::vec3(0.12f, 0.12f, 2.0f) : glm::vec3(0.3f, 0.3f, 0.9f);
    // Pointed along its flight
    const glm::vec3 dir = glm::normalize(vel);
    const glm::vec3 rotation(glm::degrees(std::asin(dir.y)), glm::degrees(std::atan2(-dir.x, -dir.z)), 0.0f);
    auto builder = engine.entities()
                       .create(friendly ? "PlayerBullet" : "EnemyBullet")
                       .withModel(EngineConfig::MODEL_BOX, pos, rotation, scale)
                       .withEmission(friendly ? glm::vec3(0.15f, 1.0f, 0.75f) : glm::vec3(1.0f, 0.18f, 0.08f), 7.0f)
                       .withComponent<Bullet>(vel, friendly ? 1.8f : 6.0f, friendly);
    // The player's shots are fast and thin: they test the path they flew each frame (updateCollisions) instead of
    // a box, so none passes through an alien between two frames. The aliens' slow shots use a box.
    if (!friendly)
      builder.withCollision(glm::vec3(0.45f));
    builder.build();
  }

  // ---- HUD ---------------------------------------------------------------------------------------------------

  void createHud(Engine &engine) {
    Rml::Context *context = engine.getUIContext();
    if (!context)
      return;
    Rml::DataModelConstructor model = context->CreateDataModel("hud");
    if (!model)
      return;
    model.Bind("score", &hud.score);
    model.Bind("lives", &hud.lives);
    model.Bind("wave", &hud.wave);
    model.Bind("remaining", &hud.remaining);
    model.Bind("progress", &hud.progress);
    model.Bind("bump", &hud.bump);
    model.Bind("hint_hidden", &hud.hintHidden);
    model.Bind("paused", &hud.paused);
    model.Bind("game_over", &hud.gameOver);
    model.Bind("won", &hud.won);
    model.Bind("restart_in", &hud.restartIn);
    model.BindEventCallback("restart", [this](Rml::DataModelHandle, Rml::Event &event, const Rml::VariantList &) {
      restartRequested = true;
      // Drop focus, so the next Space press fires instead of going to the button
      if (Rml::Element *button = event.GetCurrentElement())
        button->Blur();
    });
    model.BindEventCallback("hover", [this](Rml::DataModelHandle, Rml::Event &, const Rml::VariantList &) {
      this->engine->playSound(SOUND_UI_HOVER, 0.35f);
    });
    hudModel = model.GetModelHandle();

    hudDocument = engine.loadUIDocument("../assets/ui/aliens/hud.rml");
  }

  // Copies the game state into the HUD, marking only what changed
  template <typename T> void setHud(T &field, T value, const char *name) {
    if (field == value)
      return;
    field = value;
    if (hudModel)
      hudModel.DirtyVariable(name);
  }

  int aliveInvaders(Engine &engine) {
    int alive = 0;
    engine.getComponentManager().forEachComponent<Invader>([&](Entity, Invader &inv) { alive += inv.alive ? 1 : 0; });
    return alive;
  }

  void updateHud(Engine &engine, float deltaTime) {
    auto &state = engine.get<GameState>(gameManager);

    // This wave's survivors plus the waves still to come
    int remaining = aliveInvaders(engine);
    for (int wave = state.wave + (state.nextWaveTimer > 0.0f ? 0 : 1); wave < WAVE_COUNT; ++wave)
      remaining += static_cast<int>(formation(wave).size());

    if (state.score > hud.score)
      bumpTimer = 0.08f;
    bumpTimer -= deltaTime;
    hintTimer -= deltaTime;

    setHud(hud.score, state.score, "score");
    setHud(hud.lives, glm::max(state.lives, 0), "lives");
    setHud(hud.wave, glm::min(state.wave + 1, WAVE_COUNT), "wave");
    setHud(hud.remaining, remaining, "remaining");
    setHud(hud.progress, totalInvaders > 0 ? 100.0f * remaining / static_cast<float>(totalInvaders) : 0.0f,
           "progress");
    setHud(hud.bump, bumpTimer > 0.0f, "bump");
    setHud(hud.hintHidden, hintTimer <= 0.0f, "hint_hidden");
    setHud(hud.paused, engine.getSystemManager().getSystem<StateSystem>().isToggled(Toggle::Paused), "paused");
    setHud(hud.gameOver, state.gameOver, "game_over");
    setHud(hud.won, state.playerWon, "won");
    setHud(hud.restartIn, static_cast<int>(std::ceil(glm::max(state.restartTimer, 0.0f))), "restart_in");
  }

  // ---- Game --------------------------------------------------------------------------------------------------

  void resetGame(Engine &engine) {
    auto &cm = engine.getComponentManager();

    std::vector<Entity> toDestroy;
    cm.forEachComponent<Invader>([&](Entity e, Invader &) { toDestroy.push_back(e); });
    cm.forEachComponent<Bullet>([&](Entity e, Bullet &) { toDestroy.push_back(e); });
    cm.forEachComponent<Explosion>([&](Entity e, Explosion &) { toDestroy.push_back(e); });
    for (Entity e : toDestroy)
      engine.destroyEntity(e);

    engine.get<Player>(player) = Player{};
    auto &tf = engine.get<Transform>(player);
    tf.position = PLAYER_START;
    tf.rotation = glm::vec3(0.0f);
    engine.setEmission(player, glm::vec3(1.0f, 0.90f, 0.20f), 0.0f);

    engine.get<GameState>(gameManager) = GameState{};
  }

  void updatePlayer(Engine &engine, float deltaTime) {
    auto &input = engine.input();
    auto &tf = engine.get<Transform>(player);
    auto &ship = engine.get<Player>(player);

    glm::vec2 axis(0.0f);
    if (input.isKeyDown(SDL_SCANCODE_A) || input.isKeyDown(SDL_SCANCODE_LEFT))
      axis.x -= 1.0f;
    if (input.isKeyDown(SDL_SCANCODE_D) || input.isKeyDown(SDL_SCANCODE_RIGHT))
      axis.x += 1.0f;
    if (input.isKeyDown(SDL_SCANCODE_W) || input.isKeyDown(SDL_SCANCODE_UP))
      axis.y += 1.0f;
    if (input.isKeyDown(SDL_SCANCODE_S) || input.isKeyDown(SDL_SCANCODE_DOWN))
      axis.y -= 1.0f;

    // Accelerates along held axes, glides to a stop on the others
    for (int i = 0; i < 2; ++i) {
      if (axis[i] != 0.0f)
        ship.velocity[i] += axis[i] * ship.acceleration * deltaTime;
      else
        ship.velocity[i] = glm::mix(ship.velocity[i], 0.0f, glm::min(ship.friction * deltaTime, 1.0f));
    }
    if (glm::length(ship.velocity) > ship.maxSpeed)
      ship.velocity = glm::normalize(ship.velocity) * ship.maxSpeed;

    glm::vec2 position = glm::vec2(tf.position) + ship.velocity * deltaTime;
    for (int i = 0; i < 2; ++i) {
      if (position[i] < ship.boundsMin[i] || position[i] > ship.boundsMax[i]) {
        position[i] = glm::clamp(position[i], ship.boundsMin[i], ship.boundsMax[i]);
        ship.velocity[i] = 0.0f;
      }
    }
    tf.position = glm::vec3(position, PLAYER_Z);

    // Banks into turns and noses up or down with the climb
    const float blend = glm::min(8.0f * deltaTime, 1.0f);
    ship.roll = glm::mix(ship.roll, -ship.velocity.x / ship.maxSpeed * 35.0f, blend);
    ship.pitch = glm::mix(ship.pitch, ship.velocity.y / ship.maxSpeed * 15.0f, blend);
    tf.rotation = glm::vec3(ship.pitch, 0.0f, ship.roll);

    ship.shootTimer -= deltaTime;
    if (input.isKeyDown(SDL_SCANCODE_SPACE) && ship.shootTimer <= 0.0f) {
      const float side = ship.leftCannon ? -0.55f : 0.55f;
      ship.leftCannon = !ship.leftCannon;
      // Both cannons angle in to meet at the reticle
      const glm::vec3 muzzle = tf.position + glm::vec3(side, 0.0f, -1.5f);
      spawnBullet(engine, muzzle, glm::normalize(aimPoint(engine) - muzzle) * ship.bulletSpeed, true);
      engine.playSound(SOUND_LASER, 0.3f, pitchJitter(rng));
      ship.shootTimer = ship.shootCooldown;
    }

    if (ship.hitCooldown > 0.0f)
      ship.hitCooldown -= deltaTime;
  }

  // Behind and above the ship, following it part of the way so the ship also moves across the screen
  void updateCamera(Engine &engine, float deltaTime, bool snap = false) {
    Camera *camera = engine.getActiveCamera();
    if (!camera)
      return;
    const auto &tf = engine.get<Transform>(player);
    const auto &ship = engine.get<Player>(player);
    const glm::vec3 eye(tf.position.x * 0.6f, 3.6f + tf.position.y * 0.75f, PLAYER_Z + 8.5f);
    const glm::vec3 target(tf.position.x * 0.3f, 2.4f + tf.position.y * 0.45f, FORMATION_Z);

    const float follow = snap ? 1.0f : 1.0f - std::exp(-5.0f * deltaTime);
    cameraPosition = glm::mix(cameraPosition, eye, follow);
    cameraTarget = glm::mix(cameraTarget, target, follow);
    camera->lookAt(cameraPosition, cameraTarget);
    // A little of the ship's bank
    const float roll = glm::radians(ship.roll * 0.2f);
    const glm::vec3 right = glm::normalize(glm::cross(camera->front, glm::vec3(0.0f, 1.0f, 0.0f)));
    camera->up = glm::normalize(glm::vec3(0.0f, 1.0f, 0.0f) * std::cos(roll) + right * std::sin(roll));
  }

  // Where both cannons' shots meet: straight ahead of the ship, at about the depth of the formation's front row
  glm::vec3 aimPoint(Engine &engine) {
    const glm::vec3 &ship = engine.get<Transform>(player).position;
    return {ship.x, ship.y, formationCenter(engine.get<GameState>(gameManager).time).z + 4.0f};
  }

  void updateReticle(Engine &engine) {
    const auto &state = engine.get<GameState>(gameManager);
    const glm::vec3 center = aimPoint(engine);
    const glm::vec2 offsets[] = {{-1.1f, 0.0f}, {1.1f, 0.0f}, {0.0f, -1.1f}, {0.0f, 1.1f}};
    for (size_t i = 0; i < reticle.size(); ++i)
      // Out of sight while the game is over
      engine.get<Transform>(reticle[i]).position =
          state.gameOver ? glm::vec3(0.0f, -1000.0f, 0.0f) : center + glm::vec3(offsets[i], 0.0f);
  }

  void hitPlayer(Engine &engine) {
    auto &ship = engine.get<Player>(player);
    if (ship.hitCooldown > 0.0f)
      return;
    ship.hitCooldown = ship.hitInvincibility;

    auto &state = engine.get<GameState>(gameManager);
    auto &tf = engine.get<Transform>(player);
    createExplosion(engine, tf.position, glm::vec3(0.2f, 1.0f, 0.8f), 2.5f);
    engine.playSoundAt(SOUND_PLAYER_HIT, tf.position, 1.0f);

    if (--state.lives <= 0) {
      state.gameOver = true;
      state.playerWon = false;
      state.restartTimer = 3.5f;
      engine.setEmission(player, glm::vec3(1.0f, 0.1f, 0.1f), 3.0f);
    }
  }

  void destroyInvader(Engine &engine, Entity entity, Invader &invader, bool scored) {
    const glm::vec3 &position = engine.get<Transform>(entity).position;
    createExplosion(engine, position, invader.color, 1.4f);
    engine.playSoundAt(SOUND_EXPLOSION, position, 0.8f, pitchJitter(rng));
    invader.alive = false;
    if (scored)
      engine.get<GameState>(gameManager).score += invader.scoreValue;
  }

  void updateInvaders(Engine &engine, float deltaTime) {
    auto &cm = engine.getComponentManager();
    auto &state = engine.get<GameState>(gameManager);
    state.time += deltaTime;

    // Between waves: count down, then spawn the next one (or win after the last). Not once the game is over: the
    // survivors only keep flying until the restart.
    if (state.nextWaveTimer > 0.0f && !state.gameOver) {
      state.nextWaveTimer -= deltaTime;
      if (state.nextWaveTimer <= 0.0f)
        spawnWave(engine, state);
      return;
    }
    if (aliveInvaders(engine) == 0 && !state.gameOver) {
      if (++state.wave >= WAVE_COUNT) {
        state.gameOver = true;
        state.playerWon = true;
        state.restartTimer = 4.0f;
      } else {
        state.nextWaveTimer = 2.0f;
      }
      return;
    }

    const glm::vec3 center = formationCenter(state.time);
    const glm::vec3 &shipPosition = engine.get<Transform>(player).position;
    std::vector<Entity> inFormation;

    cm.forEachComponent<Invader>([&](Entity e, Invader &inv) {
      if (!inv.alive)
        return;
      auto &tf = engine.get<Transform>(e);
      const glm::vec3 previous = tf.position;
      const glm::vec3 place = center + inv.slot;

      switch (inv.state) {
      case InvaderState::Entering: {
        if (inv.delay > 0.0f) {
          inv.delay -= deltaTime;
          break;
        }
        inv.progress = glm::min(inv.progress + deltaTime / 2.4f, 1.0f);
        // A curve from the start to its place, bending sideways on the way, slowing down to arrive
        const float t = 1.0f - (1.0f - inv.progress) * (1.0f - inv.progress);
        const glm::vec3 bend = inv.curve * (4.0f * t * (1.0f - t));
        tf.position = glm::mix(inv.start, place, t) + bend;
        if (inv.progress >= 1.0f)
          inv.state = InvaderState::InFormation;
        break;
      }
      case InvaderState::InFormation:
        tf.position = place;
        inFormation.push_back(e);
        break;
      case InvaderState::Diving: {
        // Steers toward the ship until close, then keeps going; past the camera it comes back from the far end
        if (tf.position.z < shipPosition.z - 8.0f) {
          const glm::vec3 wanted = glm::normalize(shipPosition - tf.position) * glm::length(inv.velocity);
          inv.velocity = glm::mix(inv.velocity, wanted, glm::min(1.5f * deltaTime, 1.0f));
        }
        tf.position += inv.velocity * deltaTime;
        if (tf.position.z > PLAYER_Z + 12.0f) {
          inv.state = InvaderState::Entering;
          inv.progress = 0.0f;
          inv.start = glm::vec3(tf.position.x, tf.position.y, SPAWN_Z);
          inv.curve = glm::vec3(0.0f);
          tf.position = inv.start;
        }
        break;
      }
      }

      // Leans into its sideways motion, and faces where it flies while diving
      const glm::vec3 motion = deltaTime > 0.0f ? (tf.position - previous) / deltaTime : glm::vec3(0.0f);
      const float roll = glm::clamp(-motion.x * 2.0f, -35.0f, 35.0f);
      tf.rotation.z = glm::mix(tf.rotation.z, roll, glm::min(6.0f * deltaTime, 1.0f));
      const float pitch = inv.state == InvaderState::Diving ? -20.0f : std::sin(state.time * 2.0f + inv.slot.x) * 6.0f;
      tf.rotation.x = glm::mix(tf.rotation.x, ALIEN_ROTATION.x + pitch, glm::min(4.0f * deltaTime, 1.0f));

      // The hitbox follows the tilt, so it keeps hugging the saucer
      engine.fitCollisionToModel(e, ALIEN_FIT);
    });

    if (inFormation.empty())
      return;
    std::uniform_int_distribution<size_t> pick(0, inFormation.size() - 1);

    // One of them breaks off to ram the ship
    state.diveTimer -= deltaTime;
    if (state.diveTimer <= 0.0f) {
      Entity diver = inFormation[pick(rng)];
      auto &inv = engine.get<Invader>(diver);
      inv.state = InvaderState::Diving;
      const glm::vec3 from = engine.get<Transform>(diver).position;
      inv.velocity = glm::normalize(shipPosition - from) * (15.0f + state.wave * 2.5f);
      state.diveTimer = waveDiveInterval(state.wave) * std::uniform_real_distribution<float>(0.7f, 1.3f)(rng);
    }

    // And one shoots at where the ship is
    state.enemyShootTimer -= deltaTime;
    if (state.enemyShootTimer <= 0.0f) {
      const glm::vec3 from = engine.get<Transform>(inFormation[pick(rng)]).position + glm::vec3(0.0f, 0.0f, 1.0f);
      const float speed = 16.0f + state.wave * 3.0f;
      spawnBullet(engine, from, glm::normalize(shipPosition - from) * speed, false);
      engine.playSoundAt(SOUND_ENEMY_SHOT, from, 0.5f, pitchJitter(rng));
      state.enemyShootTimer =
          waveShootInterval(state.wave) * std::uniform_real_distribution<float>(0.6f, 1.4f)(rng);
    }
  }

  void updateCollisions(Engine &engine, float deltaTime) {
    auto &cm = engine.getComponentManager();
    auto &state = engine.get<GameState>(gameManager);
    std::unordered_set<Entity> destroySet;

    struct Hit {
      Entity shot;
      Entity invader;
      glm::vec3 point;
    };
    std::vector<Hit> hits;

    // Move every bullet first, retiring the old ones; the player's test the path their tip flew...
    cm.forEachComponent<Bullet>([&](Entity entity, Bullet &bullet) {
      auto &tf = engine.get<Transform>(entity);
      if (bullet.friendly) {
        const glm::vec3 tip = tf.position + glm::normalize(bullet.velocity) * 1.0f;
        const float step = glm::length(bullet.velocity) * deltaTime;
        if (auto hit = engine.raycast<Invader>(tip, bullet.velocity, step))
          hits.push_back({entity, hit->entity, hit->point});
      }
      tf.position += bullet.velocity * deltaTime;
      if (!bullet.friendly)
        tf.rotation.z += 400.0f * deltaTime;
      bullet.ttl -= deltaTime;
      if (bullet.ttl <= 0.0f)
        destroySet.insert(entity);
    });

    // ...then what they hit: the player's bullets destroy invaders, the invaders' bullets hurt the player
    for (const Hit &hit : hits) {
      Invader &invader = engine.get<Invader>(hit.invader);
      if (!invader.alive || destroySet.count(hit.shot))
        continue;
      destroyInvader(engine, hit.invader, invader, true);
      destroySet.insert(hit.shot);
      destroySet.insert(hit.invader);
    }
    if (state.gameOver) {
      for (Entity e : destroySet)
        engine.destroyEntity(e);
      return;
    }
    engine.forEachCollision<Bullet, Player>([&](Entity shot, Bullet &bullet, Entity, Player &) {
      if (bullet.friendly || destroySet.count(shot))
        return;
      destroySet.insert(shot);
      hitPlayer(engine);
    });
    // A diving alien that reaches the ship takes itself out with it
    engine.forEachCollision<Invader, Player>([&](Entity inv, Invader &invader, Entity, Player &) {
      if (!invader.alive || engine.get<Player>(player).hitCooldown > 0.0f)
        return;
      destroyInvader(engine, inv, invader, false);
      destroySet.insert(inv);
      hitPlayer(engine);
    });

    for (Entity e : destroySet)
      engine.destroyEntity(e);
  }

  void updateExplosions(Engine &engine, float deltaTime) {
    auto &cm = engine.getComponentManager();
    std::vector<Entity> toDestroy;

    cm.forEachComponent<Explosion>([&](Entity e, Explosion &exp) {
      exp.emitTimer -= deltaTime;
      if (exp.emitTimer <= 0.0f)
        if (auto *emitter = cm.tryGet<ParticleEmitter>(e))
          emitter->active = false;
      exp.ttl -= deltaTime;
      if (exp.ttl <= 0.0f)
        toDestroy.push_back(e);
    });

    for (Entity e : toDestroy)
      engine.destroyEntity(e);
  }

  void updatePlayerEmission(Engine &engine) {
    auto &ship = engine.get<Player>(player);
    if (ship.hitCooldown > 0.0f) {
      float blink = glm::sin(ship.hitCooldown * 24.0f) * 0.5f + 0.5f;
      engine.setEmission(player, glm::vec3(0.2f, 0.2f, 0.2f), 0.8f + blink * 5.0f);
    } else {
      engine.setEmission(player, glm::vec3(0.0f), 0.0f);
    }
  }

public:
  void setup(Engine &engine) override {
    engine.setState(Toggle::CameraMovement, false);
    engine.setState(Toggle::CursorLock, false);

    engine.entities().create("Sky").withSkybox(SKYBOX, 1.0f).build();

    // A red and a blue sun from either side, and a soft white light from behind the camera on the aliens' faces
    engine.entities()
        .create("Sun")
        .withLight(Light::directional({-0.8f, -0.25f, -0.3f}, {1.0f, 0.15f, 0.1f}, 1.3f))
        .build();
    engine.entities()
        .create("Fill")
        .withLight(Light::directional({0.8f, -0.22f, -0.3f}, {0.15f, 0.3f, 1.0f}, 1.0f))
        .build();
    engine.entities()
        .create("Front")
        .withLight(Light::directional({0.0f, -0.35f, -1.0f}, {0.9f, 0.9f, 1.0f}, 0.55f))
        .build();
    engine.createCameraEntity("Camera", cameraPosition, 0.0f, 0.0f, 60.0f);

    // The aliens spawn during play; read their model now instead of on the first spawn
    engine.preloadModel(MODEL_ALIEN);
    for (const char *sound : {SOUND_LASER, SOUND_ENEMY_SHOT, SOUND_EXPLOSION, SOUND_PLAYER_HIT})
      engine.preloadSound(sound);
    engine.playMusic(MUSIC, 0.4f);
    engine.preloadSound(SOUND_UI_HOVER);
    engine.preloadSound(SOUND_UI_CLICK);

    createGameManager(engine);
    createStarfield(engine);
    createPlayer(engine);
    createReticle(engine);
    updateCamera(engine, 0.0f, true);

    checkFormationsReachable();
    totalInvaders = 0;
    for (int wave = 0; wave < WAVE_COUNT; ++wave)
      totalInvaders += static_cast<int>(formation(wave).size());

    this->engine = &engine;
    createHud(engine);
  }

  // The game's entities (player, invaders, game state) are gone, so the game steps aside and leaves the loaded scene
  // to be explored with the free camera
  void onSceneLoaded(Engine &engine, const std::string &) override {
    sceneReplaced = true;
    if (hudDocument)
      hudDocument->Hide();
    engine.stopMusic();
    engine.setState(Toggle::CameraMovement, true);
  }

  void update(Engine &engine, float deltaTime) override {
    if (sceneReplaced)
      return;
    updateExplosions(engine, deltaTime);

    auto &state = engine.get<GameState>(gameManager);
    updateHud(engine, deltaTime);

    if (restartRequested) {
      restartRequested = false;
      if (state.gameOver) {
        engine.playSound(SOUND_UI_CLICK, 0.6f);
        resetGame(engine);
        return;
      }
    }

    if (state.gameOver) {
      state.restartTimer -= deltaTime;
      if (state.playerWon) {
        float blink = glm::sin(state.restartTimer * 12.0f) * 0.5f + 0.5f;
        engine.setEmission(player, glm::vec3(0.15f, 1.0f, 0.2f), 1.2f + blink * 5.0f);
      }
      // The aliens keep flying, the bullets keep moving
      updateInvaders(engine, deltaTime);
      updateCollisions(engine, deltaTime);
      updateReticle(engine);
      if (state.restartTimer <= 0.0f)
        resetGame(engine);
      return;
    }

    updatePlayer(engine, deltaTime);
    updateInvaders(engine, deltaTime);
    updateCollisions(engine, deltaTime);
    if (!engine.getState(Toggle::CameraMovement)) // the chase camera steps aside while the free camera is on
      updateCamera(engine, deltaTime);
    updateReticle(engine);
    updatePlayerEmission(engine);
  }
};
