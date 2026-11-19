#pragma once
// JSON conversions for GLM types and the engine's plain-data components, used by scene files.
//
// Reading fills missing fields with the component's defaults, so older scene files keep loading as components grow.
// Runtime state (live particles, sound voices, derived camera vectors) is not saved.

#include "components/audioSource.h"
#include "components/camera.h"
#include "components/collision.h"
#include "components/gravityLens.h"
#include "components/light.h"
#include "components/particleEmitter.h"
#include "components/transform.h"
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

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Collision, min, max, isStatic)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(GravityLens, schwarzschildRadius, maxMagnification)

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
