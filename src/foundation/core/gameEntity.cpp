// The game API's entity handle: see gameEntity.h.

#include "foundation/core/gameEntity.h"

#include "components/characterController.h"
#include "components/collision.h"
#include "components/audioSource.h"
#include "components/hidden.h"
#include "components/light.h"
#include "components/model.h"
#include "components/name.h"
#include "components/tags.h"
#include "components/transform.h"
#include "foundation/core/engine.h"
#include "foundation/core/logger.h"
#include "rendering/resources/material.h"
#include "systems/renderSystem.h"
#include "systems/resourceSystem.h"
#include <algorithm>
#include <cmath>

bool GameEntity::valid() const { return m_engine && m_id >= 0 && m_engine->isAlive(m_id); }

bool GameEntity::isRuntime() const { return valid() && m_engine->isRuntime(m_id); }

std::string GameEntity::describe() const {
  const Name *name = m_engine ? m_engine->tryGet<Name>(m_id) : nullptr;
  return "Entity " + std::to_string(m_id) + (name ? " (" + name->name + ")" : "");
}

std::string GameEntity::name() const {
  const Name *name = m_engine ? m_engine->tryGet<Name>(m_id) : nullptr;
  return name ? name->name : std::string();
}

void GameEntity::setName(const std::string &name) {
  if (Name *existing = m_engine ? m_engine->tryGet<Name>(m_id) : nullptr)
    existing->name = name;
}

Transform *GameEntity::transform() const { return m_engine ? m_engine->tryGet<Transform>(m_id) : nullptr; }
Light *GameEntity::light() const { return m_engine ? m_engine->tryGet<Light>(m_id) : nullptr; }
bool GameEntity::hasScript() const { return has("Script"); }
bool GameEntity::has(const std::string &component) const { return m_engine && m_engine->hasComponent(m_id, component); }

void GameEntity::addTag(const std::string &tag) {
  if (tag.empty() || !valid() || hasTag(tag))
    return;
  Tags *existing = m_engine->tryGet<Tags>(m_id);
  (existing ? *existing : m_engine->getComponentManager().addInPlace<Tags>(m_id)).tags.push_back(tag);
}

void GameEntity::removeTag(const std::string &tag) {
  Tags *existing = m_engine ? m_engine->tryGet<Tags>(m_id) : nullptr;
  if (!existing)
    return;
  existing->tags.erase(std::remove(existing->tags.begin(), existing->tags.end(), tag), existing->tags.end());
  if (existing->tags.empty())
    m_engine->removeComponent<Tags>(m_id);
}

bool GameEntity::hasTag(const std::string &tag) const {
  const Tags *existing = m_engine ? m_engine->tryGet<Tags>(m_id) : nullptr;
  return existing && std::find(existing->tags.begin(), existing->tags.end(), tag) != existing->tags.end();
}

std::vector<std::string> GameEntity::tags() const {
  const Tags *existing = m_engine ? m_engine->tryGet<Tags>(m_id) : nullptr;
  return existing ? existing->tags : std::vector<std::string>();
}

glm::vec3 GameEntity::position() const {
  const Transform *t = transform();
  return t ? t->position : glm::vec3(0.0f);
}

void GameEntity::setPosition(const glm::vec3 &position) {
  if (Transform *t = transform())
    t->position = position;
}

glm::vec3 GameEntity::rotation() const {
  const Transform *t = transform();
  return t ? t->rotation : glm::vec3(0.0f);
}

void GameEntity::setRotation(const glm::vec3 &rotation) {
  if (Transform *t = transform())
    t->rotation = rotation;
}

glm::vec3 GameEntity::scale() const {
  const Transform *t = transform();
  return t ? t->scale : glm::vec3(1.0f);
}

void GameEntity::setScale(const glm::vec3 &scale) {
  if (Transform *t = transform())
    t->scale = scale;
}

glm::vec3 GameEntity::forward() const {
  const Transform *t = transform();
  const float yaw = t ? glm::radians(t->rotation.y) : 0.0f;
  return glm::vec3(-std::sin(yaw), 0.0f, -std::cos(yaw));
}

