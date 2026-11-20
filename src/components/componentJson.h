#pragma once
// JSON conversions for GLM types and the engine's plain-data components, used by scene files.
//
// Reading fills missing fields with the component's defaults, so older scene files keep loading as components grow.
// Runtime state (live particles, sound voices, derived camera vectors) is not saved.

#include "components/animationStateMachine.h"
#include "components/animator.h"
#include "components/audioSource.h"
#include "components/camera.h"
#include "components/cameraRig.h"
#include "components/collision.h"
#include "components/gravityLens.h"
#include "components/light.h"
#include "components/particleEmitter.h"
#include "components/skybox.h"
#include "components/tags.h"
#include "components/characterController.h"
#include "components/rigidBody.h"
#include "components/script.h"
#include "components/transform.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

// Vectors as arrays: [x, y, z]
namespace nlohmann {
template <glm::length_t L, typename T, glm::qualifier Q> struct adl_serializer<glm::vec<L, T, Q>> {
  static void to_json(json &j, const glm::vec<L, T, Q> &v) {
    j = json::array();
    for (glm::length_t i = 0; i < L; ++i)
      j.push_back(v[i]);
  }
  static void from_json(const json &j, glm::vec<L, T, Q> &v) {
    for (glm::length_t i = 0; i < L && i < static_cast<glm::length_t>(j.size()); ++i)
      v[i] = j.at(static_cast<size_t>(i)).get<T>();
  }
};
} // namespace nlohmann

NLOHMANN_JSON_SERIALIZE_ENUM(LightType, {{LightType::Directional, "directional"},
                                         {LightType::Point, "point"},
                                         {LightType::Spot, "spot"}})

NLOHMANN_JSON_SERIALIZE_ENUM(EmissionShape, {{EmissionShape::Cone, "cone"},
                                             {EmissionShape::Sphere, "sphere"},
                                             {EmissionShape::Box, "box"},
                                             {EmissionShape::Ring, "ring"}})

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Transform, position, rotation, scale)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Light, type, position, direction, color, intensity, ambient, constant,
                                                linear, quadratic, cutOff, outerCutOff, castShadows, shadowRange)

// front/up follow from yaw and pitch; aspectRatio from the window
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Camera, position, yaw, pitch, fov, moveSpeed, mouseSensitivity,
                                                smoothFactor, isRelative)

// What it follows is the target's Name; the smoothed state and the orbit angles start over
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(CameraRig, target, offset, lookAhead, positionSmoothing,
                                                rotationSmoothing, followRotation, orbit, orbitSensitivity, orbitSpeed)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Collision, min, max, isStatic, layer)

NLOHMANN_JSON_SERIALIZE_ENUM(FogMode, {{FogMode::Linear, "linear"}, {FogMode::Exponential, "exponential"}})
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Fog, enabled, mode, matchSky, color, start, finish, density)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Skybox, path, intensity, rotation, color, fog, ambientColor,
                                                ambientIntensity, ssaoRadius, ssaoIntensity)

NLOHMANN_JSON_SERIALIZE_ENUM(BodyType, {{BodyType::Static, "static"},
                                        {BodyType::Dynamic, "dynamic"},
                                        {BodyType::Kinematic, "kinematic"}})
NLOHMANN_JSON_SERIALIZE_ENUM(BodyShape, {{BodyShape::Box, "box"},
                                         {BodyShape::Sphere, "sphere"},
                                         {BodyShape::Capsule, "capsule"},
                                         {BodyShape::ConvexHull, "convexHull"},
                                         {BodyShape::Mesh, "mesh"}})
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(RigidBody, type, shape, size, mass, friction, restitution, linearDamping,
                                                angularDamping, gravityScale, lockRotation, sensor, layer)
