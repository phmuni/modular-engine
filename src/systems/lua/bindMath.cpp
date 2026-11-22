// Lua bindings: vec3 and the small value types (Transform, Light).
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

void LuaBindings::bindMath() {
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
  lua.new_usertype<Light>("Light", sol::no_constructor, "color", &Light::color, "intensity", &Light::intensity,
                         "range", &Light::range);
}
