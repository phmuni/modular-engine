#pragma once
// Main engine class: initialization, game loop, and entity creation interface.

#include "components/animator.h"
#include "components/audioSource.h"
#include "components/characterController.h"
#include "components/rigidBody.h"
#include "components/script.h"
#include "components/light.h"
#include "components/reflectionProbe.h"
#include "components/skybox.h"
#include "rendering/ssao.h"
#include "components/transform.h"
#include "foundation/core/app.h"
#include "foundation/core/collisionLayers.h"
#include "foundation/core/componentRegistry.h"
#include "foundation/core/gameEntity.h"
#include "foundation/ecs/componentManager.h"
#include "foundation/ecs/entityManager.h"
#include "foundation/ecs/systemManager.h"
#include "rendering/graphicsSettings.h"
#include "systems/collisionSystem.h"
#include "systems/gameCamera.h"
#include "systems/gameTime.h"
#include "systems/gameTween.h"
#include "systems/gameUI.h"
#include "systems/navigationSystem.h"
#include "systems/physicsSystem.h"
#include "systems/prefabFileEdit.h"
#include "systems/resourceSystem.h"
#include "systems/stateSystem.h"
#include <SDL3/SDL_events.h>
#include <deque>
#include <exception>
#include <functional>
#include <limits>
#include <glm/glm.hpp>
#include <typeinfo>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Rml {
class Context;
class ElementDocument;
} // namespace Rml
class SaveData;
class PlayerSettings;
class PauseMenu;
class MenuSystem;
class LoadingScreen;
class SceneSerializer;
class InputSystem;
struct Camera;
class ScriptSystem;

class Renderer;

class Engine {
public:
  class EntityBuilder {
  public:
    EntityBuilder(Engine &engine, std::string_view name);

    // Rotation in degrees (applied Y, then X, then Z); unrotated and at full size by default
    EntityBuilder &withModel(std::string_view modelPath, glm::vec3 position, glm::vec3 rotation = glm::vec3(0.0f),
                             glm::vec3 scale = glm::vec3(1.0f), uint32_t shaderHandle = 0);
    EntityBuilder &withCollision(const glm::vec3 &scale, bool isStatic = false);
    // A Collision box that hugs the model as placed (see Engine::fitCollisionToModel); needs withModel
    EntityBuilder &withModelCollision(float fit = 1.0f, bool isStatic = false);

    template <typename T, typename... Args> EntityBuilder &withComponent(Args &&...args) {
      auto values = std::make_tuple(std::forward<Args>(args)...);
      m_actions.emplace_back([&engine = m_engine, values = std::move(values)](Entity entity) mutable {
        std::apply(
            [&](auto &&...unpacked) { engine.addComponent<T>(entity, std::forward<decltype(unpacked)>(unpacked)...); },
            values);
      });
      return *this;
    }

    EntityBuilder &withTransform(glm::vec3 position, glm::vec3 rotation = glm::vec3(0.0f),
                                 glm::vec3 scale = glm::vec3(1.0f)) {
      return withComponent<Transform>(position, rotation, scale);
    }

    EntityBuilder &withEmission(const glm::vec3 &color, float strength);
    EntityBuilder &withSolidColor(const glm::vec3 &color);
    // A light, registered with the light system: Light::directional(...), Light::point(...) or Light::spot(...).
    // Point and spot lights sit at the entity's Transform (add withTransform), plus Light::position as an offset.
    EntityBuilder &withLight(const Light &light);
    // Plays from the entity's position; see AudioSource for the other settings
    EntityBuilder &withSound(std::string_view path, bool loop = true, float volume = 1.0f) {
      return withComponent<AudioSource>(std::string(path), volume, 1.0f, loop);
    }
    // A physics body (see RigidBody): its shape fits the model unless a size is given
    EntityBuilder &withRigidBody(BodyType type, BodyShape shape = BodyShape::Box, float mass = 0.0f) {
      RigidBody body;
      body.type = type;
      body.shape = shape;
      body.mass = mass;
      return withComponent<RigidBody>(body);
    }
    // A Lua script for its behavior (see Script), e.g. withScript("scripts/coin.lua")
    EntityBuilder &withScript(std::string_view path) { return withComponent<Script>(std::string(path)); }
    // Plays a clip of its animated model (see Animator); empty: the first
    EntityBuilder &withAnimation(std::string_view clip = {}, bool loop = true) {
      Animator animator;
      animator.clip = std::string(clip);
      animator.loop = loop;
      return withComponent<Animator>(animator);
    }
    // A walking character (see CharacterController), the Transform at the capsule's middle
    EntityBuilder &withCharacter(float radius = 0.35f, float height = 1.8f) {
      CharacterController controller;
      controller.radius = radius;
      controller.height = height;
      return withComponent<CharacterController>(controller);
    }
    // Follows `parent` (see components/parent.h); the position, rotation and scale given to withModel or
    // withTransform are then relative to the parent
    EntityBuilder &withParent(Entity parent) {
      m_parent = parent;
      return *this;
    }
    // A panorama behind the scene (see Skybox); the first one in the scene is drawn
    EntityBuilder &withSkybox(std::string_view path, float intensity = 1.0f, float rotationDegrees = 0.0f) {
      return withComponent<Skybox>(std::string(path), intensity, rotationDegrees);
    }
    Entity build();

  private:
    struct ModelConfig {
      std::string path;
      glm::vec3 position;
      glm::vec3 rotation;
      glm::vec3 scale;
      uint32_t shaderHandle = 0;
    };

    Engine &m_engine;
    std::string m_name;
    std::optional<ModelConfig> m_model;
    std::vector<std::function<void(Entity)>> m_actions;
    std::optional<Entity> m_entity;
    Entity m_parent = -1;
  };

  class EntityFactory {
  public:
    explicit EntityFactory(Engine &engine);
    EntityBuilder create(std::string_view name);

  private:
    Engine &m_engine;
  };

  Engine();
  ~Engine();

  void shutdown();

  bool initialize();
  void run(App &app);