glm::vec3 GameEntity::right() const {
  const Transform *t = transform();
  const float yaw = t ? glm::radians(t->rotation.y) : 0.0f;
  return glm::vec3(std::cos(yaw), 0.0f, -std::sin(yaw));
}

void GameEntity::destroy() {
  if (valid())
    m_engine->destroyEntity(m_id);
}

GameEntity GameEntity::parent() const {
  if (!valid())
    return GameEntity();
  const Entity parent = m_engine->getParent(m_id);
  return parent == -1 ? GameEntity() : GameEntity(*m_engine, parent);
}

bool GameEntity::setParent(const GameEntity &parent) { return m_engine && m_engine->setParent(m_id, parent.m_id); }

std::vector<GameEntity> GameEntity::children() const {
  std::vector<GameEntity> list;
  if (!m_engine)
    return list;
  for (Entity child : m_engine->getChildren(m_id))
    list.emplace_back(*m_engine, child);
  return list;
}

glm::vec3 GameEntity::color() const {
  const Model *model = m_engine ? m_engine->tryGet<Model>(m_id) : nullptr;
  if (!model || model->materialHandles.empty())
    return glm::vec3(1.0f);
  return m_engine->getSystemManager().getSystem<ResourceSystem>().getMaterial(model->materialHandles[0]).getDiffuseColor();
}

void GameEntity::setColor(const glm::vec3 &color, int submesh) {
  if (m_engine && m_engine->has<Model>(m_id))
    m_engine->setSolidColor(m_id, color, submesh);
}

void GameEntity::setColor(const glm::vec3 &color, const std::string &part) {
  if (m_engine && m_engine->has<Model>(m_id))
    m_engine->setSolidColor(m_id, color, part);
}

void GameEntity::setEmission(const glm::vec3 &color, float strength, int submesh) {
  if (m_engine && m_engine->has<Model>(m_id))
    m_engine->setEmission(m_id, color, strength, submesh);
}

void GameEntity::setEmission(const glm::vec3 &color, float strength, const std::string &part) {
  if (m_engine && m_engine->has<Model>(m_id))
    m_engine->setEmission(m_id, color, strength, part);
}

void GameEntity::setVisible(bool visible) {
  if (!valid() || visible == isVisible())
    return;
  if (visible)
    m_engine->removeComponent<Hidden>(m_id);
  else
    m_engine->getComponentManager().addInPlace<Hidden>(m_id);
  m_engine->getSystemManager().getSystem<RenderSystem>().markBatchesDirty();
}

bool GameEntity::isVisible() const { return !(m_engine && m_engine->has<Hidden>(m_id)); }

static TextureSlot textureSlotNamed(const std::string &slot) {
  return slot == "normal"              ? TextureSlot::Normal
         : slot == "emission"          ? TextureSlot::Emission
         : slot == "metallicRoughness" ? TextureSlot::MetallicRoughness
         : slot == "occlusion"         ? TextureSlot::Occlusion
                                       : TextureSlot::Diffuse;
}

void GameEntity::setTexture(const std::string &slot, const std::string &path, int submesh) {
  if (m_engine && m_engine->has<Model>(m_id))
    m_engine->setTexture(m_id, textureSlotNamed(slot), path, submesh);
}

void GameEntity::setTexture(const std::string &slot, const std::string &path, const std::string &part) {
  if (m_engine && m_engine->has<Model>(m_id))
    m_engine->setTexture(m_id, textureSlotNamed(slot), path, part);
}

void GameEntity::setMetallic(float metallic, int submesh) {
  if (m_engine && m_engine->has<Model>(m_id))
    m_engine->setMetallic(m_id, metallic, submesh);
}

void GameEntity::setMetallic(float metallic, const std::string &part) {
  if (m_engine && m_engine->has<Model>(m_id))
    m_engine->setMetallic(m_id, metallic, part);
}

void GameEntity::setRoughness(float roughness, int submesh) {
  if (m_engine && m_engine->has<Model>(m_id))
    m_engine->setRoughness(m_id, roughness, submesh);
}

void GameEntity::setRoughness(float roughness, const std::string &part) {
  if (m_engine && m_engine->has<Model>(m_id))
    m_engine->setRoughness(m_id, roughness, part);
}

