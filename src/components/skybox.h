#pragma once
// Skybox: an image drawn behind everything, as if infinitely far away.
//
// The image is an equirectangular panorama (360 by 180 degrees, twice as wide as tall, the usual format of HDRI
// sites): its center column is straight ahead (-Z) and its top row straight up. The first Skybox component in the
// scene is drawn; put it on any entity (it does not need a Transform).

#include <string>

struct Skybox {
  std::string path;       // image file (png, jpg, hdr...), relative to the executable's folder like other assets
  float intensity = 1.0f; // brightness multiplier; above 1 the brightest stars reach the bloom
  float rotation = 0.0f;  // degrees around the vertical axis, to turn the panorama

  Skybox() = default;
  Skybox(std::string imagePath, float brightness = 1.0f, float rotationDegrees = 0.0f)
      : path(std::move(imagePath)), intensity(brightness), rotation(rotationDegrees) {}
};
