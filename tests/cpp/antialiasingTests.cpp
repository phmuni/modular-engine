// Antialiasing and upscaling through the C++ API (the Lua side is tests/lua/antialiasing.lua): the defaults, clamping,
// changing MSAA, FXAA and FSR while frames are drawn, the cut-out shader flag for alpha-to-coverage, and the resolved
// scene depth that later passes read. No pixel is compared: the GPU decides what MSAA gives.

#include "foundation/core/engine.h"
#include "rendering/graphicsSettings.h"
#include "rendering/resources/customShader.h"
#include "systems/renderSystem.h"
#include "testing.h"
#include <memory>

TEST_CASE(antialiasing_defaults_and_clamping) {
  const AntialiasingSettings defaults;
  CHECK_EQ(defaults.msaa, 2);
  CHECK(!defaults.fxaa);
  CHECK(!defaults.fsr);
  CHECK_NEAR(defaults.fsrSharpness, 0.2f, 1e-6f);

  CHECK_EQ(engine.msaa(), 2);
  CHECK(!engine.fxaa());
  CHECK(!engine.fsr());
  CHECK_NEAR(engine.fsrSharpness(), 0.2f, 1e-6f);

  engine.setMsaa(0);
  CHECK_EQ(engine.msaa(), 0);
  engine.setMsaa(3);
  CHECK_EQ(engine.msaa(), 2);
  engine.setMsaa(64);
  CHECK(engine.msaa() >= 2 && engine.msaa() <= 8);
  engine.setFsrSharpness(5.0f);
  CHECK_NEAR(engine.fsrSharpness(), 2.0f, 1e-6f);
  engine.setFsrSharpness(-5.0f);
  CHECK_NEAR(engine.fsrSharpness(), 0.0f, 1e-6f);
  engine.setMsaa(2);
  engine.setFsrSharpness(0.2f);
}

TEST_CASE(antialiasing_changes_while_rendering) {
  auto size = std::make_shared<glm::ivec2>();
  steps.step(2, [&engine, size]() {
    *size = engine.renderResolution(); // known once frames have run
    engine.setMsaa(4);
    engine.setFxaa(true);
    CHECK_EQ(engine.renderResolution().x, size->x); // MSAA never changes the scene's size
  });
  steps.step(2, [&engine]() {
    engine.setFsr(true);
    engine.setRenderScale(0.5f);
  });
  steps.step(2, [&engine]() { engine.setMsaa(0); });
  steps.step(2, [&engine]() {
    engine.setRenderScale(1.0f);
    engine.setMsaa(8);
  });
  steps.step(2, [&engine, size]() {
    CHECK(engine.fsr());
    CHECK(engine.fxaa());
    CHECK_EQ(engine.renderResolution().y, size->y);
    CHECK(engine.msaa() >= 2);
  });
}

TEST_CASE(antialiasing_resolved_depth_texture) {
  Renderer &renderer = engine.getSystemManager().getSystem<RenderSystem>().getRenderer();
  steps.step(2, [&renderer]() {
    CHECK(renderer.sceneDepthTexture() != 0);
    CHECK(renderer.sceneColorTexture() != 0);
  });
  steps.step(1, [&engine, &renderer]() {
    engine.setMsaa(0);
    CHECK(renderer.sceneDepthTexture() != 0); // single-sample too, with or without MSAA
  });
}

TEST_CASE(antialiasing_cut_out_shaders) {
  // cutBelow() marks a custom shader for alpha-to-coverage; a plain discard does not
  const CustomShaderCode dissolve = parseCustomShader("void surface(inout Surface s) { cutBelow(s.alpha, 0.5); }");
  CHECK(dissolve.error.empty());
  CHECK(dissolve.cutout);
  const CustomShaderCode hard = parseCustomShader("void surface(inout Surface s) { if (s.alpha < 0.5) discard; }");
  CHECK(!hard.cutout);
  const CustomShaderCode commented = parseCustomShader("void surface(inout Surface s) { /* cutBelow(1.0, 0.5); */ }");
  CHECK(!commented.cutout);
  const CustomShaderCode blended =
      parseCustomShader("#pragma blend\nvoid surface(inout Surface s) { cutBelow(1.0, 0.5); }");
  CHECK(!blended.cutout); // blending draws never use it
}