// Property overrides by name, as plain JSON values: {"path": "scripts/player.lua", "properties": {"speed": 8}}.
// A vec3 is an array, so a color reads [1, 0.8, 0.2].
inline void to_json(nlohmann::json &j, const Script &script) {
  j = nlohmann::json{{"path", script.path}};
  if (script.properties.empty())
    return;
  nlohmann::json properties = nlohmann::json::object();
  for (const auto &entry : script.properties) { // not a structured binding: lambdas capture those only from C++20
    const std::string &name = entry.first;
    std::visit([&](const auto &v) { properties[name] = v; }, entry.second);
  }
  j["properties"] = properties;
}
inline void from_json(const nlohmann::json &j, Script &script) {
  script.path = j.value("path", std::string());
  script.properties.clear();
  auto properties = j.find("properties");
  if (properties == j.end() || !properties->is_object())
    return;
  for (const auto &[name, value] : properties->items()) {
    if (value.is_boolean())
      script.properties[name] = value.get<bool>();
    else if (value.is_number())
      script.properties[name] = value.get<float>();
    else if (value.is_string())
      script.properties[name] = value.get<std::string>();
    else if (value.is_array() && value.size() == 3)
      script.properties[name] = value.get<glm::vec3>();
  }
}

// Just the list: "Tags": ["enemy", "flying"]
inline void to_json(nlohmann::json &j, const Tags &tags) { j = tags.tags; }
inline void from_json(const nlohmann::json &j, Tags &tags) {
  tags.tags.clear();
  if (!j.is_array())
    return;
  for (const auto &tag : j)
    if (tag.is_string() && !tag.get<std::string>().empty() &&
        std::find(tags.tags.begin(), tags.tags.end(), tag.get<std::string>()) == tags.tags.end())
      tags.tags.push_back(tag.get<std::string>());
}

// The clip and how it plays; a crossfade in progress is not saved
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Animator, clip, time, speed, loop, playing)
// The definition file and the parameters ("grounded": true reads as well as 1); where the machine is is not saved
inline void to_json(nlohmann::json &j, const AnimationStateMachine &machine) {
  j = nlohmann::json{{"path", machine.path}, {"params", machine.params}};
}
inline void from_json(const nlohmann::json &j, AnimationStateMachine &machine) {
  machine = AnimationStateMachine();
  machine.path = j.value("path", std::string());
  if (!j.contains("params") || !j["params"].is_object())
    return;
  for (const auto &[name, value] : j["params"].items()) {
    if (value.is_boolean())
      machine.params[name] = value.get<bool>() ? 1.0f : 0.0f;
    else if (value.is_number())
      machine.params[name] = value.get<float>();
  }
}

// move, jump, onGround and velocity are per frame, not part of the scene
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(CharacterController, radius, height, maxSlope, stepHeight,
                                                gravityScale, layer)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(GravityLens, schwarzschildRadius, maxMagnification)

// Components hold floats; written as doubles they would read 1.2000000476837158. Keeps float precision instead.
inline void roundFloats(nlohmann::json &value) {
  if (value.is_number_float()) {
    char text[32];
    std::snprintf(text, sizeof(text), "%.7g", value.get<double>());
    value = std::strtod(text, nullptr);
  } else if (value.is_structured()) {
    for (auto &child : value)
      roundFloats(child);
  }
}

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(AudioSource, path, volume, pitch, loop, spatial, playing, minDistance,
                                                maxDistance, rolloff)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(ParticleEmitter, emitRate, emissionShape, emitTangentially,
                                                tangentVariance, particleLifetime, speed, speedVariance, size,
                                                sizeDecay, gravity, startColor, endColor, emitDirection, offset, spread,
                                                sphereRadius, sphereSurfaceOnly, ringInnerRadius, ringOuterRadius,
                                                ringThickness, boxHalfExtents, maxParticles, active, additiveBlending,
                                                attractMode, attractPointRelative, attractPoint, attractStrength,
                                                tangentStrength, resetRadius, enableRecycling, distBasedColor,
                                                colorRadius, friction)
