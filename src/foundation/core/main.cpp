
// Main entry point for the application. Sets up the engine and runs the main loop with the provided application logic.

#include "components/particleEmitter.h"
#include "components/transform.h"
#include "foundation/core/config.h"
#include "foundation/core/engine.h"
#include "systems/collisionSystem.h"
#include "systems/inputSystem.h"
#include "systems/sceneSystem.h"
#include <random>
#include <unordered_set>

struct InvaderSlot {
  glm::vec3 color;
  int scoreValue;
};

struct Bullet {
  glm::vec3 velocity;
  float ttl;
  bool friendly;
};

struct Invader {
  bool alive;
  glm::vec3 color;
  int scoreValue;
  int pathIndex;
  int currentRow = -1;
};

struct Explosion {
  float emitTimer;
  float ttl;
};

struct GameState {
  int score = 0;
  int lives = 3;
  bool gameOver = false;
  bool playerWon = false;
  float restartTimer = 0.0f;
};

struct Player {
  float velocityX = 0.0f;
  float tilt = 0.0f;
  float flashTimer = 0.0f;
  float hitCooldown = 0.0f;
  float shootTimer = 0.0f;

  float acceleration = 80.0f;
  float friction = 10.0f;
  float maxSpeed = 18.0f;
  float bounds = 12.0f;
  float shootCooldown = 0.5f;
  float bulletSpeed = 28.0f;
  float hitInvincibility = 1.8f;
};

struct InvaderGroup {
  std::vector<InvaderSlot> invaderSlots;
  size_t nextInvaderIndex = 0;
  float headPathDistance = 0.0f;
  float enemyShootTimer = 1.0f;

  int rows = 3;
  int cols = 6;
  float spacing = 1.8f;
  glm::vec3 scale = {1.1f, 0.5f, 1.1f};
  float baseSpeed = 2.5f;
  float maxSpeed = 12.0f;
  float stepDown = 1.5f;

  float shootCooldownMin = 0.6f;
  float shootCooldownMax = 2.0f;
  float bulletSpeed = 10.0f;
};

class SpaceInvadersApp : public App {
  Entity player = -1;
  Entity starfield = -1;
  Entity gameManager = -1;

  std::mt19937 rng{std::random_device{}()};
  float bulletTTL = 5.0f;

  // Returns a color interpolated from purple (first row) to red (last row).
  // The gradient is stretched by `spread` so the transition to red happens later.
  glm::vec3 rowColor(int row, int totalRows) const {
    glm::vec3 purple = {0.55f, 0.10f, 0.75f};
    glm::vec3 red = {1.00f, 0.08f, 0.08f};
    if (totalRows <= 1)
      return purple;
    const float spread = 4.0f; // increase to make gradient larger (slower transition)
    int denom = glm::max(1, static_cast<int>((totalRows - 1) * spread));
    float t = static_cast<float>(row) / static_cast<float>(denom);
    return glm::mix(purple, red, glm::clamp(t, 0.0f, 1.0f));
  }

  int rowScore(int row, const InvaderGroup &ig) const { return (ig.rows - row) * 10; }

  glm::vec3 pathPosition(float d, float stepDown) const {
    d = glm::max(d, 0.0f);

    constexpr float startX = -8.0f;
    constexpr float W = 16.0f;
    constexpr float startZ = -10.0f;
    const float segLen = W + stepDown;

    int row = static_cast<int>(d / segLen);
    float remainder = std::fmod(d, segLen);
    bool goRight = (row % 2 == 0);

    float z = startZ + row * stepDown;
    float x, finalZ;

    if (remainder <= W) {
      x = goRight ? (startX + remainder) : (startX + W - remainder);
      finalZ = z;
    } else {
      float t = (remainder - W) / stepDown;
      float smooth = t * t * (3.0f - 2.0f * t);
      x = goRight ? (startX + W) : startX;
      finalZ = z + smooth * stepDown;
    }

    return {x, glm::sin(x * 0.4f + d * 0.12f) * 0.3f, finalZ};
  }

  void createPlayer(Engine &engine) {
    constexpr glm::vec3 scale{1.4f, 0.6f, 1.4f};
    player =
        engine.entities()
            .create("Player")
            .withModel("../assets/models/spaceship/spaceship.obj", glm::vec3(0.0f, 0.0f, 8.0f), glm::vec3(0.0f), scale)
            .withCollision(scale)
            .withComponent<Player>()
            .withSolidColor(glm::vec3(0.8f, 0.8f, 0.8f))
            .build();
  }