  SystemManager &getSystemManager();
  ComponentManager &getComponentManager();
  EntityManager &getEntityManager();

  Entity createEntity();
  EntityFactory entities();
  // Removes an entity and all its components, unregistering its light or camera. Not while iterating the
  // components of that entity's type (forEachComponent): collect the entities first, then destroy them.
  void destroyEntity(Entity entity);
  // Hierarchy (see components/parent.h). setParent keeps the child where it is in the world (parent -1: follows
  // nothing any more) and returns false for a loop; setLocalTransform places a child relative to its parent (an
  // entity without one: in the world).
  bool setParent(Entity child, Entity parent);
  Entity getParent(Entity entity);
  std::vector<Entity> getChildren(Entity entity);
  void setLocalTransform(Entity entity, const Transform &local);

  // The first entity with this Name (as the scene file and the editor show it), or -1. A linear search: look entities
  // up once (in setup or onSceneLoaded) and keep the handles.
  Entity findEntity(std::string_view name);
  // Whether the entity exists: handles of destroyed entities (or of a scene that loadScene replaced) do not
  bool isAlive(Entity entity) const;
  // Whether the game created the entity while it ran (a script's engine.create or spawn, an App's create): it is not
  // part of the scene, so the editor's Save Scene leaves it out and Stop discards it (the game then starts over).
  // What the editor makes (add, duplicate, paste, drop) and what loadScene reads are the scene's; the parts of a
  // runtime prefab are runtime too. Undo and redo keep it. Not in the scene or prefab files.
  bool isRuntime(Entity entity) const;

  // The game API: what Lua scripts do, for C++ games too. The Lua bindings only convert types and call these, so
  // the two languages stay the same (tools/check_api_parity.py fails the build when one gets ahead).
  // A GameEntity is a cheap handle with the entity's own methods (position, move, destroy, setColor...).
  GameEntity entity(Entity entity);
  // The first entity named so, or an empty handle (valid() is false); all of them, by entity number
  GameEntity find(std::string_view name);
  std::vector<GameEntity> findAll(std::string_view name);
  // Every entity with the tag (see GameEntity::addTag), by entity number. Looks only at tagged entities, so it
  // stays cheap in a big scene; still, find them once and keep the handles rather than asking every frame.
  std::vector<GameEntity> findTagged(std::string_view tag);
  // Every entity with the component, named as GameEntity::has takes it ("Model", or a registered game component)
  std::vector<GameEntity> findWithComponent(std::string_view component);
  bool hasComponent(Entity entity, std::string_view component);
  // An entity from a description: a model (or the engine's box, sphere, cylinder), color, physics body, script...
  GameEntity create(std::string_view name, const EntityDesc &desc = {});
  // A copy of a prefab file (see instantiate), as a handle; empty if the file cannot be read
  GameEntity spawn(std::string_view prefabPath, glm::vec3 position = glm::vec3(0.0f),
                   glm::vec3 rotation = glm::vec3(0.0f));
  // Changing a game's UI, the active camera and time (the Lua ui, camera and time tables)
  GameUI &ui() { return m_ui; }
  GameCamera &camera() { return m_camera; }
  GameTime &time() { return m_time; }
  // Animating entities and numbers over time (the Lua tween table), on game time like timers
  GameTween &tween() { return m_tween; }

  // The color the image is cleared to, behind the skybox panorama (black until set). It lives in the scene, on the
  // first Skybox component (setSkyColor adds an entity named "Sky" with one when there is none), so scene files keep
  // it and the editor shows it.
  void setSkyColor(const glm::vec3 &color);
  glm::vec3 skyColor();
  // Fog and ambient light of the scene, kept in the same Skybox component as the sky color (so a scene file keeps
  // them, and the editor's Skybox panel edits them). setFog replaces all of the fog's settings; to change one,
  // start from fog(). With `matchSky` the fog takes the sky color, so distant things fade into the sky.
  void setFog(const Fog &fog);
  Fog fog();
  // Light added to everything lit: color times intensity, on top of each Light's own ambient (0 adds nothing)
  void setAmbient(const glm::vec3 &color, float intensity);
  glm::vec3 ambientColor();
  float ambientIntensity();
  // How strongly the sky lights the scene (the Skybox's image-based lighting: diffuse light and reflections from the
  // panorama, or evenly from the sky color without one); 1 by default, 0 turns it off
  void setEnvironmentIntensity(float intensity);
  float environmentIntensity();

  // Reflection probes (the ReflectionProbe component, see components/reflectionProbe.h and rendering/reflectionProbes.h):
  // static captures of a place that light the things inside their box, taken when the scene loads or read from a baked
  // file. Nothing happens per frame; these are the ways to take them again.
  // Captures every probe of the open scenes and writes each one's file (assets/probes/<scene file name>/<entity
  // name>.probe, or the probe's own bakedPath when it is an absolute path), setting its bakedPath: the scene changes, so
  // saving it keeps the path. Returns how many were baked. Writes nothing (0) while the editor is testing (Play).
  int bakeReflectionProbes();
  // The same for one entity; false if it is not a probe, the capture failed or the file could not be written
  bool bakeReflectionProbe(Entity entity);
  // Captures one probe again into memory only (no file is written, bakedPath stays): what a game does when something
  // in the room changed. The result lasts until the scene is loaded again. False if the entity is not a probe.
  bool refreshReflectionProbe(Entity entity);
  // Whether the probe's data (file or capture) is of another position, box or resolution than it has now
  bool isReflectionProbeStale(Entity entity);
  // Probes in use now (those with data, not hidden), and how many captures were made since the start
  int reflectionProbeCount();
  int reflectionProbeCaptures();

  // Component access. get throws, with a message saying whether the entity lacks the component or no longer
  // exists; tryGet returns null instead.
  template <typename T> T &get(Entity entity) {
    if (T *component = m_componentManager.tryGet<T>(entity))
      return *component;
    throwMissingComponent(entity, typeid(T));
  }

  template <typename T> T *tryGet(Entity entity) { return m_componentManager.tryGet<T>(entity); }