void GameEntity::setUnlit(bool unlit, int submesh) {
  if (m_engine && m_engine->has<Model>(m_id))
    m_engine->setUnlit(m_id, unlit, submesh);
}

void GameEntity::setUnlit(bool unlit, const std::string &part) {
  if (m_engine && m_engine->has<Model>(m_id))
    m_engine->setUnlit(m_id, unlit, part);
}

void GameEntity::fitCollision(float fit, bool isStatic) {
  if (m_engine && m_engine->has<Model>(m_id))
    m_engine->fitCollisionToModel(m_id, fit, isStatic);
}

bool GameEntity::isTouching(const GameEntity &other) const {
  return m_engine && other.m_engine && m_engine->isColliding(m_id, other.m_id);
}

void GameEntity::setShader(const std::string &path, int submesh) {
  if (!m_engine || !m_engine->has<Model>(m_id))
    return;
  m_engine->getSystemManager().getSystem<ResourceSystem>().setShader(m_id, submesh, path);
  m_engine->getSystemManager().getSystem<RenderSystem>().markBatchesDirty();
}

void GameEntity::setShader(const std::string &path, const std::string &part) {
  if (!m_engine || !m_engine->has<Model>(m_id))
    return;
  if (const int submesh = m_engine->getSystemManager().getSystem<ResourceSystem>().findSubmesh(m_id, part); submesh >= 0)
    setShader(path, submesh);
}

std::string GameEntity::shader(int submesh) const {
  return m_engine ? m_engine->getSystemManager().getSystem<ResourceSystem>().getShader(m_id, submesh) : std::string();
}

void GameEntity::setShaderParam(const std::string &name, const ShaderValue &value, int submesh) {
  if (m_engine && m_engine->has<Model>(m_id))
    m_engine->getSystemManager().getSystem<ResourceSystem>().setShaderParam(m_id, submesh, name, value);
}

std::string GameEntity::shader(const std::string &part) const {
  if (!m_engine || !m_engine->has<Model>(m_id))
    return std::string();
  const int submesh = m_engine->getSystemManager().getSystem<ResourceSystem>().findSubmesh(m_id, part);
  return submesh >= 0 ? shader(submesh) : std::string();
}

void GameEntity::setShaderParam(const std::string &name, const ShaderValue &value, const std::string &part) {
  if (!m_engine || !m_engine->has<Model>(m_id))
    return;
  if (const int submesh = m_engine->getSystemManager().getSystem<ResourceSystem>().findSubmesh(m_id, part); submesh >= 0)
    setShaderParam(name, value, submesh);
}

std::optional<ShaderValue> GameEntity::shaderParam(const std::string &name, const std::string &part) const {
  if (!m_engine || !m_engine->has<Model>(m_id))
    return std::nullopt;
  const int submesh = m_engine->getSystemManager().getSystem<ResourceSystem>().findSubmesh(m_id, part);
  if (submesh < 0)
    return std::nullopt;
  return shaderParam(name, submesh);
}

std::optional<ShaderValue> GameEntity::shaderParam(const std::string &name, int submesh) const {
  if (!m_engine)
    return std::nullopt;
  return m_engine->getSystemManager().getSystem<ResourceSystem>().getShaderParam(m_id, submesh, name);
}

glm::vec3 GameEntity::velocity() const {
  return m_engine ? m_engine->physics().getLinearVelocity(m_id) : glm::vec3(0.0f);
}

void GameEntity::setVelocity(const glm::vec3 &velocity) {
  if (m_engine)
    m_engine->physics().setLinearVelocity(m_id, velocity);
}

void GameEntity::impulse(const glm::vec3 &impulse) {
  if (m_engine)
    m_engine->physics().addImpulse(m_id, impulse);
}

void GameEntity::force(const glm::vec3 &force) {
  if (m_engine)
    m_engine->physics().addForce(m_id, force);
}

glm::vec3 GameEntity::angularVelocity() const {
  return m_engine ? glm::degrees(m_engine->physics().getAngularVelocity(m_id)) : glm::vec3(0.0f);
}

