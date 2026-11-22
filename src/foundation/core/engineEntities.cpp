// Engine: entity creation (the builder and factory), components, hierarchy, animation and materials.

#include "foundation/core/engine.h"

#include "components/animationStateMachine.h"
#include "components/collision.h"
#include "components/hidden.h"
#include "components/joint.h"
#include "components/camera.h"
#include "components/cameraRig.h"
#include "components/model.h"
#include "components/name.h"
#include "components/parent.h"
#include "components/tags.h"
#include "foundation/core/config.h"
#include "foundation/core/logger.h"
#include "foundation/core/random.h"
#include "foundation/core/typeName.h"
#include <cstdint>
#include "rendering/resources/material.h"
#include "rendering/resources/textureProcessing.h"
#include "systems/animationGraph.h"
#include "systems/animationSystem.h"
#include "systems/scriptSystem.h"
#include "systems/audioSystem.h"
#include "systems/cameraSystem.h"
#include "systems/collisionSystem.h"
#include "systems/gameUISystem.h"
#include "systems/inputSystem.h"
#include "systems/lightSystem.h"
#include "systems/modelQueries.h"
#include "systems/navigationSystem.h"
#include "systems/particleSystem.h"
#include "systems/loadingScreen.h"
#include "systems/menu.h"
#include "systems/pauseMenu.h"
#include "systems/playerSettings.h"
#include "systems/renderSystem.h"
#include "systems/resourceSystem.h"
#include "systems/saveData.h"
#include "systems/sceneSerializer.h"
#include "systems/sceneSystem.h"
#include "systems/timeSystem.h"
#include "systems/transformSystem.h"
#include "systems/editorUISystem.h"
#include "components/particleEmitter.h"
#include "systems/windowSystem.h"
#include <algorithm>
#include <cstdlib>
#include <exception>
#include <utility>
#include <filesystem>
#include <limits>

Engine::EntityBuilder::EntityBuilder(Engine &engine, std::string_view name) : m_engine(engine), m_name(name) {}

Engine::EntityBuilder &Engine::EntityBuilder::withModel(std::string_view modelPath, glm::vec3 position,
                                                        glm::vec3 rotation, glm::vec3 scale, uint32_t shaderHandle) {
  m_model = ModelConfig{std::string(modelPath), position, rotation, scale, shaderHandle};
  return *this;
}

Engine::EntityBuilder &Engine::EntityBuilder::withCollision(const glm::vec3 &scale, bool isStatic) {
  m_actions.emplace_back([&engine = m_engine, scale, isStatic](Entity entity) {
    auto &col = engine.addComponent<Collision>(entity);
    col.min = -scale * 0.5f;
    col.max = scale * 0.5f;
    col.isStatic = isStatic;
  });
  return *this;
}

Engine::EntityBuilder &Engine::EntityBuilder::withModelCollision(float fit, bool isStatic) {
  m_actions.emplace_back([&engine = m_engine, fit, isStatic](Entity entity) {
    engine.fitCollisionToModel(entity, fit, isStatic);
  });
  return *this;
}

Engine::EntityBuilder &Engine::EntityBuilder::withEmission(const glm::vec3 &color, float strength) {
  m_actions.emplace_back(
      [&engine = m_engine, color, strength](Entity entity) { engine.setEmission(entity, color, strength); });
  return *this;
}

Engine::EntityBuilder &Engine::EntityBuilder::withLight(const Light &light) {
  m_actions.emplace_back([&engine = m_engine, light](Entity entity) {
    engine.addComponent<Light>(entity, light);
    engine.m_systemManager.getSystem<LightSystem>().createLight(entity);
  });
  return *this;
}

Engine::EntityBuilder &Engine::EntityBuilder::withSolidColor(const glm::vec3 &color) {
  m_actions.emplace_back([&engine = m_engine, color](Entity entity) { engine.setSolidColor(entity, color); });
  return *this;
}

Engine::EntityBuilder &Engine::EntityBuilder::withUnlit(bool unlit) {
  m_actions.emplace_back([&engine = m_engine, unlit](Entity entity) { engine.setUnlit(entity, unlit); });
  return *this;
}

