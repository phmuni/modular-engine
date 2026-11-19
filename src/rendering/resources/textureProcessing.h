#pragma once
// Turns decoded images into what the GPU uses directly: a full mip chain, compressed to BC1 (RGB) or BC3 (RGBA) color
// when the GPU supports S3TC, and normal maps to BC5 (two channels, part of OpenGL 3). Compressed results are cached in
// bin/cache/textures, so the first run converts each texture and later runs only read it back, skipping the PNG/JPEG
// decode too.
//
// Everything here except detectSupport runs on any thread.

#include "rendering/resources/decodedImage.h"
#include <functional>
#include <string>

namespace TextureProcessing {

// Checks for S3TC; call once on the main thread with the OpenGL context current. Until then color textures are not
// compressed.
void detectSupport();

// Adds the mip chain to an 8-bit image and converts it to BC1/BC3 (color) or BC5 (normals) when compression is on.
// Other images are left alone, and the GPU builds their mipmaps on upload.
void prepare(DecodedImage &image);

// A ready-to-upload image of `kind` for `key` (a file path, or "model.glb#image3" for embedded ones): from the cache
// if it is newer than `sourceFile`, otherwise decoded with `decode`, prepared, and cached when compressed.
// A normal map that turns out to be grayscale (a height map, as some .mtl files give for `bump`) comes back empty.
DecodedImage load(const std::string &key, const std::string &sourceFile, TextureKind kind,
                  const std::function<DecodedImage()> &decode);
// Only from the cache: false if `key` has no current entry (nothing is decoded)
bool loadCached(const std::string &key, const std::string &sourceFile, TextureKind kind, DecodedImage &image);

} // namespace TextureProcessing
