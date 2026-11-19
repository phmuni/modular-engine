#pragma once
// Image pixels decoded on any thread, turned into a GL texture later on the main thread (Material::uploadTexture).
//
// After TextureProcessing::prepare, an image carries its whole mip chain, either as plain pixels (pixels + mipmaps)
// or compressed for the GPU (compressedLevels), so the main thread only copies bytes and never builds mipmaps.

#include <glad/glad.h>
#include <cstdint>
#include <vector>

struct DecodedImage {
  int width = 0;
  int height = 0;
  int channels = 0;           // 1-4
  GLenum type = GL_UNSIGNED_BYTE;
  std::vector<uint8_t> pixels; // level 0, bottom row first, tightly packed

  // Levels 1, 2... of `pixels`, each half the size of the previous one; empty means the GPU builds them
  std::vector<std::vector<uint8_t>> mipmaps;

  // When set (a GL_COMPRESSED_* format), the image is in compressedLevels (level 0 first) instead of pixels
  GLenum compressedFormat = 0;
  std::vector<std::vector<uint8_t>> compressedLevels;

  bool empty() const { return pixels.empty() && compressedLevels.empty(); }
  bool compressed() const { return compressedFormat != 0; }
};