  template <typename T> bool has(Entity entity) {
    return m_componentManager.has<T>(entity);
  }

  template <typename T> void removeComponent(Entity entity) { m_componentManager.removeComponent<T>(entity); }

  // allow entity builder to add components without exposing this publicly
  friend class EntityBuilder;

  // Collisions between entities with Collision components, using the positions as they are when called (so move,
  // then ask). forEachCollision<A, B> visits each overlapping pair where one entity has A and the other has B, as
  // fn(Entity, A &, Entity, B &); a pair whose entity was destroyed by an earlier call is skipped.
  template <typename A, typename B, typename Fn> void forEachCollision(Fn &&fn) {
    const std::vector<CollisionEvent> pairs = findCollisions(); // a copy: fn may create or destroy entities
    for (const CollisionEvent &pair : pairs) {
      if (A *a = tryGet<A>(pair.a)) {
        if (B *b = tryGet<B>(pair.b)) {
          fn(pair.a, *a, pair.b, *b);
          continue;
        }
      }
      if (A *a = tryGet<A>(pair.b))
        if (B *b = tryGet<B>(pair.a))
          fn(pair.b, *a, pair.a, *b);
    }
  }
  // Every overlapping pair, each once
  const std::vector<CollisionEvent> &findCollisions();

  // Physics (Jolt): bodies come from RigidBody components and characters from CharacterController ones. physics()
  // gives velocities, impulses, gravity, raycasts against bodies and the contacts of the last update.
  PhysicsSystem &physics();
  // The collision layers and which of them collide: the names bodies, characters and Collision boxes are put on
  // (RigidBody::layer, GameEntity::setLayer), the matrix, and the masks raycasts take. Set them up in `setup`; a Lua
  // project's project.json does it for you. See CollisionLayers.
  CollisionLayers &layers() { return m_layers; }
  // Pathfinding over the walkable ground of the physics world: build() the map once the level exists, then
  // findPath(from, to) for a path around obstacles (see NavigationSystem)
  NavigationSystem &navigation();
  // Lua scripts: entity scripts are Script components; scripts().runGameScript(path) adds one for the whole game
  ScriptSystem &scripts();

  // Skeletal animation (glTF): plays a clip of the entity's model, fading over from the one playing for
  // `fadeSeconds`; adds an Animator if it has none. Playing the clip already playing keeps it going.
  void playAnimation(Entity entity, std::string_view clip, float fadeSeconds = 0.2f, bool loop = true);
  // The clips of the entity's model, in file order (empty for a model without animations)
  std::vector<std::string> getAnimationClips(Entity entity);

  // Animation state machines (see components/animationStateMachine.h and the README): setAnimator gives the entity the
  // machine in a JSON file (relative to assets/; adds an AnimationStateMachine and an Animator if needed) and starts it
  // in its default state. A file that is missing or invalid is logged and the machine does nothing. An empty path
  // removes the machine and leaves the Animator where it was. playAnimation on an entity with a machine takes the
  // Animator over until the machine's next state change.
  void setAnimator(Entity entity, std::string_view path);
  // A parameter's value: a float, or a bool (true is 1). One the file does not declare is a warning and is ignored.
  void setAnimationParam(Entity entity, std::string_view name, float value);
  void setAnimationParam(Entity entity, std::string_view name, bool value);
  // Raises a trigger parameter: the next transition that waits for it may fire, and uses it up
  void triggerAnimation(Entity entity, std::string_view name);
  // A parameter's value (0 for an unknown one)
  float getAnimationParam(Entity entity, std::string_view name);
  // The state its machine is in ("" without a machine or with a file that does not load)
  std::string getAnimationState(Entity entity);
  // Calls `listener` whenever a state machine moves to another state (not for its first, default state): the entity
  // and the names of the state it left and the one it entered. Lua scripts get onStateChanged(self, old, new).
  void onAnimationStateChanged(std::function<void(const GameEntity &, const std::string &, const std::string &)> listener);
  // Bodies that started touching during the last update, one of them with component A and the other with B (each
  // pair once, A's first), e.g. forEachContact<Player, Coin>
  template <typename A, typename B, typename Fn> void forEachContact(Fn &&fn) {
    const std::vector<PhysicsContact> contacts = physics().getContacts();
    for (const PhysicsContact &contact : contacts) {
      if (A *a = tryGet<A>(contact.a)) {
        if (B *b = tryGet<B>(contact.b)) {
          fn(contact.a, *a, contact.b, *b);
          continue;
        }
      }
      if (A *a = tryGet<A>(contact.b))
        if (B *b = tryGet<B>(contact.a))
          fn(contact.b, *a, contact.a, *b);
    }
  }
  // Pairs of bodies that stopped touching during the last update, one of them with component A and the other with B
  // (each pair once, A's first), e.g. forEachContactEnd<Player, Coin>. See PhysicsSystem::getContactEnds: nothing is
  // reported for an entity destroyed while touching.
  template <typename A, typename B, typename Fn> void forEachContactEnd(Fn &&fn) {
    const std::vector<PhysicsContactEnd> ends = physics().getContactEnds();
    for (const PhysicsContactEnd &end : ends) {
      if (A *a = tryGet<A>(end.a)) {
        if (B *b = tryGet<B>(end.b)) {
          fn(end.a, *a, end.b, *b);
          continue;
        }
      }
      if (A *a = tryGet<A>(end.b))
        if (B *b = tryGet<B>(end.a))
          fn(end.b, *a, end.a, *b);
    }
  }
  // The nearest Collision box along a ray, within maxDistance, on the layers in `layerMask` (all by default); see
  // CollisionSystem::raycast
  std::optional<RayHit> raycast(const glm::vec3 &origin, const glm::vec3 &direction, float maxDistance,
                                const std::function<bool(Entity)> &accept = nullptr,
                                uint32_t layerMask = CollisionLayers::kAllLayers);
  // The same with options: the layers and entities to ignore (`characters` is for physics queries, and does nothing
  // here)
  std::optional<RayHit> raycast(const glm::vec3 &origin, const glm::vec3 &direction, float maxDistance,
                                const QueryOptions &options);
  // Only entities that have component T, e.g. raycast<Enemy>(muzzle, forward, 100.0f)
  template <typename T>
  std::optional<RayHit> raycast(const glm::vec3 &origin, const glm::vec3 &direction, float maxDistance) {
    return raycast(origin, direction, maxDistance, [this](Entity entity) { return has<T>(entity); });
  }
  // Picking: the ray from the active camera through a point of the game view (the window's coordinates, like mouse
  // positions; in the editor, measured from the corner of its Game panel), and the nearest model it hits, e.g.
  // pickModel(engine.input().getMousePosition())
  struct Ray {
    glm::vec3 origin;
    glm::vec3 direction; // unit length
  };
  std::optional<Ray> screenRay(glm::vec2 windowPosition);
  // The nearest triangle of a visible model along a ray, with the surface normal there (see ModelQueries);
  // `accept` skips entities it returns false for
  std::optional<RayHit> raycastModels(const glm::vec3 &origin, const glm::vec3 &direction,
                                      float maxDistance = std::numeric_limits<float>::max(),
                                      const std::function<bool(Entity)> &accept = nullptr);
  std::optional<RayHit> pickModel(glm::vec2 windowPosition);

