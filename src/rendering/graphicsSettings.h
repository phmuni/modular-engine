#pragma once
// Antialiasing and upscaling settings (Engine::setMsaa, setFxaa, setFsr, setFsrSharpness; project.json's "graphics").
// Plain data: the renderer applies it (Renderer::setMsaa and friends) and the engine, the pause menu and the editor
// pass it around.

// The defaults: MSAA 2x on, FXAA and FSR off. Also what deterministic runs (tools/regression.py) use.
struct AntialiasingSettings {
  // Samples per pixel of the 3D scene: 0 (off), 2, 4 or 8. Other numbers snap down to one of these, and the GPU's
  // GL_MAX_SAMPLES caps it. Shadow maps stay single-sample.
  int msaa = 2;
  // FXAA 3.11 on the tone mapped image (particles included), before the upscale and the final composite
  bool fxaa = false;
  // AMD FidelityFX Super Resolution 1 (EASU upscale + RCAS sharpening) instead of the plain linear stretch, used while
  // the render scale is below 1; at 1 or above it does nothing
  bool fsr = false;
  // RCAS sharpening attenuation in stops, kMinFsrSharpness to kMaxFsrSharpness: 0 is the sharpest, each stop halves the
  // strength (2 is subtle). AMD's default is 0.2.
  float fsrSharpness = 0.2f;
};

constexpr float kMinFsrSharpness = 0.0f;
constexpr float kMaxFsrSharpness = 2.0f;
