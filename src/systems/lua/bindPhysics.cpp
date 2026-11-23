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
  // physics.raycast(origin, direction, distance, { layers = {"enemy", "static"}, ignore = self, characters = true }): the
  // nearest body, or nil. Only bodies on the listed layers count (all of them without `layers`; a name that is no layer
  // is an error), the entity or list of entities in `ignore` are skipped, and character controllers are hit only with
  // `characters` (the hit's entity is the character's).
  physics.set_function("raycast", [this](const glm::vec3 &origin, const glm::vec3 &direction,
                                         sol::optional<float> distance, sol::optional<sol::table> options) -> sol::object {
    const auto hit = engine.physics().raycast(origin, direction, distance.value_or(1000.0f), queryOptions(options));
    if (!hit)
      return sol::lua_nil;
    return hitTable(*hit);
  });
  // physics.raycastBoxes(origin, direction, distance, { layers = {...}, ignore = ... }): the same for the Collision
  // boxes (the ones engine.create makes with collision = true, and isTouching tests), which are not physics bodies
  physics.set_function("raycastBoxes", [this](const glm::vec3 &origin, const glm::vec3 &direction,
                                              sol::optional<float> distance, sol::optional<sol::table> options) -> sol::object {
    const auto hit = engine.raycast(origin, direction, distance.value_or(1000.0f), queryOptions(options));
    return hit ? sol::object(hitTable(*hit)) : sol::object(sol::lua_nil);
  });
  // physics.overlapSphere(center, radius, options) and physics.overlapBox(center, halfExtents, rotation, options): the
  // entities whose physics bodies overlap the shape, as a list; the options are the raycast's (layers, ignore,
  // characters), rotation is in degrees like an entity's
  physics.set_function("overlapSphere", [this](const glm::vec3 &center, float radius,
                                               sol::optional<sol::table> options) {
    return entityList(engine.physics().overlapSphere(center, radius, queryOptions(options)));
  });
  physics.set_function("overlapBox", [this](const glm::vec3 &center, const glm::vec3 &halfExtents,
                                            sol::optional<glm::vec3> rotation, sol::optional<sol::table> options) {
    return entityList(engine.physics().overlapBox(center, halfExtents, rotation.value_or(glm::vec3(0.0f)),
                                                  queryOptions(options)));
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
  physics.set_function("gravity", [this]() { return engine.physics().getGravity(); });
  physics.set_function("setGravity", [this](const glm::vec3 &gravity) { engine.physics().setGravity(gravity); });

  // navigation
  // nav.build({ min = vec3, max = vec3, cellSize = 0.5, agentRadius = 0.35, agentHeight = 1.8, maxSlope = 45,
  //             stepHeight = 0.35, minRegionArea = 8 }): every field is optional; without min and max the map covers
  //             the static bodies
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
      settings.minRegionArea = o.get_or("minRegionArea", settings.minRegionArea);
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
  // Builds again only the tiles the box touches (after a door opened or a wall fell); whether it did
  nav.set_function("rebuild", [this](const glm::vec3 &min, const glm::vec3 &max) {
    return engine.navigation().rebuild(min, max);
  });
  // The nearest walkable ground within maxDistance (1 by default) of the point, or nil
  nav.set_function("nearest", [this](const glm::vec3 &point, sol::optional<float> maxDistance) -> sol::object {
    const std::optional<glm::vec3> found = engine.navigation().getNearestWalkable(point, maxDistance.value_or(1.0f));
    if (!found)
      return sol::lua_nil;
    return sol::make_object(lua, *found);
  });
}
