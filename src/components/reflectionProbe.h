#pragma once
// Reflection probe: a static capture of what is around a point, which lights the things inside its box with that
// instead of the sky. Without one, an interior is lit and reflected by the sky outside it; a probe in the room gives
// it the room's own light and reflections (the walls, the lamps), which is what a metal ball in it should show.
//
// Put it on an entity with a Transform. The probe sees from the entity's position; its box is centered there, world
// units, aligned to the world axes (the entity's rotation and scale do not turn or stretch it). Things whose bounds
// overlap the box are lit by it, up to 4 probes at once; towards the box's edges it fades into the sky (or the next
// probe) over `blendDistance`. The capture is of the first bounce only: the sky, the lights and the geometry as lit
// by them, but not other probes. It is static, taken when the scene loads (or read from the baked file) and again only
// on request (Engine::refreshReflectionProbe, the editor's Bake), never per frame.
//
// The result (pre-filtered reflections and the diffuse light as spherical harmonics) is baked to a file under
// assets/probes/, whose path `bakedPath` keeps. A probe with no file, or one that cannot be read, is captured when its
// scene loads instead.

#include <glm/glm.hpp>
#include <string>

struct ReflectionProbe {
  glm::vec3 size{10.0f};        // the influence box, full extents in world units, centered on the entity
  float blendDistance = 1.0f;   // how far inside the box's edges the probe fades from nothing to full effect
  float intensity = 1.0f;       // multiplies what the probe gives, on its own: the Skybox's environmentIntensity
                                // scales the sky's light, not the probe's
  bool boxProjection = true;    // reflections are corrected for the box, so a wall appears where it is (parallax)
  int resolution = 128;         // size of a face of the capture: 64, 128 or 256 (the stored reflections are 128 anyway)
  std::string bakedPath;        // the baked file, relative to assets/ like other assets; empty: not baked

  // The sizes `resolution` can take
  static int clampResolution(int value) { return value <= 64 ? 64 : (value >= 256 ? 256 : 128); }
};