  // Sets the entity's Collision box (adding one if needed) to its model's bounds, as rotated and scaled by its
  // Transform. `fit` scales the box around its center: below 1 for forgiving hitboxes, above 1 for generous ones.
  // Call it again after changing the rotation or scale a lot; the box itself does not rotate.
  void fitCollisionToModel(Entity entity, float fit = 1.0f, bool isStatic = false);
  // Whether these two entities' boxes overlap now
  bool isColliding(Entity a, Entity b);

  // Entity creation utilities
  Entity createCameraEntity(std::string_view name, glm::vec3 position, float yaw = 0.0f, float pitch = 0.0f,
                            float fov = 90.0f, bool isActive = true);
  // The camera the scene renders from, or null; set it with Camera::lookAt, or its yaw/pitch then Camera::updateFront
  Camera *getActiveCamera();

  // Keyboard and mouse: isKeyDown while held, wasKeyPressed on the frame it went down, mouse buttons and position
  InputSystem &input();

  // Mouse capture, for mouse-look: captured, the cursor is hidden and held in the window, and InputSystem::
  // getMouseDelta keeps moving at the window's edge. It is the same switch as Settings' "Cursor lock" and its key
  // (Right Alt), so the editor and a person at the keyboard can still flip it; the pause menu frees the mouse while
  // it is open and puts back what the game asked for when it closes. The engine starts with it captured: a game
  // with menus to click releases it in setup. While captured the game UI and the editor get no mouse.
  void setMouseCaptured(bool captured);
  bool isMouseCaptured();
  // Hides or shows the cursor when it is not captured (a game that draws its own crosshair or cursor). It is shown
  // anyway while the pause menu is open and while the editor's panels are (F2), which need clicking.
  void setCursorVisible(bool visible) { m_cursorVisible = visible; }
  bool isCursorVisible() const { return m_cursorVisible; }

  // Reads a model on a worker thread so a later withModel/createModelEntity of it does not stall the frame
  void preloadModel(std::string_view modelPath);

  Entity createModelEntity(std::string_view name, std::string_view modelPath, glm::vec3 position, glm::vec3 rotation,
                           glm::vec3 scale, uint32_t shaderHandle = 0);

  // Prefer entities().create(name).withLight(Light::...): cutOff and outerCutOff here are cosines, used by spots only
  Entity createLightEntity(std::string_view name, glm::vec3 position, glm::vec3 direction, glm::vec3 color,
                           LightType type, float intensity, float cutOff, float outerCutOff);

  void setState(Toggle toggle, bool value);
  bool getState(Toggle toggle);

  // Audio (WAV, MP3, FLAC). Effects pause with the game and follow the time scale; music does not.
  // Decodes a sound in the background, so its first play does not wait for the file
  void preloadSound(std::string_view path);
  // Fire-and-forget effect, heard the same anywhere
  // The effect calls return a handle (0: nothing played) to stop or quiet it while it plays; one that ended is
  // simply not found by these.
  uint32_t playSound(std::string_view path, float volume = 1.0f, float pitch = 1.0f);
  // Fire-and-forget effect placed in the world, panned and faded relative to the camera
  uint32_t playSoundAt(std::string_view path, glm::vec3 position, float volume = 1.0f, float pitch = 1.0f);
  void stopSound(uint32_t sound);
  void setSoundVolume(uint32_t sound, float volume);
  bool isSoundPlaying(uint32_t sound);
  // Streams from disk, replacing the music already playing; returns a handle (0: nothing plays) like the effects do.
  // The calls below take it to leave newer music alone; without one (0) they mean the music playing now.
  uint32_t playMusic(std::string_view path, float volume = 1.0f, bool loop = true);
  void stopMusic(uint32_t music = 0);
  void setMusicVolume(float volume, uint32_t music = 0);
  // False once music that does not loop has ended
  bool isMusicPlaying(uint32_t music = 0);

