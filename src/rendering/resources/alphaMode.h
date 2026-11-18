#pragma once
// How a material's alpha is used (glTF alphaMode; OBJ materials with d < 1 blend).

enum class AlphaMode {
  Opaque, // alpha ignored unless the whole model is made transparent (Model::opacity)
  Mask,   // fragments below the cutoff are discarded; the rest is opaque
  Blend,  // drawn in the transparent pass, sorted back to front
};
