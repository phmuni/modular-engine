#pragma once
// Image pixels decoded on any thread, turned into a GL texture later on the main thread (Material::uploadTexture).

#include <glad/glad.h>
#include <cstdint>
#include <vector>

struct DecodedImage {
  int width = 0;
  int height = 0;
  int channels = 0;           // 1-4
  GLenum type = GL_UNSIGNED_BYTE;
  std::vector<uint8_t> pixels; // bottom row first, tightly packed

  bool empty() const { return pixels.empty(); }
};