Entity Engine::EntityBuilder::build() {
  if (!m_entity) {
    if (m_model) {
      const auto &model = *m_model;
      m_entity = m_engine.createModelEntity(m_name, model.path, model.position, model.rotation, model.scale,
                                            model.shaderHandle);
    } else {
      m_entity = m_engine.createEntity();
      m_engine.addComponent<Name>(*m_entity, m_name);
    }
  }

  for (auto &action : m_actions)
    action(*m_entity);
  m_actions.clear();

  // Last, once it has its Transform: what it was given becomes its place relative to the parent
  if (m_parent != -1) {
    if (const Transform *given = m_engine.tryGet<Transform>(*m_entity)) {
      const Transform local = *given;
      if (m_engine.setParent(*m_entity, m_parent))
        m_engine.setLocalTransform(*m_entity, local);
    }
    m_parent = -1;
  }

  return *m_entity;
}

Engine::EntityFactory::EntityFactory(Engine &engine) : m_engine(engine) {}

Engine::EntityBuilder Engine::EntityFactory::create(std::string_view name) { return EntityBuilder(m_engine, name); }

Entity Engine::createEntity() { return m_entityManager.createEntity(); }

Engine::EntityFactory Engine::entities() { return EntityFactory(*this); }

void Engine::destroyEntity(Entity entity) { m_systemManager.getSystem<SceneSystem>().destroyEntity(entity); }

bool Engine::isAlive(Entity entity) const { return m_entityManager.isAlive(entity); }

bool Engine::isRuntime(Entity entity) const { return m_entityManager.isRuntime(entity); }

void Engine::throwMissingComponent(Entity entity, const std::type_info &type) const {
  std::string message;
  if (!m_entityManager.isAlive(entity))
    message = "entity " + std::to_string(entity) + " no longer exists (destroyed, or its scene was replaced), so it " +
              "has no " + typeName(type);
  else
    message = "entity " + std::to_string(entity) + " has no " + typeName(type) + " component";
  LOG_E("[Engine] get: %s", message.c_str());
  throw std::runtime_error(message);
}

void Engine::playAnimation(Entity entity, std::string_view clip, float fadeSeconds, bool loop) {
  Animator *animator = tryGet<Animator>(entity);
  if (!animator) {
    animator = &addComponent<Animator>(entity);
    fadeSeconds = 0.0f; // nothing to fade from
  }
  const Model *model = tryGet<Model>(entity);
  const MeshAnimation *animation =
      model ? m_systemManager.getSystem<ResourceSystem>().getMesh(model->meshHandle).getAnimation() : nullptr;
  if (AnimationStateMachine *machine = tryGet<AnimationStateMachine>(entity))
    machine->manual = true; // the machine keeps its state but leaves the Animator alone until it changes state
  if (animation && !clip.empty() && animation->findClip(clip) < 0)
    LOG_W("[Engine] playAnimation: entity %d's model has no clip \"%.*s\"", entity, static_cast<int>(clip.size()),
          clip.data());
  const bool same = animator->clip == clip && animator->playing && animator->blendClip.empty();
  animator->loop = loop;
  animator->playing = true;
  if (same)
    return; // already playing it: keep going rather than restart
  AnimationSystem::crossfade(*animator, animation, std::string(clip), fadeSeconds, loop);
}

std::vector<std::string> Engine::getAnimationClips(Entity entity) {
  std::vector<std::string> names;
  const Model *model = tryGet<Model>(entity);
  if (const MeshAnimation *animation =
          model ? m_systemManager.getSystem<ResourceSystem>().getMesh(model->meshHandle).getAnimation() : nullptr)
    for (const AnimationClip &clip : animation->clips)
      names.push_back(clip.name);
  return names;
}

void Engine::setAnimator(Entity entity, std::string_view path) {
  if (!isAlive(entity))
    return;
  if (path.empty()) {
    removeComponent<AnimationStateMachine>(entity);
    return;
  }
  AnimationStateMachine *machine = tryGet<AnimationStateMachine>(entity);
  if (!machine)
    machine = &addComponent<AnimationStateMachine>(entity);
  machine->path = std::string(path);
  auto &animationSystem = m_systemManager.getSystem<AnimationSystem>();
  animationSystem.restartMachine(entity);
}