  void createStarfield(Engine &engine) {
    starfield = engine.entities()
                    .create("Starfield")
                    .withTransform(glm::vec3(0.0f, 7.0f, -40.0f), glm::vec3(0.0f), glm::vec3(1.0f))
                    .withComponent<ParticleEmitter>()
                    .build();
    auto &p = engine.getOrThrow<ParticleEmitter>(starfield);
    p.emitRate = 100.0f;
    p.particleLifetime = 9.0f;
    p.speed = 12.0f;
    p.speedVariance = 3.5f;
    p.size = 0.09f;
    p.sizeDecay = 0.0f;
    p.gravity = {0.0f, 0.0f, 0.0f};
    p.startColor = {0.85f, 0.85f, 1.0f, 0.9f};
    p.endColor = {0.85f, 0.85f, 1.0f, 0.0f};
    p.emitDirection = {0.0f, 0.0f, 1.0f};
    p.spread = 0.28f;
    p.maxParticles = 1000;
    p.additiveBlending = true;
  }

  void createGameManager(Engine &engine) {
    gameManager =
        engine.entities().create("GameManager").withComponent<GameState>().withComponent<InvaderGroup>().build();
  }

  void createExplosion(Engine &engine, const glm::vec3 &position, const glm::vec3 &color, float scale = 1.0f) {
    Entity expl = engine.entities()
                      .create("Explosion")
                      .withTransform(position, glm::vec3(0.0f), glm::vec3(1.0f))
                      .withComponent<ParticleEmitter>()
                      .withComponent<Explosion>(0.1f, 0.55f * scale)
                      .build();
    auto &p = engine.getOrThrow<ParticleEmitter>(expl);
    p.emitRate = 350.0f;
    p.particleLifetime = 0.55f * scale;
    p.speed = 5.5f * scale;
    p.speedVariance = 3.0f * scale;
    p.size = 0.35f * scale;
    p.sizeDecay = 0.9f;
    p.gravity = {0.0f, 0.5f, 0.0f};
    p.startColor = {color.r, color.g, color.b, 1.0f};
    p.endColor = {color.r * 0.3f, color.g * 0.3f, color.b * 0.3f, 0.0f};
    p.emitDirection = {0.0f, 0.0f, 1.0f};
    p.spread = 1.0f;
    p.maxParticles = 80;
    p.additiveBlending = true;
  }

  void spawnBullet(Engine &engine, const glm::vec3 &pos, const glm::vec3 &vel, bool friendly) {
    const glm::vec3 scale = friendly ? glm::vec3(0.12f, 0.12f, 1.2f) : glm::vec3(0.22f, 0.22f, 0.70f);
    Entity bullet = engine.entities()
                        .create(friendly ? "PlayerBullet" : "EnemyBullet")
                        .withModel(EngineConfig::MODEL_BOX, pos, glm::vec3(0.0f), scale)
                        .withCollision(scale)
                        .withEmission(friendly ? glm::vec3(0.15f, 1.0f, 0.75f) : glm::vec3(1.0f, 0.18f, 0.08f), 7.0f)
                        .withComponent<Bullet>(vel, bulletTTL, friendly)
                        .build();
  }

  void initInvaderSlots(InvaderGroup &ig) {
    ig.invaderSlots.clear();
    ig.nextInvaderIndex = 0;
    ig.invaderSlots.reserve(ig.rows * ig.cols);
    for (int r = 0; r < ig.rows; ++r)
      for (int c = 0; c < ig.cols; ++c)
        ig.invaderSlots.push_back({rowColor(r, ig.rows), rowScore(r, ig)});
  }

  void resetGame(Engine &engine) {
    auto &scene = engine.getSystemManager().getSystem<SceneSystem>();
    auto &cm = engine.getComponentManager();

    std::vector<Entity> toDestroy;
    cm.forEachComponent<Invader>([&](Entity e, Invader &) { toDestroy.push_back(e); });
    cm.forEachComponent<Bullet>([&](Entity e, Bullet &) { toDestroy.push_back(e); });
    cm.forEachComponent<Explosion>([&](Entity e, Explosion &) { toDestroy.push_back(e); });
    for (Entity e : toDestroy)
      scene.destroyEntity(e);

    auto &state = engine.getOrThrow<GameState>(gameManager);
    auto &ig = engine.getOrThrow<InvaderGroup>(gameManager);
    auto &pData = engine.getOrThrow<Player>(player);
    auto &tf = engine.getOrThrow<Transform>(player);

    initInvaderSlots(ig);
    ig.headPathDistance = 0.0f;
    ig.enemyShootTimer = 1.0f;

    pData = Player{};

    tf.position = glm::vec3(0.0f, 0.0f, 8.0f);
    tf.rotation = glm::vec3(0.0f);

    engine.setEmission(player, -1, glm::vec3(1.0f, 0.90f, 0.20f), 0.0f);

    state = GameState{};
  }

