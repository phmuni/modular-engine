// Lua scripting with sol2: script instances per entity, hot reload, and the API scripts see.

#include <sol/sol.hpp> // SOL_ALL_SAFETIES_ON comes from the build, the same for every file that includes sol2

#include "systems/scriptSystem.h"

#include "components/animator.h"
#include "components/camera.h"
#include "components/characterController.h"
#include "components/hidden.h"
#include "components/light.h"
#include "components/model.h"
#include "components/name.h"
#include "components/rigidBody.h"
#include "components/script.h"
#include "components/transform.h"
#include "foundation/core/config.h"
#include "foundation/core/engine.h"
#include "foundation/core/logger.h"
#include "systems/inputSystem.h"
#include "systems/physicsSystem.h"
#include "systems/renderSystem.h"
#include <RmlUi/Core.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <unordered_map>
#include <vector>

namespace {
// Paths in scripts are relative to the assets folder ("sounds/laser.wav"); "../..." and absolute ones are kept
std::string assetPath(const std::string &path) {
  if (path.empty() || path.rfind("../", 0) == 0 || std::filesystem::path(path).is_absolute())
    return path;
  return "../assets/" + path;
}

// "scripts/coin.lua" for ".../assets/scripts/coin.lua"
std::string displayName(const std::string &path) {
  const size_t assets = path.rfind("/assets/");
  return assets == std::string::npos ? path : path.substr(assets + 8);
}

std::filesystem::file_time_type modifiedTime(const std::string &path) {
  std::error_code error;
  const auto time = std::filesystem::last_write_time(path, error);
  return error ? std::filesystem::file_time_type{} : time;
}

// What scripts hold for an entity: its handle; fields set on it live in the ScriptSystem (see Impl::fields)
struct ScriptEntity {
  Entity id = -1;
  friend bool operator==(const ScriptEntity &a, const ScriptEntity &b) { return a.id == b.id; }
};

// A click handler from ui.onClick
class ClickListener final : public Rml::EventListener {
public:
  ClickListener(sol::protected_function handler, std::function<void(const sol::protected_function_result &)> report)
      : m_handler(std::move(handler)), m_report(std::move(report)) {}
  void ProcessEvent(Rml::Event &) override { m_report(m_handler()); }

private:
  sol::protected_function m_handler;
  std::function<void(const sol::protected_function_result &)> m_report;
};
} // namespace

struct ScriptSystem::Impl {
  Engine &engine;
  sol::state lua;
  sol::table gameTable; // the shared `game` table

  // A loaded script: its file's functions live in `environment` (falling back to the globals)
  struct Instance {
    std::string path; // resolved
    std::string written; // Script::path as given, to notice when it changes
    sol::environment environment;
    std::filesystem::file_time_type loadedTime{};
    bool started = false;
    bool broken = false; // failed to load or run: waits for the file to change
  };
  std::unordered_map<Entity, Instance> instances;
  Instance gameScript;
  bool hasGameScript = false;
  std::string gameScriptPath;

  // Each entity's own Lua fields (self.speed = 5), kept apart from its script so reloading keeps them
  std::unordered_map<Entity, sol::table> fields;
  std::vector<std::unique_ptr<ClickListener>> clickListeners;
  std::unordered_map<std::string, Rml::ElementDocument *> documents; // ui.load, by path

  std::string lastError;
  std::string lastErrorPath;
  float reloadCheck = 0.0f;