void Engine::setAnimationParam(Entity entity, std::string_view name, float value) {
  AnimationStateMachine *machine = tryGet<AnimationStateMachine>(entity);
  if (!machine) {
    LOG_W("[Engine] setAnimationParam: entity %d has no animation state machine", entity);
    return;
  }
  const auto graph = m_systemManager.getSystem<AnimationSystem>().graph(machine->path);
  // With a file that did not load there is nothing to check the name against: the value is kept for when it does
  if (graph && !graph->params.count(std::string(name))) {
    LOG_W("[Engine] setAnimationParam: %s has no parameter \"%.*s\"", machine->path.c_str(), static_cast<int>(name.size()),
          name.data());
    return;
  }
  machine->params[std::string(name)] = value;
}

void Engine::setAnimationParam(Entity entity, std::string_view name, bool value) {
  setAnimationParam(entity, name, value ? 1.0f : 0.0f);
}

void Engine::triggerAnimation(Entity entity, std::string_view name) { setAnimationParam(entity, name, 1.0f); }

float Engine::getAnimationParam(Entity entity, std::string_view name) {
  const AnimationStateMachine *machine = tryGet<AnimationStateMachine>(entity);
  if (!machine)
    return 0.0f;
  auto value = machine->params.find(std::string(name));
  return value != machine->params.end() ? value->second : 0.0f;
}

std::string Engine::getAnimationState(Entity entity) {
  if (!tryGet<AnimationStateMachine>(entity))
    return std::string();
  m_systemManager.getSystem<AnimationSystem>().startMachine(entity); // a scene's machine starts with the next frame
  return tryGet<AnimationStateMachine>(entity)->state;
}

void Engine::onAnimationStateChanged(
    std::function<void(const GameEntity &, const std::string &, const std::string &)> listener) {
  m_animationListeners.push_back(std::move(listener));
}

Entity Engine::createCameraEntity(std::string_view name, glm::vec3 position, float yaw, float pitch, float fov,
                                  bool isActive) {
  return m_systemManager.getSystem<SceneSystem>().createCameraEntity(std::string(name), position, yaw, pitch, fov,
                                                                     isActive);
}

void Engine::preloadModel(std::string_view modelPath) {
  m_systemManager.getSystem<ResourceSystem>().preloadModel(EngineConfig::resolveAsset(std::string(modelPath)));
}

Entity Engine::createModelEntity(std::string_view name, std::string_view modelPath, glm::vec3 position,
                                 glm::vec3 rotation, glm::vec3 scale, uint32_t shaderHandle) {
  uint32_t effectiveShader = shaderHandle == 0 ? m_baseShaderHandle : shaderHandle;
  return m_systemManager.getSystem<SceneSystem>().createModelEntity(
      std::string(name), EngineConfig::resolveAsset(std::string(modelPath)), effectiveShader, position, rotation, scale);
}

Entity Engine::createLightEntity(std::string_view name, glm::vec3 position, glm::vec3 direction, glm::vec3 color,
                                 LightType type, float intensity, float cutOff, float outerCutOff) {
  return m_systemManager.getSystem<SceneSystem>().createLightEntity(std::string(name), position, direction, color, type,
                                                                    intensity, cutOff, outerCutOff);
}

bool Engine::setParent(Entity child, Entity parent) {
  return m_systemManager.getSystem<TransformSystem>().setParent(m_componentManager, child, parent);
}

Entity Engine::getParent(Entity entity) {
  return m_systemManager.getSystem<TransformSystem>().getParent(m_componentManager, entity);
}

std::vector<Entity> Engine::getChildren(Entity entity) {
  return m_systemManager.getSystem<TransformSystem>().getChildren(m_componentManager, entity);
}

void Engine::setLocalTransform(Entity entity, const Transform &local) {
  m_systemManager.getSystem<TransformSystem>().setLocalTransform(m_componentManager, entity, local);
}

Entity Engine::findEntity(std::string_view name) {
  Entity found = -1;
  m_componentManager.forEachComponent<Name>([&](Entity entity, Name &entityName) {
    if (found == -1 && entityName.name == name)
      found = entity;
  });
  return found;
}