  void updatePlayer(Engine &engine, float deltaTime) {
    auto &input = engine.getSystemManager().getSystem<InputSystem>();
    auto &tf = engine.getOrThrow<Transform>(player);
    auto &pData = engine.getOrThrow<Player>(player);

    float axis = 0.0f;
    if (input.isKeyPressed(SDL_SCANCODE_A) || input.isKeyPressed(SDL_SCANCODE_LEFT))
      axis -= 1.0f;
    if (input.isKeyPressed(SDL_SCANCODE_D) || input.isKeyPressed(SDL_SCANCODE_RIGHT))
      axis += 1.0f;

    if (axis != 0.0f) {
      pData.velocityX += axis * pData.acceleration * deltaTime;
    } else {
      pData.velocityX = glm::mix(pData.velocityX, 0.0f, glm::min(pData.friction * deltaTime, 1.0f));
    }

    pData.velocityX = glm::clamp(pData.velocityX, -pData.maxSpeed, pData.maxSpeed);
    tf.position.x += pData.velocityX * deltaTime;
    tf.position.x = glm::clamp(tf.position.x, -pData.bounds, pData.bounds);

    if (tf.position.x <= -pData.bounds || tf.position.x >= pData.bounds)
      pData.velocityX = 0.0f;

    pData.tilt = glm::mix(pData.tilt, -axis * 30.0f, 8.0f * deltaTime);
    tf.rotation.z = pData.tilt;

    pData.shootTimer -= deltaTime;
    if (input.isKeyPressed(SDL_SCANCODE_SPACE) && pData.shootTimer <= 0.0f) {
      spawnBullet(engine, tf.position + glm::vec3(0.0f, 0.0f, -1.0f), {0.0f, 0.0f, -pData.bulletSpeed}, true);
      pData.shootTimer = pData.shootCooldown;
      pData.flashTimer = 0.06f;
    }

    if (pData.hitCooldown > 0.0f)
      pData.hitCooldown -= deltaTime;
  }

  void killPlayer(Engine &engine) {
    auto &pData = engine.getOrThrow<Player>(player);
    if (pData.hitCooldown > 0.0f)
      return;

    pData.hitCooldown = pData.hitInvincibility;

    auto &state = engine.getOrThrow<GameState>(gameManager);
    auto &tf = engine.getOrThrow<Transform>(player);
    createExplosion(engine, tf.position, glm::vec3(0.2f, 1.0f, 0.8f), 2.5f);

    if (--state.lives <= 0) {
      state.gameOver = true;
      state.playerWon = false;
      state.restartTimer = 3.5f;
      engine.setEmission(player, -1, glm::vec3(1.0f, 0.1f, 0.1f), 3.0f);
    } else {
      engine.setEmission(player, -1, glm::vec3(1.0f, 0.5f, 0.2f), 4.0f);
    }
  }

