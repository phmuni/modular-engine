#pragma once
// Uploads large textures over several frames instead of stalling one, smallest mip level first.
//
// A new texture gets its small levels at once, so it is usable (blurry) right away; each frame then sends up to a
// byte budget of the larger levels, and the texture sharpens as each level completes (GL_TEXTURE_BASE_LEVEL follows
// the largest complete level). Main thread only.

#include "rendering/resources/decodedImage.h"
#include <cstddef>

namespace TextureUploader {

// Creates the texture and starts sending `image`; returns at once. Empty images give a 1x1 magenta texture.
GLuint upload(DecodedImage image);
// Sends up to `budgetBytes` of the waiting textures; call once per frame
void pump(size_t budgetBytes);
// Textures still being sent
size_t pendingCount();

} // namespace TextureUploader