GameEntity Engine::entity(Entity entity) { return GameEntity(*this, entity); }

GameEntity Engine::find(std::string_view name) {
  const Entity found = findEntity(name);
  return found == -1 ? GameEntity() : GameEntity(*this, found);
}

std::vector<GameEntity> Engine::findAll(std::string_view name) {
  std::vector<Entity> found;
  m_componentManager.forEachComponent<Name>([&](Entity entity, Name &entityName) {
    if (entityName.name == name)
      found.push_back(entity);
  });
  std::sort(found.begin(), found.end());
  std::vector<GameEntity> handles;
  handles.reserve(found.size());
  for (Entity entity : found)
    handles.emplace_back(*this, entity);
  return handles;
}

bool Engine::hasComponent(Entity entity, std::string_view component) {
  if (component == "Model") return has<Model>(entity);
  if (component == "RigidBody") return has<RigidBody>(entity);
  if (component == "CharacterController") return has<CharacterController>(entity);
  if (component == "Light") return has<Light>(entity);
  if (component == "Camera") return has<Camera>(entity);
  if (component == "Animator") return has<Animator>(entity);
  if (component == "AnimationStateMachine") return has<AnimationStateMachine>(entity);
  if (component == "Script") return has<Script>(entity);
  if (component == "Transform") return has<Transform>(entity);
  if (component == "AudioSource") return has<AudioSource>(entity);
  if (component == "Skybox") return has<Skybox>(entity);
  if (component == "ReflectionProbe") return has<ReflectionProbe>(entity);
  if (component == "Joint") return has<Joint>(entity);
  if (component == "Collision") return has<Collision>(entity);
  if (component == "ParticleEmitter") return has<ParticleEmitter>(entity);
  if (component == "Hidden") return has<Hidden>(entity);
  if (component == "CameraRig") return has<CameraRig>(entity);
  if (component == "Tags") return has<Tags>(entity);
  for (const ReflectedComponent &reflected : m_reflectedComponents)
    if (reflected.name == component)
      return reflected.get && reflected.get(m_componentManager, entity) != nullptr;
  return false;
}

std::vector<GameEntity> Engine::findTagged(std::string_view tag) {
  std::vector<Entity> found;
  m_componentManager.forEachComponent<Tags>([&](Entity entity, Tags &tags) {
    if (std::find(tags.tags.begin(), tags.tags.end(), tag) != tags.tags.end())
      found.push_back(entity);
  });
  std::sort(found.begin(), found.end());
  std::vector<GameEntity> handles;
  handles.reserve(found.size());
  for (Entity entity : found)
    handles.emplace_back(*this, entity);
  return handles;
}

std::vector<GameEntity> Engine::findWithComponent(std::string_view component) {
  std::vector<Entity> found;
  for (Entity entity : m_entityManager.entities())
    if (hasComponent(entity, component))
      found.push_back(entity);
  std::sort(found.begin(), found.end());
  std::vector<GameEntity> handles;
  handles.reserve(found.size());
  for (Entity entity : found)
    handles.emplace_back(*this, entity);
  return handles;
}

GameEntity Engine::create(std::string_view name, const EntityDesc &desc) {
  auto builder = entities().create(name);
  // "box", "sphere" and "cylinder" are the engine's own models
  if (!desc.model.empty())
    builder.withModel(desc.model == "box"        ? std::string(EngineConfig::MODEL_BOX)
                      : desc.model == "sphere"   ? std::string(EngineConfig::MODEL_SPHERE)
                      : desc.model == "cylinder" ? std::string(EngineConfig::MODEL_CYLINDER)
                                                 : desc.model,
                      desc.position, desc.rotation, desc.scale);
  else
    builder.withTransform(desc.position, desc.rotation, desc.scale);
  if (desc.color)
    builder.withSolidColor(*desc.color);
  if (desc.emission)
    builder.withEmission(*desc.emission, desc.glow);
  if (desc.character) {
    if (desc.body)
      LOG_W("[Engine] create(\"%.*s\"): a character is its own physics body, so `body` is ignored",
            static_cast<int>(name.size()), name.data());
    builder.withComponent<CharacterController>(*desc.character);
  } else if (desc.body) {
    builder.withRigidBody(*desc.body, desc.shape, desc.mass);
  }
  if (desc.collision)
    builder.withModelCollision();
  if (!desc.script.empty())
    builder.withScript(desc.script);
  GameEntity created(*this, builder.build());
  if (!desc.layer.empty())
    created.setLayer(desc.layer);
  for (const std::string &tag : desc.tags)
    created.addTag(tag);
  for (const std::string &component : desc.components) {
    const auto reflected = std::find_if(m_reflectedComponents.begin(), m_reflectedComponents.end(),
                                        [&](const ReflectedComponent &candidate) { return candidate.name == component; });
    if (reflected != m_reflectedComponents.end() && reflected->add)
      reflected->add(m_componentManager, created.id());
    else
      LOG_W("[Engine] create(\"%.*s\"): no game component \"%s\" is registered (Engine::registerComponent), or it has no "
            "default constructor",
            static_cast<int>(name.size()), name.data(), component.c_str());
  }
  return created;
}