  void updateInvaders(Engine &engine, float deltaTime) {
    auto &cm = engine.getComponentManager();
    auto &state = engine.getOrThrow<GameState>(gameManager);
    auto &ig = engine.getOrThrow<InvaderGroup>(gameManager);

    int totalAlive = 0;
    cm.forEachComponent<Invader>([&](Entity, Invader &inv) {
      if (inv.alive)
        ++totalAlive;
    });

    bool allSpawned = ig.nextInvaderIndex >= ig.invaderSlots.size();

    if (totalAlive == 0 && allSpawned) {
      state.gameOver = true;
      state.playerWon = true;
      state.restartTimer = 4.0f;
      return;
    }

    float fractionAlive = allSpawned ? static_cast<float>(totalAlive) / (ig.rows * ig.cols) : 1.0f;
    float speedT = 1.0f - glm::clamp(fractionAlive, 0.0f, 1.0f);
    ig.headPathDistance += glm::mix(ig.baseSpeed, ig.maxSpeed, speedT * speedT) * deltaTime;

    while (ig.nextInvaderIndex < ig.invaderSlots.size()) {
      if (ig.headPathDistance < static_cast<float>(ig.nextInvaderIndex) * ig.spacing)
        break;
      const auto &slot = ig.invaderSlots[ig.nextInvaderIndex];
      Entity invader =
          engine.entities()
              .create("Invader")
              .withModel(EngineConfig::MODEL_BOX, glm::vec3(0.0f), glm::vec3(0.0f), ig.scale)
              .withCollision(ig.scale)
              .withSolidColor(slot.color)
              .withComponent<Invader>(true, slot.color, slot.scoreValue, static_cast<int>(ig.nextInvaderIndex), -1)
              .build();
      ++ig.nextInvaderIndex;
    }

    std::vector<Entity> aliveEntities;
    cm.forEachComponent<Invader>([&](Entity e, Invader &inv) {
      if (!inv.alive)
        return;
      aliveEntities.push_back(e);
      auto &tf = engine.getOrThrow<Transform>(e);
      glm::vec3 prev = tf.position;
      float travelD = ig.headPathDistance - inv.pathIndex * ig.spacing;
      tf.position = pathPosition(travelD, ig.stepDown);
      float dx = tf.position.x - prev.x;
      tf.rotation.z = glm::mix(tf.rotation.z, -dx * 2.5f, 10.0f * deltaTime);
      tf.rotation.x = glm::sin(travelD * 0.5f) * 0.08f;

      const float W = 16.0f;
      const float segLen = W + ig.stepDown;
      float dClamped = glm::max(travelD, 0.0f);
      int newRow = static_cast<int>(dClamped / segLen);
      if (newRow != inv.currentRow) {
        inv.currentRow = newRow;
        glm::vec3 newColor = rowColor(newRow, ig.rows);
        inv.color = newColor;
        engine.setSolidColor(e, -1, newColor);
      }
    });

    ig.enemyShootTimer -= deltaTime;
    if (ig.enemyShootTimer <= 0.0f && !aliveEntities.empty()) {
      Entity shooter = aliveEntities[std::uniform_int_distribution<size_t>(0, aliveEntities.size() - 1)(rng)];
      auto &tf = engine.getOrThrow<Transform>(shooter);
      spawnBullet(engine, tf.position + glm::vec3(0.0f, 0.0f, 1.0f), {0.0f, 0.0f, ig.bulletSpeed}, false);
      float aggressionScale = glm::mix(0.25f, 1.0f, fractionAlive);
      ig.enemyShootTimer =
          std::uniform_real_distribution<float>(ig.shootCooldownMin, ig.shootCooldownMax)(rng) * aggressionScale;
    }
  }

  void updateBullets(Engine &engine, float deltaTime) {
    auto &scene = engine.getSystemManager().getSystem<SceneSystem>();
    auto &collisionSystem = engine.getSystemManager().getSystem<CollisionSystem>();
    auto &cm = engine.getComponentManager();
    auto &state = engine.getOrThrow<GameState>(gameManager);

    std::unordered_set<Entity> destroySet;

    cm.forEachComponent<Bullet>([&](Entity entity, Bullet &bullet) {
      if (destroySet.count(entity))
        return;

      auto &tf = engine.getOrThrow<Transform>(entity);
      tf.position += bullet.velocity * deltaTime;

      if (!bullet.friendly)
        tf.rotation.z += 8.0f * deltaTime;

      bullet.ttl -= deltaTime;
      if (bullet.ttl <= 0.0f) {
        destroySet.insert(entity);
        return;
      }

      if (bullet.friendly) {
        cm.forEachComponent<Invader>([&](Entity inv, Invader &invader) {
          if (!invader.alive || destroySet.count(entity) || destroySet.count(inv))
            return;
          if (collisionSystem.checkEntitiesCollision(entity, inv, cm)) {
            createExplosion(engine, engine.getOrThrow<Transform>(inv).position, invader.color, 1.2f);
            invader.alive = false;
            state.score += invader.scoreValue;
            destroySet.insert(entity);
            destroySet.insert(inv);
          }
        });
      } else if (!state.gameOver && collisionSystem.checkEntitiesCollision(entity, player, cm)) {
        destroySet.insert(entity);
        killPlayer(engine);
      }
    });

    for (Entity e : destroySet)
      scene.destroyEntity(e);
  }

