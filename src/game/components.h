#pragma once
// Components used by the Space Aliens example game.

#include <glm/glm.hpp>

struct Bullet {
  glm::vec3 velocity;
  float ttl;
  bool friendly;
};

enum class InvaderState {
  Entering,    // flying in from the far end to its place in the formation
  InFormation, // holding its place, moving with the formation
  Diving,      // leaving the formation to ram the player
};

struct Invader {
  bool alive = true;
  glm::vec3 color{1.0f};
  int scoreValue = 10;
  glm::vec3 slot{0.0f}; // its place in the formation, relative to the formation's center

  InvaderState state = InvaderState::Entering;
  float delay = 0.0f;      // Entering: seconds before it starts flying in
  float progress = 0.0f;   // Entering: 0 at the start of the flight, 1 at its place
  glm::vec3 start{0.0f};   // Entering: where the flight began
  glm::vec3 curve{0.0f};   // Entering: sideways bend of the path
  glm::vec3 velocity{0.0f}; // Diving
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

  int wave = 0;              // index of the current wave
  float nextWaveTimer = 1.0f; // > 0: seconds until the wave spawns
  float time = 0.0f;         // drives the formation's sway
  float diveTimer = 4.0f;
  float enemyShootTimer = 2.0f;
};

struct Player {
  glm::vec2 velocity{0.0f}; // x and y
  float roll = 0.0f;        // banks into horizontal turns
  float pitch = 0.0f;       // noses up and down with vertical movement
  float hitCooldown = 0.0f;
  float shootTimer = 0.0f;
  bool leftCannon = false; // shots alternate between the two wing cannons

  float acceleration = 70.0f;
  float friction = 7.0f;
  float maxSpeed = 13.0f;
  glm::vec2 boundsMin{-11.0f, 0.5f}; // the formations are laid out to stay within reach of these
  glm::vec2 boundsMax{11.0f, 10.0f};
  float shootCooldown = 0.2f;
  float bulletSpeed = 70.0f;
  float hitInvincibility = 1.8f;
};