  explicit Impl(Engine &e) : engine(e) {
    lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::string, sol::lib::table, sol::lib::os,
                       sol::lib::io, sol::lib::coroutine, sol::lib::utf8, sol::lib::package);
    // require("enemies") finds assets/scripts/enemies.lua
    const std::string scripts = EngineConfig::resolvePath("../assets/scripts");
    lua["package"]["path"] = scripts + "/?.lua;" + scripts + "/?/init.lua";
    gameTable = lua.create_named_table("game");
    bindApi();
  }

  // --- Errors ---------------------------------------------------------------------------------------------------

  void report(const std::string &path, const std::string &message) {
    lastError = message;
    lastErrorPath = path;
    LOG_E("[Script] %s", message.c_str());
  }

  // Runs a script function if the script defines it; false (and reported) if it raised an error
  template <typename... Args> bool call(Instance &instance, const char *name, Args &&...args) {
    if (instance.broken)
      return false;
    sol::object function = instance.environment.raw_get<sol::object>(name);
    if (!function.is<sol::protected_function>())
      return true;
    sol::protected_function_result result = function.as<sol::protected_function>()(std::forward<Args>(args)...);
    if (result.valid())
      return true;
    const sol::error error = result;
    report(instance.path, error.what());
    instance.broken = true;
    return false;
  }

  // --- Loading --------------------------------------------------------------------------------------------------

  bool load(Instance &instance) {
    instance.loadedTime = modifiedTime(instance.path);
    if (!std::filesystem::exists(instance.path)) {
      report(instance.path, displayName(instance.path) + ": file not found");
      instance.broken = true;
      return false;
    }
    if (!instance.environment.valid())
      instance.environment = sol::environment(lua, sol::create, lua.globals());
    // Read here rather than by Lua, to name it by its short path in error messages ("scripts/coin.lua:12: ...")
    std::ifstream file(instance.path, std::ios::binary);
    const std::string source((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    sol::load_result chunk = lua.load(source, "@" + displayName(instance.path));
    if (!chunk.valid()) {
      const sol::error error = chunk;
      report(instance.path, error.what());
      instance.broken = true;
      return false;
    }
    sol::protected_function body = chunk;
    sol::set_environment(instance.environment, body);
    sol::protected_function_result result = body();
    if (!result.valid()) {
      const sol::error error = result;
      report(instance.path, error.what());
      instance.broken = true;
      return false;
    }
    instance.broken = false;
    if (lastErrorPath == instance.path)
      lastError.clear(); // this file is fine again
    return true;
  }

  // Scripts whose file changed are loaded again: their functions are replaced, entities keep their fields
  void reloadChanged() {
    auto reload = [&](Instance &instance, const sol::object &self) {
      if (modifiedTime(instance.path) == instance.loadedTime)
        return;
      LOG_I("[Script] Reloaded %s", displayName(instance.path).c_str());
      if (load(instance) && instance.started)
        call(instance, "onReload", self);
    };
    for (auto &[entity, instance] : instances)
      reload(instance, sol::make_object(lua, ScriptEntity{entity}));
    if (hasGameScript)
      reload(gameScript, sol::lua_nil);
  }

  // --- Entities -------------------------------------------------------------------------------------------------

  sol::table &fieldsOf(Entity entity) {
    auto it = fields.find(entity);
    if (it == fields.end())
      it = fields.emplace(entity, lua.create_table()).first;
    return it->second;
  }

  sol::object entityObject(Entity entity) {
    return entity >= 0 && engine.isAlive(entity) ? sol::make_object(lua, ScriptEntity{entity}) : sol::lua_nil;
  }

  // Instances for entities that got a Script, gone for those that lost it (or were destroyed)
  void syncInstances() {
    for (auto it = instances.begin(); it != instances.end();) {
      const Script *script = engine.tryGet<Script>(it->first);
      if (!script || script->path != it->second.written)
        it = instances.erase(it);
      else
        ++it;
    }
    for (auto it = fields.begin(); it != fields.end();)
      it = engine.isAlive(it->first) ? std::next(it) : fields.erase(it);

    engine.getComponentManager().forEachComponent<Script>([&](Entity entity, Script &script) {
      if (script.path.empty() || instances.count(entity))
        return;
      Instance &instance = instances[entity];
      instance.written = script.path;
      instance.path = EngineConfig::resolvePath(assetPath(script.path));
      load(instance);
    });
  }

  void update(float deltaTime) {
    reloadCheck -= deltaTime > 0.0f ? deltaTime : 1.0f / 60.0f;
    if (reloadCheck <= 0.0f) {
      reloadCheck = 0.5f;
      reloadChanged();
    }
    syncInstances();

    if (hasGameScript && !gameScript.started) {
      gameScript.started = true;
      call(gameScript, "onStart");
    }
    // A copy of the list: scripts may create and destroy entities (with scripts) while running
    std::vector<Entity> entities;
    entities.reserve(instances.size());
    for (auto &[entity, instance] : instances)
      entities.push_back(entity);
    std::sort(entities.begin(), entities.end());

    for (Entity entity : entities) {
      auto it = instances.find(entity);
      if (it == instances.end() || !engine.isAlive(entity))
        continue;
      if (!it->second.started) {
        it->second.started = true;
        call(it->second, "onStart", ScriptEntity{entity});
      }
    }
    if (hasGameScript)
      call(gameScript, "onUpdate", deltaTime);
    for (Entity entity : entities) {
      auto it = instances.find(entity);
      if (it != instances.end() && engine.isAlive(entity))
        call(it->second, "onUpdate", ScriptEntity{entity}, deltaTime);
    }

    // Contacts and overlaps, to both entities' scripts
    auto notify = [&](Entity self, Entity other, const char *name, auto &&...extra) {
      auto it = instances.find(self);
      if (it != instances.end() && engine.isAlive(self) && engine.isAlive(other))
        call(it->second, name, ScriptEntity{self}, ScriptEntity{other}, extra...);
    };
    const std::vector<PhysicsContact> contacts = engine.physics().getContacts();
    for (const PhysicsContact &contact : contacts) {
      notify(contact.a, contact.b, "onContact", contact.point, contact.normal);
      notify(contact.b, contact.a, "onContact", contact.point, -contact.normal);
    }
    if (!instances.empty()) {
      const std::vector<CollisionEvent> overlaps = engine.findCollisions();
      for (const CollisionEvent &pair : overlaps) {
        notify(pair.a, pair.b, "onOverlap");
        notify(pair.b, pair.a, "onOverlap");
      }
    }
  }

  // --- The API --------------------------------------------------------------------------------------------------

  void bindApi() {
    bindMath();
    bindEntity();
    bindGlobals();
  }

  void bindMath() {
    lua.new_usertype<glm::vec3>(
        "vec3",
        sol::call_constructor,
        sol::factories([] { return glm::vec3(0.0f); }, [](float v) { return glm::vec3(v); },
                       [](float x, float y, float z) { return glm::vec3(x, y, z); }),
        "x", &glm::vec3::x, "y", &glm::vec3::y, "z", &glm::vec3::z,
        sol::meta_function::addition, [](const glm::vec3 &a, const glm::vec3 &b) { return a + b; },
        sol::meta_function::subtraction, [](const glm::vec3 &a, const glm::vec3 &b) { return a - b; },
        sol::meta_function::multiplication,
        sol::overload([](const glm::vec3 &a, float s) { return a * s; },
                      [](float s, const glm::vec3 &a) { return a * s; },
                      [](const glm::vec3 &a, const glm::vec3 &b) { return a * b; }),
        sol::meta_function::division, [](const glm::vec3 &a, float s) { return a / s; },
        sol::meta_function::unary_minus, [](const glm::vec3 &a) { return -a; },
        sol::meta_function::equal_to, [](const glm::vec3 &a, const glm::vec3 &b) { return a == b; },
        sol::meta_function::to_string,
        [](const glm::vec3 &v) {
          char text[96];
          std::snprintf(text, sizeof(text), "vec3(%.3f, %.3f, %.3f)", v.x, v.y, v.z);
          return std::string(text);
        },
        "length", [](const glm::vec3 &v) { return glm::length(v); },
        "normalized",
        [](const glm::vec3 &v) {
          const float length = glm::length(v);
          return length > 0.0f ? v / length : v;
        },
        "dot", [](const glm::vec3 &a, const glm::vec3 &b) { return glm::dot(a, b); },
        "cross", [](const glm::vec3 &a, const glm::vec3 &b) { return glm::cross(a, b); },
        "distance", [](const glm::vec3 &a, const glm::vec3 &b) { return glm::distance(a, b); },
        "lerp", [](const glm::vec3 &a, const glm::vec3 &b, float t) { return glm::mix(a, b, t); });

    lua.new_usertype<Transform>("Transform", sol::no_constructor, "position", &Transform::position, "rotation",
                                &Transform::rotation, "scale", &Transform::scale);
    lua.new_usertype<Light>("Light", sol::no_constructor, "color", &Light::color, "intensity", &Light::intensity);
  }

  void bindEntity() {
    auto transform = [this](ScriptEntity &e) { return engine.tryGet<Transform>(e.id); };
    lua.new_usertype<ScriptEntity>(
        "Entity", sol::no_constructor,
        sol::meta_function::equal_to, [](const ScriptEntity &a, const ScriptEntity &b) { return a.id == b.id; },
        sol::meta_function::to_string,
        [this](const ScriptEntity &e) {
          const Name *name = engine.tryGet<Name>(e.id);
          return "Entity " + std::to_string(e.id) + (name ? " (" + name->name + ")" : "");
        },
        // Unknown keys are the entity's own fields: self.speed = 5, other.health
        sol::meta_function::index,
        [this](ScriptEntity &e, sol::stack_object key) -> sol::object { return fieldsOf(e.id)[key]; },
        sol::meta_function::new_index,
        [this](ScriptEntity &e, sol::stack_object key, sol::stack_object value) { fieldsOf(e.id)[key] = value; },

        "id", sol::readonly_property([](ScriptEntity &e) { return e.id; }),
        "valid", sol::readonly_property([this](ScriptEntity &e) { return engine.isAlive(e.id); }),
        "name", sol::property(
                    [this](ScriptEntity &e) {
                      const Name *name = engine.tryGet<Name>(e.id);
                      return name ? name->name : std::string();
                    },
                    [this](ScriptEntity &e, const std::string &name) {
                      if (Name *existing = engine.tryGet<Name>(e.id))
                        existing->name = name;
                    }),
        "transform", sol::readonly_property(transform),
        "position", sol::property(
                        [this](ScriptEntity &e) {
                          const Transform *t = engine.tryGet<Transform>(e.id);
                          return t ? t->position : glm::vec3(0.0f);
                        },
                        [this](ScriptEntity &e, const glm::vec3 &position) {
                          if (Transform *t = engine.tryGet<Transform>(e.id))
                            t->position = position;
                        }),
        "rotation", sol::property(
                        [this](ScriptEntity &e) {
                          const Transform *t = engine.tryGet<Transform>(e.id);
                          return t ? t->rotation : glm::vec3(0.0f);
                        },
                        [this](ScriptEntity &e, const glm::vec3 &rotation) {
                          if (Transform *t = engine.tryGet<Transform>(e.id))
                            t->rotation = rotation;
                        }),
        "forward", sol::readonly_property([this](ScriptEntity &e) {
          // -Z turned by the entity's yaw
          const Transform *t = engine.tryGet<Transform>(e.id);
          const float yaw = t ? glm::radians(t->rotation.y) : 0.0f;
          return glm::vec3(-std::sin(yaw), 0.0f, -std::cos(yaw));
        }),
        "light", sol::readonly_property([this](ScriptEntity &e) { return engine.tryGet<Light>(e.id); }),
        "script", sol::readonly_property([this](ScriptEntity &e) -> sol::object {
          return engine.has<Script>(e.id) ? sol::make_object(lua, e) : sol::lua_nil;
        }),

        "destroy", [this](ScriptEntity &e) {
          if (engine.isAlive(e.id))
            engine.destroyEntity(e.id);
        },
        // Engine components by name: "Model", "RigidBody", "CharacterController", "Light", "Camera", "Animator",
        // "Script"
        "has", [this](ScriptEntity &e, const std::string &component) {
          if (component == "Model") return engine.has<Model>(e.id);
          if (component == "RigidBody") return engine.has<RigidBody>(e.id);
          if (component == "CharacterController") return engine.has<CharacterController>(e.id);
          if (component == "Light") return engine.has<Light>(e.id);
          if (component == "Camera") return engine.has<Camera>(e.id);
          if (component == "Animator") return engine.has<Animator>(e.id);
          if (component == "Script") return engine.has<Script>(e.id);
          if (component == "Transform") return engine.has<Transform>(e.id);
          return false;
        },
        "parent", [this](ScriptEntity &e) { return entityObject(engine.getParent(e.id)); },
        "setParent", [this](ScriptEntity &e, sol::optional<ScriptEntity> parent) {
          return engine.setParent(e.id, parent ? parent->id : -1);
        },
        "children", [this](ScriptEntity &e) {
          sol::table list = lua.create_table();
          for (Entity child : engine.getChildren(e.id))
            list.add(ScriptEntity{child});
          return list;
        },

        // Looks
        "setColor", [this](ScriptEntity &e, const glm::vec3 &color) {
          if (engine.has<Model>(e.id))
            engine.setSolidColor(e.id, color);
        },
        "setEmission", [this](ScriptEntity &e, const glm::vec3 &color, sol::optional<float> strength) {
          if (engine.has<Model>(e.id))
            engine.setEmission(e.id, color, strength.value_or(1.0f));
        },
        "setVisible", [this](ScriptEntity &e, bool visible) {
          const bool hidden = engine.has<Hidden>(e.id);
          if (visible == !hidden)
            return;
          if (visible)
            engine.removeComponent<Hidden>(e.id);
          else
            engine.getComponentManager().addInPlace<Hidden>(e.id);
          engine.getSystemManager().getSystem<RenderSystem>().markBatchesDirty();
        },

        // Physics bodies
        "velocity", sol::property([this](ScriptEntity &e) { return engine.physics().getLinearVelocity(e.id); },
                                  [this](ScriptEntity &e, const glm::vec3 &velocity) {
                                    engine.physics().setLinearVelocity(e.id, velocity);
                                  }),
        "impulse", [this](ScriptEntity &e, const glm::vec3 &impulse) { engine.physics().addImpulse(e.id, impulse); },
        "force", [this](ScriptEntity &e, const glm::vec3 &force) { engine.physics().addForce(e.id, force); },

        // Characters
        "move", [this](ScriptEntity &e, const glm::vec3 &velocity) {
          if (CharacterController *character = engine.tryGet<CharacterController>(e.id))
            character->move = velocity;
        },
        "jump", [this](ScriptEntity &e, float speed) {
          if (CharacterController *character = engine.tryGet<CharacterController>(e.id))
            character->jump = speed;
        },
        "onGround", sol::readonly_property([this](ScriptEntity &e) {
          const CharacterController *character = engine.tryGet<CharacterController>(e.id);
          return character && character->onGround;
        }),

        // Animation and sound
        "play", [this](ScriptEntity &e, const std::string &clip, sol::optional<float> fade, sol::optional<bool> loop) {
          engine.playAnimation(e.id, clip, fade.value_or(0.2f), loop.value_or(true));
        },
        "clips", [this](ScriptEntity &e) { return sol::as_table(engine.getAnimationClips(e.id)); },
        "sound", [this](ScriptEntity &e, const std::string &path, sol::optional<float> volume) {
          const Transform *t = engine.tryGet<Transform>(e.id);
          engine.playSoundAt(assetPath(path), t ? t->position : glm::vec3(0.0f), volume.value_or(1.0f));
        });
  }

  void bindGlobals() {
    // print goes to the engine's log
    lua.set_function("print", [](sol::variadic_args args, sol::this_state state) {
      sol::state_view view(state);
      std::string line;
      for (auto arg : args) {
        if (!line.empty())
          line += "  ";
        line += view["tostring"](arg.get<sol::object>()).get<std::string>();
      }
      LOG_I("[Lua] %s", line.c_str());
    });

    // engine: entities, scenes, time
    sol::table api = lua.create_named_table("engine");
    api.set_function("find", [this](const std::string &name) { return entityObject(engine.findEntity(name)); });
    api.set_function("spawn", [this](const std::string &prefab, sol::optional<glm::vec3> position,
                                     sol::optional<glm::vec3> rotation) {
      return entityObject(engine.instantiate(assetPath(prefab), position.value_or(glm::vec3(0.0f)),
                                             rotation.value_or(glm::vec3(0.0f))));
    });
    // engine.create("Coin", { model = "models/coin.glb", position = vec3(0, 1, 0), scale = vec3(0.5),
    //                         color = vec3(1, 0.8, 0.2), body = "dynamic", shape = "sphere", script = "scripts/coin.lua" })
    // engine.create(name, { model = "box" | "sphere" | path, position, rotation, scale, color, emission, glow,
    //                       body = "static" | "dynamic" | "kinematic", shape, mass, collision = true, script })
    api.set_function("create", [this](const std::string &name, sol::optional<sol::table> options) {
      sol::table o = options ? *options : lua.create_table();
      auto vectorOr = [&](const char *key, glm::vec3 fallback) {
        sol::optional<glm::vec3> value = o[key];
        return value ? *value : fallback;
      };
      const glm::vec3 position = vectorOr("position", glm::vec3(0.0f));
      const glm::vec3 rotation = vectorOr("rotation", glm::vec3(0.0f));
      const glm::vec3 scale = vectorOr("scale", glm::vec3(1.0f));
      auto builder = engine.entities().create(name);
      // "box" and "sphere" are the engine's own models
      if (sol::optional<std::string> model = o["model"])
        builder.withModel(*model == "box"      ? std::string(EngineConfig::MODEL_BOX)
                          : *model == "sphere" ? std::string(EngineConfig::MODEL_SPHERE)
                                               : assetPath(*model),
                          position, rotation, scale);
      else
        builder.withTransform(position, rotation, scale);
      if (sol::optional<glm::vec3> color = o["color"])
        builder.withSolidColor(*color);
      if (sol::optional<glm::vec3> emission = o["emission"])
        builder.withEmission(*emission, o.get_or("glow", 1.5f));
      if (sol::optional<std::string> body = o["body"]) {
        const std::string shape = o.get_or<std::string>("shape", "box");
        builder.withRigidBody(*body == "static" ? BodyType::Static
                              : *body == "kinematic" ? BodyType::Kinematic
                                                     : BodyType::Dynamic,
                              shape == "sphere"       ? BodyShape::Sphere
                              : shape == "capsule"    ? BodyShape::Capsule
                              : shape == "convexHull" ? BodyShape::ConvexHull
                              : shape == "mesh"       ? BodyShape::Mesh
                                                      : BodyShape::Box,
                              o.get_or("mass", 0.0f));
      }
      if (o.get_or("collision", false))
        builder.withModelCollision();
      if (sol::optional<std::string> script = o["script"])
        builder.withComponent<Script>(*script);
      return entityObject(builder.build());
    });
    api.set_function("destroy", [this](sol::optional<ScriptEntity> e) {
      if (e && engine.isAlive(e->id))
        engine.destroyEntity(e->id);
    });
    api.set_function("loadScene", [this](const std::string &path) { engine.loadScene(assetPath(path)); });
    api.set_function("setTitle", [this](const std::string &title) { engine.setWindowTitle(title); });

    // camera: the active one
    sol::table camera = lua.create_named_table("camera");
    // Lets go while the free camera (Settings) is on, like the engine's own examples
    camera.set_function("lookAt", [this](const glm::vec3 &from, const glm::vec3 &target) {
      if (engine.getState(Toggle::CameraMovement))
        return;
      if (Camera *active = engine.getActiveCamera())
        active->lookAt(from, target);
    });
    camera.set_function("position", [this]() {
      const Camera *active = engine.getActiveCamera();
      return active ? active->position : glm::vec3(0.0f);
    });
    camera.set_function("forward", [this]() {
      const Camera *active = engine.getActiveCamera();
      return active ? active->front : glm::vec3(0.0f, 0.0f, -1.0f);
    });

    // input: actions and axes (see InputSystem::bindAction), keys by SDL's names ("Space", "Left Shift")
    sol::table input = lua.create_named_table("input");
    input.set_function("axis", [this](const std::string &name) { return engine.input().getAxis(name); });
    input.set_function("down", [this](const std::string &name) { return engine.input().isActionDown(name); });
    input.set_function("pressed", [this](const std::string &name) { return engine.input().wasActionPressed(name); });
    input.set_function("released", [this](const std::string &name) { return engine.input().wasActionReleased(name); });
    input.set_function("key", [this](const std::string &name) {
      const SDL_Scancode key = SDL_GetScancodeFromName(name.c_str());
      return key != SDL_SCANCODE_UNKNOWN && engine.input().isKeyDown(key);
    });
    input.set_function("keyPressed", [this](const std::string &name) {
      const SDL_Scancode key = SDL_GetScancodeFromName(name.c_str());
      return key != SDL_SCANCODE_UNKNOWN && engine.input().wasKeyPressed(key);
    });
    input.set_function("mouse", [this]() {
      const glm::vec2 position = engine.input().getMousePosition();
      return std::make_tuple(position.x, position.y);
    });
    input.set_function("click", [this](sol::optional<int> button) {
      return engine.input().wasMouseButtonPressed(static_cast<MouseButton>(button.value_or(1)));
    });
    input.set_function("bindings", [this](const std::string &path) { return engine.input().loadBindings(assetPath(path)); });

    // physics
    sol::table physics = lua.create_named_table("physics");
    physics.set_function("raycast", [this](const glm::vec3 &origin, const glm::vec3 &direction,
                                           sol::optional<float> distance) -> sol::object {
      const auto hit = engine.physics().raycast(origin, direction, distance.value_or(1000.0f));
      if (!hit)
        return sol::lua_nil;
      sol::table result = lua.create_table();
      result["entity"] = entityObject(hit->entity);
      result["point"] = hit->point;
      result["normal"] = hit->normal;
      result["distance"] = hit->distance;
      return result;
    });
    physics.set_function("gravity", [this](sol::optional<glm::vec3> gravity) {
      if (gravity)
        engine.physics().setGravity(*gravity);
      return engine.physics().getGravity();
    });

    // sound
    sol::table sound = lua.create_named_table("sound");
    sound.set_function("play", [this](const std::string &path, sol::optional<float> volume, sol::optional<float> pitch) {
      engine.playSound(assetPath(path), volume.value_or(1.0f), pitch.value_or(1.0f));
    });
    sound.set_function("playAt", [this](const std::string &path, const glm::vec3 &position, sol::optional<float> volume) {
      engine.playSoundAt(assetPath(path), position, volume.value_or(1.0f));
    });
    sound.set_function("music", [this](const std::string &path, sol::optional<float> volume) {
      engine.playMusic(assetPath(path), volume.value_or(1.0f));
    });
    sound.set_function("stopMusic", [this]() { engine.stopMusic(); });

    // ui: RmlUi documents, changed by element id
    sol::table ui = lua.create_named_table("ui");
    // A document loaded once: loading it again (the game script running again after Stop) shows the same one
    ui.set_function("load", [this](const std::string &path) {
      auto it = documents.find(path);
      if (it != documents.end()) {
        it->second->Show();
        return true;
      }
      Rml::ElementDocument *document = engine.loadUIDocument(assetPath(path));
      if (document)
        documents[path] = document;
      return document != nullptr;
    });
    ui.set_function("text", [this](const std::string &id, sol::object value) {
      if (Rml::Element *element = findElement(id))
        element->SetInnerRML(lua["tostring"](value).get<std::string>());
    });
    ui.set_function("show", [this](const std::string &id, bool visible) {
      if (Rml::Element *element = findElement(id)) {
        if (visible)
          element->RemoveProperty("display");
        else
          element->SetProperty("display", "none");
      }
    });
    ui.set_function("class", [this](const std::string &id, const std::string &name, bool on) {
      if (Rml::Element *element = findElement(id))
        element->SetClass(name, on);
    });
    ui.set_function("onClick", [this](const std::string &id, sol::protected_function handler) {
      Rml::Element *element = findElement(id);
      if (!element)
        return false;
      clickListeners.push_back(std::make_unique<ClickListener>(
          std::move(handler), [this](const sol::protected_function_result &result) {
            if (!result.valid()) {
              const sol::error error = result;
              report("ui.onClick", error.what());
            }
          }));
      element->AddEventListener(Rml::EventId::Click, clickListeners.back().get());
      return true;
    });
  }

  Rml::Element *findElement(const std::string &id) {
    Rml::Context *context = engine.getUIContext();
    if (!context)
      return nullptr;
    for (int i = 0; i < context->GetNumDocuments(); ++i)
      if (Rml::Element *element = context->GetDocument(i)->GetElementById(id))
        return element;
    return nullptr;
  }
};

ScriptSystem::ScriptSystem(Engine &engine) : m_impl(std::make_unique<Impl>(engine)) {}
ScriptSystem::~ScriptSystem() = default;

void ScriptSystem::update(float deltaTime) { m_impl->update(deltaTime); }

void ScriptSystem::reset() {
  Impl &impl = *m_impl;
  impl.instances.clear();
  impl.fields.clear();
  impl.gameTable = impl.lua.create_named_table("game"); // shared state starts over too
  impl.lua.collect_garbage();
  if (impl.hasGameScript) {
    impl.gameScript = Impl::Instance{};
    impl.gameScript.path = impl.gameScriptPath;
    impl.load(impl.gameScript);
  }
}

bool ScriptSystem::runGameScript(const std::string &path) {
  Impl &impl = *m_impl;
  impl.gameScriptPath = EngineConfig::resolvePath(assetPath(path));
  impl.gameScript = Impl::Instance{};
  impl.gameScript.path = impl.gameScriptPath;
  impl.hasGameScript = true;
  return impl.load(impl.gameScript);
}

sol::state &ScriptSystem::lua() { return m_impl->lua; }
const std::string &ScriptSystem::getLastError() const { return m_impl->lastError; }
size_t ScriptSystem::getInstanceCount() const { return m_impl->instances.size(); }
