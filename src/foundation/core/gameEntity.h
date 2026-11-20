#pragma once
// The game API's entity handle, and the description engine.create takes. Lua scripts and C++ games use the same
// ones: the Lua `Entity` type is this class (see the bindings in systems/scriptSystem.cpp), so what a script does to
// an entity, C++ can too, with the same behavior.
//
// A handle is an engine pointer and an entity number: cheap to copy and to keep, and safe to keep after the entity
// is gone (valid() says; getters then return defaults and setters do nothing).

#include "components/characterController.h"
#include "components/rigidBody.h"
#include "foundation/ecs/entityManager.h"
#include "rendering/resources/customShader.h"
#include <cstdint>
#include <glm/glm.hpp>
#include <optional>
#include <string>
#include <type_traits>
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
  // "Animator", "AnimationStateMachine", "Script", "Transform", "AudioSource", "Skybox", "Collision", "ParticleEmitter", "Hidden", "CameraRig", "Tags") or a game
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
  // A `submesh` (counted from 0, as the editor lists them) or a `part` (an OBJ object/group or glTF node, by name)
  // limits a look to that part of the model; -1, the default, is every part.
  glm::vec3 color() const;
  void setColor(const glm::vec3 &color, int submesh = -1);
  void setColor(const glm::vec3 &color, const std::string &part);
  void setEmission(const glm::vec3 &color, float strength = 1.0f, int submesh = -1);
  void setEmission(const glm::vec3 &color, float strength, const std::string &part);
  void setVisible(bool visible);
  bool isVisible() const;
  // slot: "diffuse", "specular", "normal" or "emission"; path relative to assets/ like every asset path
  void setTexture(const std::string &slot, const std::string &path, int submesh = -1);
  void setTexture(const std::string &slot, const std::string &path, const std::string &part);
  void setShininess(float shininess, int submesh = -1);
  void setShininess(float shininess, const std::string &part);

  // Sets its Collision box (adding one if needed) to its model's bounds, as rotated and scaled now; `fit` scales the
  // box around its center (see Engine::fitCollisionToModel). Call it again after changing the rotation or scale a lot.
  void fitCollision(float fit = 1.0f, bool isStatic = false);
  // Whether its Collision box overlaps the other's now (see Engine::isColliding)
  bool isTouching(const GameEntity &other) const;

  // Custom surface shaders (see rendering/resources/customShader.h and the README): a GLSL file in assets/ with a
  // surface() and/or vertex() function that the engine adds to its lit shader, so lights, shadows and fog still apply.
  // `submesh` is the model's part, counting from 0 as the editor lists them; -1 means every part. Only this entity's
  // parts change, not others made from the same model file. setShader("") returns to the standard shader. A file that
  // does not compile is logged with its name and the part draws with the standard shader.
  void setShader(const std::string &path, int submesh = -1);
  // The part's shader file, "" for the standard shader (or without a model)
  std::string shader(int submesh = 0) const;
  // A value for one of the shader's uniforms: a number, vec2/vec3/vec4, or a texture's path (relative to assets/).
  // Needs the part to have a custom shader; a name it does not declare or a value of another kind is a warning.
  void setShaderParam(const std::string &name, const ShaderValue &value, int submesh = -1);
  // The part's value for a uniform (the file's default until set); nothing for a name the shader does not have
  std::optional<ShaderValue> shaderParam(const std::string &name, int submesh = 0) const;

  // Physics body (RigidBody): its velocity, and a push now (impulse) or this frame (force)
  glm::vec3 velocity() const;
  void setVelocity(const glm::vec3 &velocity);
  void impulse(const glm::vec3 &impulse);
  void force(const glm::vec3 &force);

  // The collision layer (see CollisionLayers) of its RigidBody, CharacterController and Collision, whichever it has: what
  // it hits and what hits it. "default" for an entity with none of them. setLayer takes effect at once, also on a body
  // that is already simulating; a name that is no layer is a warning and changes nothing.
  std::string layer() const;
  void setLayer(const std::string &layer);

  // Character (CharacterController): walk at this velocity until told otherwise, jump at this speed
  void move(const glm::vec3 &velocity);
  void jump(float speed);
  bool onGround() const;

  // Animation clip of its model, blended in over `fadeSeconds`; the clips there are
  void play(const std::string &clip, float fadeSeconds = kDefaultFade, bool loop = true);
  std::vector<std::string> clips() const;
  // Animation state machine: its definition file (relative to assets/; see the README), parameters, triggers and
  // state. setAnimator("") removes it. A manual play() takes the Animator over until the machine's next state change.
  void setAnimator(const std::string &path);
  void setParam(const std::string &name, float value);
  void setParam(const std::string &name, bool value);
  // Any other number (an int, a double) is a float parameter; the exact bool and float overloads above win over this
  template <typename T, typename = std::enable_if_t<std::is_arithmetic_v<T> && !std::is_same_v<T, bool>>>
  void setParam(const std::string &name, T value) {
    setParam(name, static_cast<float>(value));
  }
  float param(const std::string &name) const;
  void trigger(const std::string &name);
  // The state it is in, "" without a machine
  std::string animationState() const;
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
  // A walking character (see CharacterController): radius, height, maxSlope and stepHeight; the Transform is the
  // capsule's middle. A character is its own physics body, so it replaces `body` when both are given.
  std::optional<CharacterController> character;
  // Game components registered with Engine::registerComponent, by their name, added with the values their default
  // constructor gives ("Player"); one nothing registered is skipped with a warning
  std::vector<std::string> components;
  // The collision layer of its body, character and collision box (see CollisionLayers); empty: "default"
  std::string layer;
  std::string script;     // a Lua script, relative to assets/ ("scripts/coin.lua")
  std::vector<std::string> tags; // see GameEntity::addTag
};
