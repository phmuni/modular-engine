#pragma once
// Components used by the Space Aliens example game.

#include <glm/glm.hpp>
#include <vector>

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
  float spacing = 2.4f;
  glm::vec3 scale = {1.1f, 0.5f, 1.1f};
  float baseSpeed = 2.5f;
  float maxSpeed = 14.0f;
  float stepDown = 1.5f;

  float shootCooldownMin = 0.6f;
  float shootCooldownMax = 2.0f;
  float bulletSpeed = 10.0f;
};
