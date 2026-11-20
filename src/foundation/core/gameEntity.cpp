// The game API's entity handle: see gameEntity.h.

#include "foundation/core/gameEntity.h"

#include "components/characterController.h"
#include "components/hidden.h"
#include "components/light.h"
#include "components/model.h"
#include "components/name.h"
#include "components/tags.h"
#include "components/transform.h"
#include "foundation/core/engine.h"
#include "systems/renderSystem.h"
#include <algorithm>
#include <cmath>

bool GameEntity::valid() const { return m_engine && m_id >= 0 && m_engine->isAlive(m_id); }

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

void GameEntity::setColor(const glm::vec3 &color) {
  if (m_engine && m_engine->has<Model>(m_id))
    m_engine->setSolidColor(m_id, color);
}

void GameEntity::setEmission(const glm::vec3 &color, float strength) {
  if (m_engine && m_engine->has<Model>(m_id))
    m_engine->setEmission(m_id, color, strength);
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

void GameEntity::setTexture(const std::string &slot, const std::string &path) {
  if (!m_engine || !m_engine->has<Model>(m_id))
    return;
  const TextureSlot textureSlot = slot == "specular"   ? TextureSlot::Specular
                                  : slot == "normal"   ? TextureSlot::Normal
                                  : slot == "emission" ? TextureSlot::Emission
                                                       : TextureSlot::Diffuse;
  m_engine->setTexture(m_id, textureSlot, path);
}

void GameEntity::setShininess(float shininess) {
  if (m_engine && m_engine->has<Model>(m_id))
    m_engine->setShininess(m_id, shininess);
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

uint32_t GameEntity::sound(const std::string &path, float volume) {
  return m_engine ? m_engine->playSoundAt(path, position(), volume) : 0;
}
