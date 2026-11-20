#pragma once
// Main engine class: initialization, game loop, and entity creation interface.

#include "components/animator.h"
#include "components/audioSource.h"
#include "components/characterController.h"
#include "components/rigidBody.h"
#include "components/script.h"
#include "components/light.h"
#include "components/skybox.h"
#include "components/transform.h"
#include "foundation/core/app.h"
#include "foundation/ecs/componentManager.h"
#include "foundation/ecs/entityManager.h"
#include "foundation/ecs/systemManager.h"
#include "systems/collisionSystem.h"
#include "systems/navigationSystem.h"
#include "systems/physicsSystem.h"
#include "systems/resourceSystem.h"
#include "systems/stateSystem.h"
#include <functional>
#include <limits>
#include <glm/glm.hpp>
#include <typeinfo>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace Rml {
class Context;
class ElementDocument;
} // namespace Rml
class SceneSerializer;
class InputSystem;
struct Camera;
class ScriptSystem;

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
  // The nearest Collision box along a ray, within maxDistance; see CollisionSystem::raycast
  std::optional<RayHit> raycast(const glm::vec3 &origin, const glm::vec3 &direction, float maxDistance,
                                const std::function<bool(Entity)> &accept = nullptr);
  // Only entities that have component T, e.g. raycast<Enemy>(muzzle, forward, 100.0f)
  template <typename T>
  std::optional<RayHit> raycast(const glm::vec3 &origin, const glm::vec3 &direction, float maxDistance) {
    return raycast(origin, direction, maxDistance, [this](Entity entity) { return has<T>(entity); });
  }
  // Picking: the ray from the active camera through a point of the window (window coordinates, like mouse
  // positions), and the nearest model it hits, e.g. pickModel(engine.input().getMousePosition())
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
  void playSound(std::string_view path, float volume = 1.0f, float pitch = 1.0f);
  // Fire-and-forget effect placed in the world, panned and faded relative to the camera
  void playSoundAt(std::string_view path, glm::vec3 position, float volume = 1.0f, float pitch = 1.0f);
  // Streams from disk, replacing the music already playing
  void playMusic(std::string_view path, float volume = 1.0f, bool loop = true);
  void stopMusic();

  // Scenes (JSON). Saving writes every entity with components that have a serializer; see SceneSerializer.
  bool saveScene(std::string_view path);
  // Replaces all entities with the file's, then calls App::onSceneLoaded. Its models are read on worker threads
  // first, so the current scene keeps running until they are ready; the switch happens at the end of that frame.
  void loadScene(std::string_view path);
  // Whether a loadScene is waiting for its models
  bool isLoadingScene() const { return m_pendingScene.has_value(); }
  // The file last loaded or saved, empty before that
  const std::string &getScenePath() const { return m_scenePath; }
  // Lets scene files hold game components: include "systems/sceneSerializer.h" and call registerComponent<T>(key)
  // on it. Kept out of this header, so files that include the engine do not compile the JSON library.
  SceneSerializer &getSceneSerializer();
  // Saves the scene after `delaySeconds` of running; `quitAfter` closes the engine
  void saveSceneAfter(std::string path, float delaySeconds, bool quitAfter = false);

  // Play mode, the editor's Play and Stop. Running is the default: the app updates as usual. Editing freezes the app
  // and the simulation (time stands still) while the camera and the editor keep working. play() saves every entity
  // and component (game components too) and runs the app from there; stop() puts them all back, calls
  // App::onPlayStopped and returns to Editing; edit() freezes without saving anything, resume() unfreezes. Each takes
  // effect at the start of the next frame.
  enum class PlayMode { Running, Editing, Testing };
  PlayMode getPlayMode() const { return m_playMode; }
  void edit() { m_requestedPlayMode = PlayMode::Editing; }
  void resume() { m_requestedPlayMode = PlayMode::Running; }
  void play() { m_requestedPlayMode = PlayMode::Testing; }
  void stop() { m_requestedPlayMode = PlayMode::Editing; }

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
  // parseCommandLine reads them (no window needed, so bad arguments can be reported first) and leaves the rest,
  // in order, to the game; applyCommandLine acts on them, after initialize() and before run().
  struct CommandLine {
    std::string screenshotPath;
    float screenshotDelay = 3.0f;
    float benchmarkSeconds = 0.0f;
    std::string saveScenePath;
    float saveSceneDelay = 0.0f;
    bool deterministic = false;
    std::vector<std::string> arguments; // everything else, argv[0] left out
  };
  static CommandLine parseCommandLine(int argc, char **argv);
  static const char *commandLineHelp();
  void applyCommandLine(const CommandLine &commandLine);

  void setWindowTitle(std::string_view title);

  // Material utilities
  // `submesh` of -1 applies to every submesh of the entity.
  void setEmission(Entity entity, glm::vec3 color, float strength, int submesh = -1);
  void setTexture(Entity entity, TextureSlot slot, std::string_view path, int submesh = -1);
  void setShininess(Entity entity, float shininess, int submesh = -1);
  void setSolidColor(Entity entity, glm::vec3 color, int submesh = -1);

  // Same, addressing a part by name (an OBJ object/group or glTF node); logs a warning if there is none
  void setEmission(Entity entity, glm::vec3 color, float strength, std::string_view part);
  void setTexture(Entity entity, TextureSlot slot, std::string_view path, std::string_view part);
  void setShininess(Entity entity, float shininess, std::string_view part);
  void setSolidColor(Entity entity, glm::vec3 color, std::string_view part);

private:
  float m_screenWidth;
  float m_screenHeight;
  uint32_t m_baseShaderHandle = 0;

  // Component access (private): builders and internals should use this.
  template <typename T, typename... Args> T &addComponent(Entity entity, Args &&...args) {
    return m_componentManager.addInPlace<T>(entity, std::forward<Args>(args)...);
  }

  EntityManager m_entityManager;
  ComponentManager m_componentManager;
  SystemManager m_systemManager;

  App *m_app = nullptr;
  bool m_shutdown = false;
  bool m_deterministic = false;

  struct ScreenshotRequest {
    std::string path;
    float delay = 0.0f;
    bool quitAfter = false;
  };
  std::optional<ScreenshotRequest> m_screenshot;
  std::optional<ScreenshotRequest> m_sceneSave;

  PlayMode m_playMode = PlayMode::Running;
  std::optional<PlayMode> m_requestedPlayMode;
  struct PlaySnapshot {
    ComponentManager::Snapshot components;
    EntityManager entities;
    Entity activeCamera = -1;
  };
  std::unique_ptr<PlaySnapshot> m_playSnapshot;
  void applyPlayModeRequest();
  std::optional<std::string> m_pendingScene;
  std::vector<std::string> m_pendingSceneModels;
  std::string m_scenePath;
  void applyPendingScene();
  bool m_screenshotKeyDown = false;
  bool m_quitRequested = false;
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
  void update(bool &isRunning);
  void render();
};