void GameEntity::setAngularVelocity(const glm::vec3 &degreesPerSecond) {
  if (m_engine)
    m_engine->physics().setAngularVelocity(m_id, glm::radians(degreesPerSecond));
}

void GameEntity::torque(const glm::vec3 &torque) {
  if (m_engine)
    m_engine->physics().addTorque(m_id, torque);
}

void GameEntity::angularImpulse(const glm::vec3 &impulse) {
  if (m_engine)
    m_engine->physics().addAngularImpulse(m_id, impulse);
}

void GameEntity::impulseAt(const glm::vec3 &impulse, const glm::vec3 &worldPoint) {
  if (m_engine)
    m_engine->physics().addImpulseAt(m_id, impulse, worldPoint);
}

void GameEntity::forceAt(const glm::vec3 &force, const glm::vec3 &worldPoint) {
  if (m_engine)
    m_engine->physics().addForceAt(m_id, force, worldPoint);
}

void GameEntity::setJoint(const Joint &joint) {
  if (m_engine)
    m_engine->getComponentManager().addInPlace<Joint>(m_id) = joint;
}

void GameEntity::removeJoint() {
  if (m_engine)
    m_engine->removeComponent<Joint>(m_id);
}

bool GameEntity::hasJoint() const { return m_engine && m_engine->tryGet<Joint>(m_id) != nullptr; }

float GameEntity::jointAngle() const { return m_engine ? m_engine->physics().getJointAngle(m_id) : 0.0f; }

float GameEntity::jointPosition() const { return m_engine ? m_engine->physics().getJointPosition(m_id) : 0.0f; }

void GameEntity::setJointMotorSpeed(float speed) {
  if (Joint *joint = m_engine ? m_engine->tryGet<Joint>(m_id) : nullptr) {
    joint->motor = JointMotor::Velocity;
    joint->motorSpeed = speed;
  } else if (m_engine) {
    LOG_W("[Entity] %s has no Joint to drive (setJoint adds one)", describe().c_str());
  }
}

void GameEntity::setJointMotorTarget(float target) {
  if (Joint *joint = m_engine ? m_engine->tryGet<Joint>(m_id) : nullptr) {
    joint->motor = JointMotor::Position;
    joint->motorTarget = target;
  } else if (m_engine) {
    LOG_W("[Entity] %s has no Joint to drive (setJoint adds one)", describe().c_str());
  }
}

void GameEntity::stopJointMotor() {
  if (Joint *joint = m_engine ? m_engine->tryGet<Joint>(m_id) : nullptr)
    joint->motor = JointMotor::Off;
}

std::string GameEntity::layer() const {
  if (!m_engine)
    return CollisionLayers::kDefaultLayer;
  if (const CharacterController *character = m_engine->tryGet<CharacterController>(m_id))
    return character->layer;
  if (const RigidBody *body = m_engine->tryGet<RigidBody>(m_id))
    return body->layer;
  if (const Collision *collision = m_engine->tryGet<Collision>(m_id))
    return collision->layer;
  return CollisionLayers::kDefaultLayer;
}

void GameEntity::setLayer(const std::string &layer) {
  if (!m_engine)
    return;
  if (m_engine->layers().indexOf(layer) < 0) {
    LOG_W("[Entity] %s: \"%s\" is not a collision layer (Engine::layers().add defines one)", describe().c_str(),
          layer.c_str());
    return;
  }
  CharacterController *character = m_engine->tryGet<CharacterController>(m_id);
  RigidBody *body = m_engine->tryGet<RigidBody>(m_id);
  Collision *collision = m_engine->tryGet<Collision>(m_id);
  if (!character && !body && !collision)
    LOG_W("[Entity] %s has no RigidBody, CharacterController or Collision to put on layer \"%s\"", describe().c_str(),
          layer.c_str());
  if (character)
    character->layer = layer;
  if (body)
    body->layer = layer;
  if (collision)
    collision->layer = layer;
}

void GameEntity::move(const glm::vec3 &velocity) {
  if (CharacterController *character = m_engine ? m_engine->tryGet<CharacterController>(m_id) : nullptr)
    character->move = velocity;
}

void GameEntity::jump(float speed) {
  if (CharacterController *character = m_engine ? m_engine->tryGet<CharacterController>(m_id) : nullptr)
    character->jump = speed;
}

