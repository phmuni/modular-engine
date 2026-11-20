#pragma once
// The game API's entity handle, and the description engine.create takes. Lua scripts and C++ games use the same
// ones: the Lua `Entity` type is this class (see the bindings in systems/scriptSystem.cpp), so what a script does to
// an entity, C++ can too, with the same behavior.
//
// A handle is an engine pointer and an entity number: cheap to copy and to keep, and safe to keep after the entity
// is gone (valid() says; getters then return defaults and setters do nothing).

#include "components/rigidBody.h"
#include "foundation/ecs/entityManager.h"
#include <cstdint>
#include <glm/glm.hpp>
#include <optional>
#include <string>
#include <vector>

class Engine;
struct Transform;
struct Light;

class GameEntity {
public:
  static constexpr float kDefaultFade = 0.2f; // seconds an animation takes to blend in (play)

  GameEntity() = default;
  GameEntity(Engine &engine, Entity entity) : m_engine(&engine), m_id(entity) {}

  // The entity number, -1 for an empty handle
  Entity id() const { return m_id; }
  // Whether the entity exists now (not destroyed, not replaced by a scene load)
  bool valid() const;
  explicit operator bool() const { return valid(); }
  friend bool operator==(const GameEntity &a, const GameEntity &b) { return a.m_id == b.m_id; }
  friend bool operator!=(const GameEntity &a, const GameEntity &b) { return a.m_id != b.m_id; }
  // "Entity 12 (Player)", for logs
  std::string describe() const;

  std::string name() const;
  void setName(const std::string &name);

  // The Transform and Light components, or null (the pointer stays good until the entity loses the component)
  Transform *transform() const;
  Light *light() const;
  // Whether it has a Script component
  bool hasScript() const;
  // Whether it has the component: an engine one ("Model", "RigidBody", "CharacterController", "Light", "Camera",
  // "Animator", "Script", "Transform", "AudioSource", "Skybox", "Collision", "ParticleEmitter", "Hidden", "CameraRig", "Tags") or a game
  // component registered with Engine::registerComponent, by its name
  bool has(const std::string &component) const;

  // Tags (see components/tags.h): labels to find groups of entities by (Engine::findTagged). Each tag is kept once, an
  // empty one is ignored, and the entity's last tag removed takes the Tags component with it.
  void addTag(const std::string &tag);
  void removeTag(const std::string &tag);
  bool hasTag(const std::string &tag) const;
  std::vector<std::string> tags() const;

  // Where it is; rotation is Euler angles in degrees, scale a size factor. A destroyed entity is at the origin.
  glm::vec3 position() const;
  void setPosition(const glm::vec3 &position);
  glm::vec3 rotation() const;
  void setRotation(const glm::vec3 &rotation);
  glm::vec3 scale() const;
  void setScale(const glm::vec3 &scale);
  // Where it faces on the ground, -Z turned by its yaw (rotation.y), and the way to its right: unit vectors
  glm::vec3 forward() const;
  glm::vec3 right() const;

  // Removes the entity and all its components
  void destroy();

  // Hierarchy (see components/parent.h): an empty or invalid handle as parent frees it
  GameEntity parent() const;
  bool setParent(const GameEntity &parent);
  std::vector<GameEntity> children() const;

  // Looks: a color for the whole model (no model: nothing), emission, and whether it is drawn at all. color() is the
  // first part's color (white without a model).
  glm::vec3 color() const;
  void setColor(const glm::vec3 &color);
  void setEmission(const glm::vec3 &color, float strength = 1.0f);
  void setVisible(bool visible);
  bool isVisible() const;
  // slot: "diffuse", "specular", "normal" or "emission"; path relative to assets/ like every asset path
  void setTexture(const std::string &slot, const std::string &path);
  void setShininess(float shininess);

  // Physics body (RigidBody): its velocity, and a push now (impulse) or this frame (force)
  glm::vec3 velocity() const;
  void setVelocity(const glm::vec3 &velocity);
  void impulse(const glm::vec3 &impulse);
  void force(const glm::vec3 &force);

  // Character (CharacterController): walk at this velocity until told otherwise, jump at this speed
  void move(const glm::vec3 &velocity);
  void jump(float speed);
  bool onGround() const;

  // Animation clip of its model, blended in over `fadeSeconds`; the clips there are
  void play(const std::string &clip, float fadeSeconds = kDefaultFade, bool loop = true);
  std::vector<std::string> clips() const;
  // Its AudioSource (see components/audioSource.h): the sound attached to it, heard from where the entity is as it
  // moves. All of these do nothing without one (add it in the editor, or with the builder's withSound). playAudio
  // starts it over (or continues it after pauseAudio), stopAudio stops and rewinds it. Volume is 1 for normal,
  // pitch 1 for the sound as recorded.
  void playAudio();
  void stopAudio();
  void pauseAudio();
  // Playing and not paused: false once a sound that does not loop has ended
  bool isAudioPlaying() const;
  float audioVolume() const;
  void setAudioVolume(float volume);
  float audioPitch() const;
  void setAudioPitch(float pitch);
  bool audioLoop() const;
  void setAudioLoop(bool loop);
  // A sound from where it is; returns a handle for Engine::stopSound and friends (0: nothing played)
  uint32_t sound(const std::string &path, float volume = 1.0f);

private:
  Engine *m_engine = nullptr;
  Entity m_id = -1;
};

// What Engine::create builds an entity from; every field is optional.
struct EntityDesc {
  // "box", "sphere" or "cylinder" (the engine's own models), or a model file; empty: no model, only a Transform
  std::string model;
  glm::vec3 position{0.0f};
  glm::vec3 rotation{0.0f}; // degrees
  glm::vec3 scale{1.0f};
  std::optional<glm::vec3> color;    // a solid color for the whole model
  std::optional<glm::vec3> emission; // glows in this color, `glow` strong
  float glow = 1.5f;
  std::optional<BodyType> body; // a physics body, shaped by `shape` (see RigidBody) and `mass` (dynamic only)
  BodyShape shape = BodyShape::Box;
  float mass = 0.0f;
  bool collision = false; // a Collision box that hugs the model (see Engine::fitCollisionToModel)
  std::string script;     // a Lua script, relative to assets/ ("scripts/coin.lua")
  std::vector<std::string> tags; // see GameEntity::addTag
};
