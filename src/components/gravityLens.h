#pragma once
// Gravitational lens: bends the light of particles behind the entity, as a point mass would.

struct GravityLens {
  float schwarzschildRadius = 1.0f; // world units; sets the lensing strength
  float maxMagnification = 6.0f;    // caps the brightening of images near the Einstein ring

  GravityLens() = default;
  GravityLens(float radius, float magnificationCap = 6.0f)
      : schwarzschildRadius(radius), maxMagnification(magnificationCap) {}
};
