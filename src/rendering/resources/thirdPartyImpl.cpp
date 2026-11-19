// Implementations of the header-only file parsers (OBJ, glTF, images), kept in one file so CMake can compile it with
// optimizations even in Debug builds: unoptimized, they read large models several times slower.

#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader/tiny_obj_loader.h>

#define TINYGLTF3_IMPLEMENTATION
#include <tiny_gltf_loader/tiny_gltf_v3.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image/stb_image.h>

#define STB_DXT_IMPLEMENTATION
#include <stb_dxt/stb_dxt.h>