  // Scenes (JSON). Saving writes every entity of the scene (not the ones the game created, see isRuntime) with
  // components that have a serializer; see SceneSerializer.
  //
  // Several scene files can be open at once (additive scenes, as in Unity). Every entity a scene file made, and every
  // one the editor makes, belongs to exactly one open scene; the entities the game creates belong to none (they are
  // not saved). One open scene is the active one: new editor entities (add, duplicate, paste, drop, prefab) go to it,
  // and getScenePath/saveScene mean it. A child is always in its parent's scene: parenting an entity under one of
  // another scene moves it (with what is below it) there. Entities of different scenes find each other by name
  // (findEntity), not by file-local links: a parent or a prefab link never crosses scenes.
  //
  // Saves the active scene's entities to `path`, and `path` is its file from then on (Save As); a path that is another
  // open scene's file is refused. Starts an untitled scene when none is open.
  bool saveScene(std::string_view path);
  // Saves one open scene (by the path it was opened or saved with) to its own file
  bool saveOpenScene(std::string_view path);
  // Saves every open scene that has a file; false if one failed or an untitled one (no file) has entities and was
  // skipped
  bool saveAllScenes();
  // Replaces all entities (of every open scene) with the file's, which becomes the only open scene, then calls
  // App::onSceneLoaded. Its models are read on worker threads first, so the current scene keeps running until they
  // are ready; the switch happens at the end of that frame. Clears the undo history.
  void loadScene(std::string_view path);
  // Opens one more scene next to the ones open, keeping their entities. Like loadScene (models first, entities made
  // at the end of the frame, isLoadingScene is true meanwhile) and it calls App::onSceneLoaded with this file's path
  // when done; loadedScenes() then holds it. Loading a file that is already open does nothing (it is logged). The
  // active scene and the active camera stay as they are: a camera this file marks active only takes the view when
  // there was none. Its collision layers merge with the ones in effect. Clears the undo history, like loadScene.
  void loadSceneAdditive(std::string_view path);
  // Destroys the scene's entities (and their children) and closes it, at the end of the frame. The last open scene
  // is never unloaded (loadScene replaces it). If it was the active one, the first still open becomes it; if it held
  // the active camera, the first camera of the open scenes takes over. False if no such scene is open, or it is the
  // only one. Clears the undo history.
  bool unloadScene(std::string_view path);
  // The open scenes' files in the order they were opened (an untitled scene, with no file yet, is ""), and the
  // active one's. getScenePath is the same as getActiveScene.
  std::vector<std::string> loadedScenes() const;
  const std::string &getActiveScene() const { return getScenePath(); }
  // False if that file is not open
  bool setActiveScene(std::string_view path);
  // Moves the entity and everything below it into an open scene (the editor's drag onto a scene in the Scene
  // Explorer). One that has a parent in another scene is detached first (a child lives in its parent's scene), keeping
  // its place in the world. False if the scene is not open or the entity is in none (the game's own).
  bool moveToScene(Entity entity, std::string_view path);
  // Replaces all entities (of every open scene) with a minimal starting scene (a camera and a directional light) that
  // has no file: the scene path is empty and nothing is unsaved. Takes effect at the end of the frame, like loadScene; the editor's
  // File > New Scene. App::onSceneLoaded is not called (there is no file).
  void newScene() { m_newScenePending = true; }
  // Whether a loadScene or loadSceneAdditive is waiting for its models (or for its turn: they are done in order)
  bool isLoadingScene() const;
  // How far the loading is, from 0 to 1: the share of the pending scenes' models that are read (1 when none is
  // loading). The
  // scene's own entities are made in the last frame, in one step, and textures upload as they are first drawn:
  // neither is counted.
  float sceneLoadProgress();
  // The active scene's file, as it was loaded or saved; empty for a scene with no file yet and before any
  const std::string &getScenePath() const;
  // Lets scene files hold game components: include "systems/sceneSerializer.h" and call registerComponent<T>(key)
  // on it. Kept out of this header, so files that include the engine do not compile the JSON library.
  SceneSerializer &getSceneSerializer();
  // Registers a game component by its fields (see componentRegistry.h): saved in scenes under `name`, editable in the
  // Properties panel and listed in Add Component. Chain .field("speed", &Player::speed) on the result.
  template <typename T> ComponentBuilder<T> registerComponent(std::string name) {
    return ComponentBuilder<T>(addReflectedComponent(makeReflectedComponent<T>(std::move(name))));
  }
  const std::deque<ReflectedComponent> &getReflectedComponents() const { return m_reflectedComponents; }
  // Saves the scene after `delaySeconds` of running, with the entities the game created (it is a picture of what the
  // game built); `quitAfter` closes the engine
  void saveSceneAfter(std::string path, float delaySeconds, bool quitAfter = false);

  // Play mode, the editor's Play, Pause and Stop. Running is the default: the app updates as usual. Editing freezes
  // the app and the simulation (time stands still) while the camera and the editor keep working. play() saves every
  // entity and component (game components too) and runs the app from there (Testing); pause() freezes that test
  // (Paused: like Editing, but the saved scene is kept, so edits made there are thrown away by stop()); resume()
  // continues it; stop() puts everything back, calls App::onPlayStopped and returns to Editing. Without a test,
  // pause() freezes a Running game without saving anything (Editing), and resume() unfreezes it. Each takes effect
  // at the start of the next frame.
  enum class PlayMode { Running, Editing, Testing, Paused };
  PlayMode getPlayMode() const { return m_playMode; }
  // The app and the clock stand still: the editor edits (Editing, Paused)
  bool isFrozen() const { return m_playMode == PlayMode::Editing || m_playMode == PlayMode::Paused; }
  // A test is under way and owns the scene: it is restored on stop() (Testing, Paused)
  bool isTesting() const { return m_playMode == PlayMode::Testing || m_playMode == PlayMode::Paused; }
  void pause() { m_requestedPlayMode = PlayMode::Paused; }
  void resume() { m_requestedPlayMode = PlayMode::Running; }
  void play() { m_requestedPlayMode = PlayMode::Testing; }
  void stop() { m_requestedPlayMode = PlayMode::Editing; }

