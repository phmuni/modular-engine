#pragma once
// Skybox: the scene's environment: an image drawn behind everything as if infinitely far away, plus the fog and the
// ambient light that go with the sky. The first Skybox component in the scene is the one in effect; put it on any
// entity (it does not need a Transform).
//
// The image is an equirectangular panorama (360 by 180 degrees, twice as wide as tall, the usual format of HDRI
// sites): its center column is straight ahead (-Z) and its top row straight up.
//
// `color` is the sky color the image is cleared to (Engine::setSkyColor sets it on the first Skybox component): with
// no `path` a plain sky, with one the panorama draws over it.
//
// Fog and ambient are part of this component rather than a component of their own so that "the sky" stays one thing
// to add, find and save, and so that fog can take the sky's color; a scene file without them loads as before.

#include <glm/glm.hpp>
#include <string>
#include <utility>

enum class FogMode {
  Linear,      // clear up to `start`, solid fog from `finish` on
  Exponential, // thickens with distance as 1 - exp(-(distance * density)^2): soft, with no edges
};

// Fog blends what is lit toward a color with distance from the camera (the sky is not fogged, so give the fog the
// sky's color and distant things fade into it). Particles fade out with it instead, as they are drawn after tone
// mapping.
struct Fog {
  bool enabled = false;
  FogMode mode = FogMode::Linear;
  bool matchSky = true;                    // the fog color is the sky's `color`; otherwise `color` below
  glm::vec3 color{0.5f, 0.6f, 0.7f};
  float start = 10.0f;                     // Linear: where it begins, in world units from the camera
  float finish = 100.0f;                   // Linear: where it is solid
  float density = 0.02f;                   // Exponential: per world unit
};

struct Skybox {
  std::string path;       // image file (png, jpg, hdr...), relative to assets/ like other assets; empty: only the color
  float intensity = 1.0f; // brightness multiplier; above 1 the brightest stars reach the bloom
  float rotation = 0.0f;  // degrees around the vertical axis, to turn the panorama
  glm::vec3 color{0.0f};  // the background color: black by default

  Fog fog;
  // Light added to everything lit, from nowhere: color times intensity, shown on the surface's color. 0 (the default)
  // adds nothing. Lights have no ambient of their own; this and the sky's lighting are the scene's fill.
  glm::vec3 ambientColor{1.0f};
  float ambientIntensity = 0.0f;
  // Image-based lighting: the sky itself lights what is in the scene (diffuse light and reflections), from the
  // panorama, or from `color` evenly when there is no image (so a black sky adds nothing). This scales it; 0 turns it
  // off.
  float environmentIntensity = 1.0f;
  // Ambient occlusion of this scene (see Ssao): how far geometry shades a point (world units) and how dark it makes
  // it. Whether it is on at all is the player's and quality setting (Engine::setSsao), not the scene's.
  float ssaoRadius = 0.5f;
  float ssaoIntensity = 1.0f;

  Skybox() = default;
  Skybox(std::string imagePath, float brightness = 1.0f, float rotationDegrees = 0.0f)
      : path(std::move(imagePath)), intensity(brightness), rotation(rotationDegrees) {}
};
