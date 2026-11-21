// Lua bindings: the physics (raycasts, layers, gravity) and nav (pathfinding) tables.
// A binding only converts types and calls the C++ game API: see the rule at the top of luaBindings.h.

#include <sol/sol.hpp> // SOL_ALL_SAFETIES_ON comes from the build, the same for every file that includes sol2

#include "systems/lua/luaBindings.h"

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
#include "foundation/core/gameEntity.h"
#include "foundation/core/logger.h"
#include "systems/inputSystem.h"
#include "systems/navigationSystem.h"
#include "systems/physicsSystem.h"
#include "systems/playerSettings.h"
#include "systems/renderSystem.h"
#include "systems/saveData.h"
#include <RmlUi/Core.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <stdexcept>
#include <optional>
#include <tuple>
#include <nlohmann/json.hpp>
#include <unordered_map>
#include <variant>
#include <vector>

void LuaBindings::bindPhysics() {
  // physics
  sol::table physics = lua.create_named_table("physics");
  // physics.raycast(origin, direction, distance, { layers = {"enemy", "static"} }): the nearest body, or nil. Only bodies on
  // the listed layers count (all of them without `layers`); a name that is no layer is an error.
  physics.set_function("raycast", [this](const glm::vec3 &origin, const glm::vec3 &direction,
                                         sol::optional<float> distance, sol::optional<sol::table> options) -> sol::object {
    uint32_t layerMask = CollisionLayers::kAllLayers;
    if (options)
      if (const sol::optional<sol::table> layers = (*options)["layers"])
        layerMask = layerMaskOf(*layers);
    const auto hit = engine.physics().raycast(origin, direction, distance.value_or(1000.0f), nullptr, layerMask);
    if (!hit)
      return sol::lua_nil;
    return hitTable(*hit);
  });
  // physics.raycastBoxes(origin, direction, distance, { layers = {...} }): the same for the Collision boxes (the ones
  // engine.create makes with collision = true, and isTouching tests), which are not physics bodies
  physics.set_function("raycastBoxes", [this](const glm::vec3 &origin, const glm::vec3 &direction,
                                              sol::optional<float> distance, sol::optional<sol::table> options) -> sol::object {
    uint32_t layerMask = CollisionLayers::kAllLayers;
    if (options)
      if (const sol::optional<sol::table> layers = (*options)["layers"])
        layerMask = layerMaskOf(*layers);
    const auto hit = engine.raycast(origin, direction, distance.value_or(1000.0f), nullptr, layerMask);
    return hit ? sol::object(hitTable(*hit)) : sol::object(sol::lua_nil);
  });
  // physics.raycastModels(origin, direction, distance): the nearest triangle of a visible model, with the surface
  // normal there (physics.raycast stops at the body's shape); distance is unlimited without one
  physics.set_function("raycastModels", [this](const glm::vec3 &origin, const glm::vec3 &direction,
                                               sol::optional<float> distance) -> sol::object {
    const auto hit = engine.raycastModels(origin, direction, distance.value_or(std::numeric_limits<float>::max()));
    return hit ? sol::object(hitTable(*hit)) : sol::object(sol::lua_nil);
  });
  // Collision layers: bodies, characters and Collision boxes are on one (entity.layer, or layer = in engine.create), and
  // layers that do not collide pass through each other. "default", "static", "player", "enemy", "projectile" and
  // "trigger" exist; addLayer defines more (32 in all). Everything collides until setLayersCollide says otherwise.
  physics.set_function("addLayer", [this](const std::string &name) {
    if (engine.layers().add(name) < 0)
      throw std::runtime_error("physics.addLayer: cannot define \"" + name + "\" (the name is empty or all 32 layers are taken)");
  });
  physics.set_function("layers", [this]() { return sol::as_table(std::vector<std::string>(engine.layers().names())); });
  physics.set_function("setLayersCollide", [this](const std::string &a, const std::string &b, bool collide) {
    requireLayer("physics.setLayersCollide", a);
    requireLayer("physics.setLayersCollide", b);
    engine.layers().setCollide(a, b, collide);
  });
  physics.set_function("layersCollide", [this](const std::string &a, const std::string &b) {
    requireLayer("physics.layersCollide", a);
    requireLayer("physics.layersCollide", b);
    return engine.layers().collide(a, b);
  });
  physics.set_function("gravity", [this](sol::optional<glm::vec3> gravity) {
    if (gravity)
      engine.physics().setGravity(*gravity);
    return engine.physics().getGravity();
  });

  // navigation
  // nav.build({ min = vec3, max = vec3, cellSize = 0.5, agentRadius = 0.35, agentHeight = 1.8, maxSlope = 45,
  //             stepHeight = 0.35 }): every field is optional; without min and max the map covers the static bodies
  sol::table nav = lua.create_named_table("nav");
  nav.set_function("build", [this](sol::optional<sol::table> options) {
    NavigationSettings settings;
    std::optional<glm::vec3> min, max;
    if (options) {
      const sol::table &o = *options;
      settings.cellSize = o.get_or("cellSize", settings.cellSize);
      settings.agentRadius = o.get_or("agentRadius", settings.agentRadius);
      settings.agentHeight = o.get_or("agentHeight", settings.agentHeight);
      settings.maxSlope = o.get_or("maxSlope", settings.maxSlope);
      settings.stepHeight = o.get_or("stepHeight", settings.stepHeight);
      min = vectorField(o, "min");
      max = vectorField(o, "max");
    }
    return engine.navigation().buildIn(min, max, settings);
  });
  // The points to walk, on the ground, or nil when there is no way (or no map)
  nav.set_function("path", [this](const glm::vec3 &from, const glm::vec3 &to) -> sol::object {
    const std::vector<glm::vec3> points = engine.navigation().findPath(from, to);
    if (points.empty())
      return sol::lua_nil;
    return sol::make_object(lua, sol::as_table(points));
  });
  nav.set_function("walkable", [this](const glm::vec3 &point) { return engine.navigation().isWalkable(point); });
}