  // Undo and redo for the editor: the scene (every entity and component, game components too) is copied before each
  // edit, and undo() / redo() put those copies back at the start of the next frame. A checkpoint covers one edit:
  // beginCheckpoint() copies the scene as it is, endCheckpoint() keeps the copy as an undo step only if the scene
  // changed meanwhile (compared as saved-scene text, see SceneSerializer::fingerprint), so selecting or opening a
  // menu leaves no empty step. A new step clears what could be redone. The last kMaxUndoSteps steps are kept; each
  // is a full copy of the scene, so memory grows with scene size times that.
  // Scripts are not restarted by undo (their Lua state stays; entity scripts follow their components as usual), the
  // viewport camera stays where it is, and the history survives Play and Stop but is not recorded or used while
  // testing. Loading a scene clears it.
  // An edit that writes a prefab file (Apply on a linked prefab, Save as prefab) is one step with the file: undo
  // writes its previous content back (or deletes a file the edit created) and redo writes it again, with the
  // instances following as the scene is restored. Edits made to the file outside the editor in between are lost.
  static constexpr size_t kMaxUndoSteps = 50;
  void beginCheckpoint();
  void endCheckpoint();
  bool hasOpenCheckpoint() const { return m_openCheckpoint != nullptr; }
  // Whether the editor changed the scene since it was last saved or loaded (what an undo step covers; changes the
  // game makes while running do not count)
  // (any open scene; per scene: the overload, and dirtyScenes). A change undone back to what is saved counts as none.
  bool isSceneDirty() const;
  bool isSceneDirty(std::string_view path) const;
  // The files of the open scenes with unsaved changes (an untitled one is "")
  std::vector<std::string> dirtyScenes() const;
  void undo() { m_historyRequest = HistoryRequest::Undo; }
  void redo() { m_historyRequest = HistoryRequest::Redo; }
  bool canUndo() const { return !isTesting() && !m_undoStack.empty(); }
  bool canRedo() const { return !isTesting() && !m_redoStack.empty(); }

  // Prefabs: an entity and its children saved to a file (the same format as scenes, with the entity at the origin)
  // and copied into the scene as often as needed. instantiate puts the copy's root at `position`, turned `rotation`
  // degrees, and returns it (-1 if the file cannot be read). Its models load on the spot: preloadModel them for a
  // prefab spawned during play.
  bool savePrefab(Entity root, std::string_view path);
  Entity instantiate(std::string_view path, glm::vec3 position = glm::vec3(0.0f), glm::vec3 rotation = glm::vec3(0.0f));

  // Game UI (RmlUi). Create data models on the context before loading the documents that bind to them.
  Rml::Context *getUIContext();
  // Loads an RML document (relative to bin/ like models) and shows it unless `show` is false
  Rml::ElementDocument *loadUIDocument(std::string_view path, bool show = true);
  // Lato (font-family: LatoLatin) is loaded by default; fonts are used by the family name inside the file
  bool loadUIFont(std::string_view path, bool fallback = false);

  // The pause menu (see systems/pauseMenu.h): a document of the project's (ui/pauseMenu.rml) that Escape opens once it
  // is enabled; the game pauses like with F7, and keys and clicks stay away from the game until it closes.
  // pauseMenu() has all of its options and callbacks; ui().pauseMenu(...) is the game API for the same.
  void enablePauseMenu(bool enabled = true);
  // pauseMenu().open(), close() and isOpen() work from code (a button of your own, a game-over screen), without
  // enablePauseMenu
  PauseMenu &pauseMenu();
  // Menus (systems/menu.h): any document the game shows as a menu; ui().menu(path, options) is the game API
  MenuSystem &menus();
  // The screen shown while a scene loads (systems/loadingScreen.h); ui().loadingScreen(...) is the game API
  LoadingScreen &loadingScreen();

  // The player's options (volumes, window size, graphics...): see systems/playerSettings.h. Saved choices are applied
  // when the game starts, before setup.
  PlayerSettings &settings();

  // Closes the game at the end of this frame
  void quit() { m_quitRequested = true; }

  // Saved data: progress and settings that stay between runs (see SaveData). The name picks the folder they are kept
  // in, so call setGameName once at start-up ("Game" until then; the Lua runner uses project.json's name). Called in
  // setup, it also applies the player's saved options (settings()) from that game's save, so call it first, before the
  // game's own defaults.
  void setGameName(std::string_view name);
  SaveData &saveData();

  // Repeatable runs for regression tests: fixed 1/60 s frames, seeded randomness, scene switches and texture
  // uploads that do not depend on background threads. Call before the app is set up.
  void setDeterministic();

  // Runs with vsync off for `seconds` (after a 1 s warm-up), logs frame-time statistics and closes the engine.
  void runBenchmark(float seconds);

  // Saves a BMP of the next frame. With a delay, waits that long after start-up; `quitAfter` closes the engine.
  void captureScreenshot(std::string path, float delaySeconds = 0.0f, bool quitAfter = false);

  // The options every game gets on its command line:
  //   --screenshot <file.bmp> [seconds]   save a frame after that long (3 s by default) and quit
  //   --benchmark [seconds]               log frame-time statistics (10 s by default) and quit
  //   --save-scene <file.json> [seconds]  save what the game built (after 0 s by default) and quit
  //   --deterministic                     fixed 1/60 s frames and seeded randomness, for repeatable runs
  //   --script <file.lua>                 run a Lua script beside the game (onStart, onUpdate, engine.run), e.g. to
  //                                       drive its menus in a smoke test; a script error is logged like any other
  // parseCommandLine reads them (no window needed, so bad arguments can be reported first) and leaves the rest,
  // in order, to the game; applyCommandLine acts on them, after initialize() and before run().
  struct CommandLine {
    std::string screenshotPath;
    float screenshotDelay = 3.0f;
    float benchmarkSeconds = 0.0f;
    std::string saveScenePath;
    float saveSceneDelay = 0.0f;
    bool deterministic = false;
    std::string scriptPath; // --script, absolute
    std::vector<std::string> arguments; // everything else, argv[0] left out
  };
  static CommandLine parseCommandLine(int argc, char **argv);
  static const char *commandLineHelp();
  void applyCommandLine(const CommandLine &commandLine);

  void setWindowTitle(std::string_view title);