bool GameEntity::onGround() const {
  const CharacterController *character = m_engine ? m_engine->tryGet<CharacterController>(m_id) : nullptr;
  return character && character->onGround;
}

void GameEntity::play(const std::string &clip, float fadeSeconds, bool loop) {
  if (m_engine)
    m_engine->playAnimation(m_id, clip, fadeSeconds, loop);
}

std::vector<std::string> GameEntity::clips() const {
  return m_engine ? m_engine->getAnimationClips(m_id) : std::vector<std::string>();
}

bool GameEntity::bakeReflectionProbe() { return m_engine && m_engine->bakeReflectionProbe(m_id); }

bool GameEntity::refreshReflectionProbe() { return m_engine && m_engine->refreshReflectionProbe(m_id); }

bool GameEntity::isReflectionProbeStale() const { return m_engine && m_engine->isReflectionProbeStale(m_id); }

void GameEntity::setAnimator(const std::string &path) {
  if (m_engine)
    m_engine->setAnimator(m_id, path);
}

void GameEntity::setParam(const std::string &name, float value) {
  if (m_engine)
    m_engine->setAnimationParam(m_id, name, value);
}

void GameEntity::setParam(const std::string &name, bool value) {
  if (m_engine)
    m_engine->setAnimationParam(m_id, name, value);
}

float GameEntity::param(const std::string &name) const {
  return m_engine ? m_engine->getAnimationParam(m_id, name) : 0.0f;
}

void GameEntity::trigger(const std::string &name) {
  if (m_engine)
    m_engine->triggerAnimation(m_id, name);
}

std::string GameEntity::animationState() const {
  return m_engine ? m_engine->getAnimationState(m_id) : std::string();
}

void GameEntity::playAudio() {
  AudioSource *source = m_engine ? m_engine->tryGet<AudioSource>(m_id) : nullptr;
  if (!source)
    return;
  if (source->paused && source->playing) {
    source->paused = false; // continue from where it was held
    return;
  }
  source->paused = false;
  source->playing = true;
  source->rewind = true;
}

void GameEntity::stopAudio() {
  if (AudioSource *source = m_engine ? m_engine->tryGet<AudioSource>(m_id) : nullptr) {
    source->playing = false;
    source->paused = false;
    source->rewind = true;
  }
}

void GameEntity::pauseAudio() {
  if (AudioSource *source = m_engine ? m_engine->tryGet<AudioSource>(m_id) : nullptr)
    source->paused = source->playing;
}

bool GameEntity::isAudioPlaying() const {
  const AudioSource *source = m_engine ? m_engine->tryGet<AudioSource>(m_id) : nullptr;
  return source && source->playing && !source->paused;
}

float GameEntity::audioVolume() const {
  const AudioSource *source = m_engine ? m_engine->tryGet<AudioSource>(m_id) : nullptr;
  return source ? source->volume : 0.0f;
}

void GameEntity::setAudioVolume(float volume) {
  if (AudioSource *source = m_engine ? m_engine->tryGet<AudioSource>(m_id) : nullptr)
    source->volume = std::max(volume, 0.0f);
}

float GameEntity::audioPitch() const {
  const AudioSource *source = m_engine ? m_engine->tryGet<AudioSource>(m_id) : nullptr;
  return source ? source->pitch : 1.0f;
}

void GameEntity::setAudioPitch(float pitch) {
  if (AudioSource *source = m_engine ? m_engine->tryGet<AudioSource>(m_id) : nullptr)
    source->pitch = std::max(pitch, 0.01f);
}

bool GameEntity::audioLoop() const {
  const AudioSource *source = m_engine ? m_engine->tryGet<AudioSource>(m_id) : nullptr;
  return source && source->loop;
}

void GameEntity::setAudioLoop(bool loop) {
  if (AudioSource *source = m_engine ? m_engine->tryGet<AudioSource>(m_id) : nullptr)
    source->loop = loop;
}

uint32_t GameEntity::sound(const std::string &path, float volume) {
  return m_engine ? m_engine->playSoundAt(path, position(), volume) : 0;
}
