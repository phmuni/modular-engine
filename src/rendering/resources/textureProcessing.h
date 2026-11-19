#pragma once
// Turns decoded images into what the GPU uses directly: a full mip chain, compressed to BC1 (RGB) or BC3 (RGBA) when
// the GPU supports S3TC. Compressed results are cached in bin/cache/textures, so the first run converts each texture
// and later runs only read it back, skipping the PNG/JPEG decode too.
//
// Everything here except detectSupport runs on any thread.

#include "rendering/resources/decodedImage.h"
#include <functional>
#include <string>

namespace TextureProcessing {

// Checks for S3TC; call once on the main thread with the OpenGL context current. Until then nothing is compressed.
void detectSupport();
bool compressionSupported();

// Adds the mip chain to an 8-bit image and, with `compress`, converts it to BC1/BC3. Other images are left alone,
// and the GPU builds their mipmaps on upload.
void prepare(DecodedImage &image, bool compress);

// A ready-to-upload image for `key` (a file path, or "model.glb#image3" for embedded ones): from the cache if it is
// newer than `sourceFile`, otherwise decoded with `decode`, prepared, and cached when compressed.
// `compress` is false for images that BC1 would damage, such as normal maps.
DecodedImage load(const std::string &key, const std::string &sourceFile, bool compress,
                  const std::function<DecodedImage()> &decode);

} // namespace TextureProcessing