  // Window and render resolution. Sizes are in window units (what SDL calls the window size: points on a Retina
  // screen, pixels elsewhere); the framebuffer behind the window has pixelDensity times as many pixels.
  struct WindowConfig {
    int width = 0; // 0 for either: the default size (DEFAULT_SCREEN_WIDTH x HEIGHT, grown on a scaled display)
    int height = 0;
    bool fullscreen = false;
    float renderScale = 1.0f;
  };
  // The game's window, as project.json's "window" describes it (the Lua runner reads it). Call it before initialize()
  // and the window is created at that size, with no visible resize; called later (in setup) it resizes the window.
  // What the player chose or left behind in an earlier run (the pause menu's options, the size and place the window
  // was closed at) still comes after it. Ignored by deterministic runs, which keep a fixed window and render scale.
  void configureWindow(const WindowConfig &config);
  // Resizes the window; the call returns once it has the new size. Does nothing for a size that is not positive. In
  // fullscreen it is the size the window takes when it leaves it.
  void setWindowSize(int width, int height);
  glm::ivec2 windowSize();
  // Window sizes that fit the display the window is on (usable area: no taskbar or menu bar, and room for the title
  // bar), smallest first: the common 16:9 sizes (the display's own is what fullscreen is for); on a display smaller
  // than all of them, the window's current size. Never empty.
  std::vector<glm::ivec2> resolutions();
  // The 3D scene is drawn at the framebuffer's pixels times this scale (0.5 to 2, clamped; 1 by default) and
  // stretched onto the window with linear filtering: below 1 is faster, above 1 sharper (supersampling). The editor
  // and the game UI keep drawing at the framebuffer's own size.
  void setRenderScale(float scale);
  float renderScale();
  // The scene's size in pixels now: the framebuffer's times the render scale
  glm::ivec2 renderResolution();
  // Antialiasing and upscaling (graphicsSettings.h). Defaults: MSAA 2x on, FXAA off, FSR off.
  // MSAA samples: 0 (off), 2, 4 or 8; other numbers snap down (1 and below are off) and the GPU's maximum caps it.
  // Changing it remakes the scene targets; msaa() is what is in use. The scene's size (renderResolution) is not
  // affected, and neither are shadow maps (single-sample). Cut-out edges (alpha-mask materials, custom shaders that
  // call cutBelow) are smoothed with alpha-to-coverage while it is on.
  void setMsaa(int samples);
  int msaa();
  // FXAA 3.11 on the tone mapped image, before the upscale and the final composite
  void setFxaa(bool enabled);
  bool fxaa();
  // AMD FSR 1 (EASU upscale, RCAS sharpening) in place of the linear stretch while the render scale is below 1; at 1
  // or above it does nothing
  void setFsr(bool enabled);
  bool fsr();
  // RCAS sharpness in stops, 0 (sharpest) to 2, clamped; AMD's default is 0.2
  void setFsrSharpness(float stops);
  float fsrSharpness();
  // The game's antialiasing and upscaling defaults, as project.json's "graphics" describes them (the Lua runner reads
  // it). Call it before initialize() or later (in setup); what the player chose in the pause menu still comes after.
  // Ignored by deterministic runs, which keep the defaults.
  void configureGraphics(const AntialiasingSettings &config);
  // Ambient occlusion (SSAO): contact shadows in the ambient light where objects meet and in creases (never in direct
  // lights or emission). On by default. setSsaoSettings clamps radius (0.05 to 10 world units) and intensity (0 to 4;
  // 0 is off); the scene's Skybox keeps radius and intensity (they come back from ssaoSettings), so a level carries
  // its own, and setSsaoSettings writes them into the scene's Skybox when there is one.
  void setSsao(bool enabled);
  bool ssao();
  void setSsaoSettings(const SsaoSettings &settings);
  SsaoSettings ssaoSettings();
  // Shows the AO alone instead of the image, for tuning in the editor (off by default)
  void setSsaoDebugView(bool enabled);
  bool ssaoDebugView();
  // Borderless fullscreen at the desktop's size; the window comes back at its old size
  void setFullscreen(bool enabled);
  bool isFullscreen();

  // Material utilities
  // `submesh` of -1 applies to every submesh of the entity.
  void setEmission(Entity entity, glm::vec3 color, float strength, int submesh = -1);
  void setTexture(Entity entity, TextureSlot slot, std::string_view path, int submesh = -1);
  void setMetallic(Entity entity, float metallic, int submesh = -1);
  void setRoughness(Entity entity, float roughness, int submesh = -1);
  void setSolidColor(Entity entity, glm::vec3 color, int submesh = -1);

  // Same, addressing a part by name (an OBJ object/group or glTF node); logs a warning if there is none
  void setEmission(Entity entity, glm::vec3 color, float strength, std::string_view part);
  void setTexture(Entity entity, TextureSlot slot, std::string_view path, std::string_view part);
  void setMetallic(Entity entity, float metallic, std::string_view part);
  void setRoughness(Entity entity, float roughness, std::string_view part);
  void setSolidColor(Entity entity, glm::vec3 color, std::string_view part);

private:
  // The Skybox the scene uses (the first visible one, made if there is none), or null
  Skybox &sceneSky();
  const Skybox *findSceneSky();

  float m_screenWidth;
  float m_screenHeight;
  uint32_t m_baseShaderHandle = 0;

  // Component access (private): builders and internals should use this.
  template <typename T, typename... Args> T &addComponent(Entity entity, Args &&...args) {
    return m_componentManager.addInPlace<T>(entity, std::forward<Args>(args)...);
  }

  CollisionLayers m_layers; // before the systems that read it
  EntityManager m_entityManager;
  ComponentManager m_componentManager;
  SystemManager m_systemManager;
  GameUI m_ui{*this};
  GameCamera m_camera{*this};
  GameTime m_time{*this};
  GameTween m_tween{*this};

  App *m_app = nullptr;
  bool m_shutdown = false;
  bool m_deterministic = false;
  // --deterministic was on the command line: known before the window exists, which setDeterministic comes too late for
  static inline bool s_deterministicRun = false;
  bool m_initialized = false;
  bool m_windowShown = false; // the window was put in place and shown: its size is worth remembering
  Renderer &renderer(); // the render system's
  AntialiasingSettings m_graphicsConfig; // from configureGraphics before initialize()
  WindowConfig m_windowConfig; // from configureWindow before initialize()
  // Puts the window where it was last time and shows it; the window starts hidden for this
  void startWindow();
  // Keeps the window's size and place for the next run
  void rememberWindow();