GameEntity Engine::spawn(std::string_view prefabPath, glm::vec3 position, glm::vec3 rotation) {
  const Entity spawned = instantiate(prefabPath, position, rotation);
  return spawned == -1 ? GameEntity() : GameEntity(*this, spawned);
}

void Engine::setEmission(Entity entity, glm::vec3 color, float strength, int submesh) {
  m_systemManager.getSystem<ResourceSystem>().setEmission(entity, submesh, color, strength);
}

void Engine::setTexture(Entity entity, TextureSlot slot, std::string_view path, int submesh) {
  m_systemManager.getSystem<ResourceSystem>().setTexture(entity, submesh, slot, path);
}

void Engine::setMetallic(Entity entity, float metallic, int submesh) {
  m_systemManager.getSystem<ResourceSystem>().setMetallic(entity, submesh, metallic);
}

void Engine::setRoughness(Entity entity, float roughness, int submesh) {
  m_systemManager.getSystem<ResourceSystem>().setRoughness(entity, submesh, roughness);
}

void Engine::setUnlit(Entity entity, bool unlit, int submesh) {
  m_systemManager.getSystem<ResourceSystem>().setUnlit(entity, submesh, unlit);
}

void Engine::setSolidColor(Entity entity, glm::vec3 color, int submesh) {
  m_systemManager.getSystem<ResourceSystem>().setSolidColor(entity, submesh, color);
}

void Engine::setEmission(Entity entity, glm::vec3 color, float strength, std::string_view part) {
  auto &resources = m_systemManager.getSystem<ResourceSystem>();
  if (int submesh = resources.findSubmesh(entity, part); submesh >= 0)
    resources.setEmission(entity, submesh, color, strength);
}

void Engine::setTexture(Entity entity, TextureSlot slot, std::string_view path, std::string_view part) {
  auto &resources = m_systemManager.getSystem<ResourceSystem>();
  if (int submesh = resources.findSubmesh(entity, part); submesh >= 0)
    resources.setTexture(entity, submesh, slot, path);
}

void Engine::setMetallic(Entity entity, float metallic, std::string_view part) {
  auto &resources = m_systemManager.getSystem<ResourceSystem>();
  if (int submesh = resources.findSubmesh(entity, part); submesh >= 0)
    resources.setMetallic(entity, submesh, metallic);
}

void Engine::setRoughness(Entity entity, float roughness, std::string_view part) {
  auto &resources = m_systemManager.getSystem<ResourceSystem>();
  if (int submesh = resources.findSubmesh(entity, part); submesh >= 0)
    resources.setRoughness(entity, submesh, roughness);
}

void Engine::setUnlit(Entity entity, bool unlit, std::string_view part) {
  auto &resources = m_systemManager.getSystem<ResourceSystem>();
  if (int submesh = resources.findSubmesh(entity, part); submesh >= 0)
    resources.setUnlit(entity, submesh, unlit);
}

void Engine::setSolidColor(Entity entity, glm::vec3 color, std::string_view part) {
  auto &resources = m_systemManager.getSystem<ResourceSystem>();
  if (int submesh = resources.findSubmesh(entity, part); submesh >= 0)
    resources.setSolidColor(entity, submesh, color);
}
