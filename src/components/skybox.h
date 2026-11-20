#pragma once
// Skybox: an image drawn behind everything, as if infinitely far away.
//
// The image is an equirectangular panorama (360 by 180 degrees, twice as wide as tall, the usual format of HDRI
// sites): its center column is straight ahead (-Z) and its top row straight up. The first Skybox component in the
// scene is drawn; put it on any entity (it does not need a Transform).
//
// `color` is the sky color the image is cleared to (Engine::setSkyColor sets it on the first Skybox component): with
// no `path` a plain sky, with one the panorama draws over it.

#include <glm/glm.hpp>
#include <string>

struct Skybox {
  std::string path;       // image file (png, jpg, hdr...), relative to assets/ like other assets; empty: only the color
  float intensity = 1.0f; // brightness multiplier; above 1 the brightest stars reach the bloom
  float rotation = 0.0f;  // degrees around the vertical axis, to turn the panorama
  glm::vec3 color{0.0f};  // the background color: black by default

  Skybox() = default;
  Skybox(std::string imagePath, float brightness = 1.0f, float rotationDegrees = 0.0f)
      : path(std::move(imagePath)), intensity(brightness), rotation(rotationDegrees) {}
};