  struct ScreenshotRequest {
    std::string path;
    float delay = 0.0f;
    bool quitAfter = false;
  };
  std::optional<ScreenshotRequest> m_screenshot;
  std::optional<ScreenshotRequest> m_sceneSave;

  PlayMode m_playMode = PlayMode::Running;
  std::optional<PlayMode> m_requestedPlayMode;
  // Everything a scene is made of, for Play/Stop and the undo history
  struct SceneSnapshot {
    ComponentManager::Snapshot components;
    EntityManager entities;
    Entity activeCamera = -1;
    std::vector<uint32_t> materials; // the models' materials, kept alive while the snapshot exists
    // Undo history only: the prefab files the edit this step covers wrote (Apply). Undo puts their `before` back,
    // redo their `after`.
    std::vector<PrefabFileEdit> files;

    // Move-only (see ComponentManager::Snapshot), so the undo history's vectors move it rather than try to copy
    SceneSnapshot() = default;
    SceneSnapshot(SceneSnapshot &&) = default;
    SceneSnapshot &operator=(SceneSnapshot &&) = default;
    SceneSnapshot(const SceneSnapshot &) = delete;
    SceneSnapshot &operator=(const SceneSnapshot &) = delete;
  };
  SceneSnapshot captureScene();
  // Replaces the scene with the snapshot's and refreshes what depends on it (camera, lights, render caches, physics)
  // `dropRuntime`: Stop, which leaves out what the game had created before Play
  // `wholeScenes`: Stop, which also brings back which scenes are open and the active one (undo keeps those)
  void restoreScene(SceneSnapshot snapshot, bool dropRuntime = false, bool wholeScenes = false);
  // Everything (every scene, with `includeRuntime` the game's entities too) into one file, touching no scene's state
  bool writeScene(std::string_view path, bool includeRuntime);
  // Writes one open scene to `path` and makes it its file
  bool saveSceneTo(int scene, std::string_view path);
  // Keeps every material a snapshot (Play's, the history's, the open checkpoint's) refers to alive, and counted as
  // used, so changing one in the scene copies it first
  void refreshPinnedMaterials();
  std::unique_ptr<SceneSnapshot> m_playSnapshot;
  void applyPlayModeRequest();

  struct Checkpoint {
    SceneSnapshot snapshot;
    std::string fingerprint;
  };
  enum class HistoryRequest { Undo, Redo };
  std::unique_ptr<Checkpoint> m_openCheckpoint;
  std::deque<SceneSnapshot> m_undoStack; // oldest first; each is the scene before an edit
  std::vector<SceneSnapshot> m_redoStack;
  std::optional<HistoryRequest> m_historyRequest;
  uint64_t m_editRevision = 0; // counts undo steps made, undone and redone
  // Unsaved changes per open scene (by id): a hash of what its file would hold when it was loaded or saved; a scene
  // is dirty when it holds something else now. Looked at again when m_editRevision moved.
  std::unordered_map<int, size_t> m_savedHash;
  mutable bool m_dirtyValid = false;
  mutable uint64_t m_dirtyRevision = 0;
  mutable std::unordered_set<int> m_dirtyScenes;
  void markSceneClean(int scene);
  void refreshDirty() const;
  // Captures a reflection probe into memory, and with `write` to its baked file too (see bakeReflectionProbe)
  bool captureReflectionProbe(Entity entity, bool write);
  std::string reflectionProbeFile(Entity entity);
  static constexpr float kPrefabPollSeconds = 0.5f;
  float m_prefabPollTime = 0.0f;
  std::vector<std::function<void(const GameEntity &, const std::string &, const std::string &)>> m_animationListeners;
  float m_previewTime = 0.0f; // seconds in the editor with the game frozen, what custom shaders' `time` shows there
  void applyHistoryRequest();
  void clearHistory();
  // loadScene, loadSceneAdditive and unloadScene asked for, done in order at the end of a frame; a loadScene
  // drops the ones before it
  struct PendingScene {
    enum class Kind { Replace, Additive, Unload };
    Kind kind = Kind::Replace;
    std::string path;
    std::vector<std::string> models;
  };
  std::deque<PendingScene> m_pendingScenes;
  void queueScene(PendingScene::Kind kind, std::string_view path);
  bool m_newScenePending = false;
  std::deque<ReflectedComponent> m_reflectedComponents; // a deque: the builder and the serializer keep references
  ReflectedComponent &addReflectedComponent(ReflectedComponent component);
  void applyPendingScene();
  bool m_screenshotKeyDown = false;
  bool m_quitRequested = false;
  bool m_cursorVisible = true;
  uint64_t m_frameStartNs = 0;

  struct Benchmark {
    double duration = 10.0;
    double warmup = 1.0;
    double elapsed = 0.0;
    uint64_t lastFrame = 0;
    std::vector<double> frameMs;
  };
  std::optional<Benchmark> m_benchmark;
  void recordBenchmarkFrame();

  // Shows an error that is about to close the engine, so it does not just vanish
  void reportFatalError(const std::string &message);
  [[noreturn]] void throwMissingComponent(Entity entity, const std::type_info &type) const;
  void registerSystems();
  bool loadResources();

  void loop(bool &isRunning);
  // `pollInput` is false for a frame drawn from inside the OS's move/resize loop, where events must not be polled
  void update(bool &isRunning, bool pollInput = true);
  void render();

  // While the OS moves or resizes the window it runs its own loop inside the event pump, and the main loop is stuck.
  // SDL sends EXPOSED repeatedly from that loop; the watch draws a frame from it so the game keeps running.
  static bool SDLCALL onSdlEvent(void *userdata, SDL_Event *event);
  void runFrameInEventPump();
  bool m_loopRunning = false;
  bool m_inLiveFrame = false;
  std::exception_ptr m_liveFrameError; // thrown in the watch, which SDL's C code must not unwind through
};