  void checkInvaderReach(Engine &engine) {
    auto &state = engine.getOrThrow<GameState>(gameManager);
    if (state.gameOver)
      return;

    auto &playerTf = engine.getOrThrow<Transform>(player);
    auto &cm = engine.getComponentManager();

    cm.forEachComponent<Invader>([&](Entity e, Invader &inv) {
      if (!inv.alive || state.gameOver)
        return;
      if (engine.getOrThrow<Transform>(e).position.z >= playerTf.position.z - 1.5f) {
        killPlayer(engine);
        state.gameOver = true;
      }
    });
  }

  void updateExplosions(Engine &engine, float deltaTime) {
    auto &scene = engine.getSystemManager().getSystem<SceneSystem>();
    auto &cm = engine.getComponentManager();
    std::vector<Entity> toDestroy;

    cm.forEachComponent<Explosion>([&](Entity e, Explosion &exp) {
      exp.emitTimer -= deltaTime;
      if (exp.emitTimer <= 0.0f)
        if (auto *emitter = cm.getOrNil<ParticleEmitter>(e))
          emitter->active = false;
      exp.ttl -= deltaTime;
      if (exp.ttl <= 0.0f)
        toDestroy.push_back(e);
    });

    for (Entity e : toDestroy)
      scene.destroyEntity(e);
  }

  void updatePlayerEmission(Engine &engine, float deltaTime) {
    auto &state = engine.getOrThrow<GameState>(gameManager);
    auto &pData = engine.getOrThrow<Player>(player);
    if (state.gameOver)
      return;

    if (pData.hitCooldown > 0.0f) {
      float blink = glm::sin(pData.hitCooldown * 24.0f) * 0.5f + 0.5f;
      engine.setEmission(player, -1, glm::vec3(0.2f, 0.2f, 0.2f), 0.8f + blink * 5.0f);
    } else {
      engine.setEmission(player, -1, glm::vec3(0.0f), 0.0f);
    }
  }

public:
  void setup(Engine &engine) override {
    engine.setState(Toggle::CameraMovement, false);
    engine.setState(Toggle::CursorLock, false);

    engine.createLightEntity("Sun", glm::vec3(0.0f), glm::vec3(-0.8f, -0.25f, 0.1f), glm::vec3(1.0f, 0.0f, 0.0f),
                             LightType::Directional, 1.4f, 0.0f, 0.0f);

    engine.createLightEntity("Fill", glm::vec3(0.0f), glm::vec3(0.8f, -0.22f, 0.15f), glm::vec3(0.0f, 0.0f, 1.0f),
                             LightType::Directional, 0.95f, 0.0f, 0.0f);

    engine.createLightEntity("Overhead", glm::vec3(0.0f, 12.0f, -15.0f), glm::vec3(0.0f, -1.0f, 0.5f),
                             glm::vec3(1.0f, 1.0f, 1.0f), LightType::Spot, 10.0f, glm::cos(glm::radians(80.0f)),
                             glm::cos(glm::radians(85.0f)));
    engine.createCameraEntity("Camera", glm::vec3(0.0f, 14.0f, 18.0f), 0.0f, -35.0f, 55.0f);

    createGameManager(engine);
    createStarfield(engine);
    createPlayer(engine);

    initInvaderSlots(engine.getOrThrow<InvaderGroup>(gameManager));
  }

  void update(Engine &engine, float deltaTime) override {
    updateExplosions(engine, deltaTime);

    auto &state = engine.getOrThrow<GameState>(gameManager);
    if (state.gameOver) {
      state.restartTimer -= deltaTime;
      if (state.playerWon) {
        float blink = glm::sin(state.restartTimer * 12.0f) * 0.5f + 0.5f;
        engine.setEmission(player, -1, glm::vec3(0.15f, 1.0f, 0.2f), 1.2f + blink * 5.0f);
      }
      if (state.restartTimer <= 0.0f)
        resetGame(engine);
      return;
    }

    updatePlayer(engine, deltaTime);
    updateInvaders(engine, deltaTime);
    updateBullets(engine, deltaTime);
    checkInvaderReach(engine);
    updatePlayerEmission(engine, deltaTime);
  }
};

int main() {
  Engine engine;
  if (!engine.initialize())
    return 1;
  SpaceInvadersApp application;
  engine.run(application);
  return 0;
}