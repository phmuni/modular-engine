#pragma once
// Binary cache of parsed meshes, so a large text model (OBJ) is parsed once and then read back as raw arrays.
//
// Entries live in bin/cache/meshes, one per source file. An entry is used only while the source file and the material
// files (.mtl) in its folder are unchanged since it was written; otherwise the model is parsed and the entry rewritten.

#include "rendering/resources/mesh.h"
#include <string>

namespace MeshCache {

// Replaces `data` with the geometry, parts and materials (not baseDir, textures or bounds) stored for `sourcePath`,
// if the entry is current
bool read(const std::string &sourcePath, MeshData &data);
// Saves what a loader produced for `sourcePath`; failures only log, the cache is an optimization
void write(const std::string &sourcePath, const MeshData &data);

} // namespace MeshCache
