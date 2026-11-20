// Ambient occlusion through the C++ API: its defaults and limits, and the scene's own radius and intensity, which the
// Skybox keeps and a scene file carries (the Lua side is tests/lua/ssao.lua).

#include "components/skybox.h"
#include "foundation/core/engine.h"
#include "testing.h"
#include <filesystem>

TEST_CASE(ssao_defaults_and_clamping) {
  CHECK(engine.ssao());
  SsaoSettings settings = engine.ssaoSettings();
  CHECK_NEAR(settings.radius, 0.5f, 1e-6f);
  CHECK_NEAR(settings.intensity, 1.0f, 1e-6f);
  CHECK(settings.quality == SsaoQuality::Medium);
  CHECK(settings.halfResolution);
  CHECK(!engine.ssaoDebugView());

  settings.radius = 99.0f;
  settings.intensity = -3.0f;
  settings.quality = SsaoQuality::High;
  engine.setSsaoSettings(settings);
  CHECK_NEAR(engine.ssaoSettings().radius, SsaoSettings::kMaxRadius, 1e-6f);
  CHECK_NEAR(engine.ssaoSettings().intensity, 0.0f, 1e-6f);
  CHECK(engine.ssaoSettings().quality == SsaoQuality::High);
  CHECK_EQ(ssaoSampleCount(SsaoQuality::Low), 8);
  CHECK_EQ(ssaoSampleCount(SsaoQuality::Medium), 16);
  CHECK_EQ(ssaoSampleCount(SsaoQuality::High), 32);

  engine.setSsao(false);
  CHECK(!engine.ssao());
  steps.step(2, [&engine]() {
    engine.setSsao(true);
    CHECK(engine.ssao());
  });
  steps.step(2, [&engine]() { CHECK(engine.ssao()); });
}

TEST_CASE(ssao_scene_values_round_trip) {
  const std::string file = testing::tempDir("ssao") + "/ssao_scene.json";
  engine.setSkyColor(glm::vec3(0.1f)); // makes the scene's Skybox
  GameEntity sky = engine.find("Sky");
  CHECK(sky.valid());

  SsaoSettings settings;
  settings.radius = 1.5f;
  settings.intensity = 2.0f;
  engine.setSsaoSettings(settings); // lands in the scene's Skybox
  CHECK_NEAR(engine.get<Skybox>(sky.id()).ssaoRadius, 1.5f, 1e-6f);
  CHECK(engine.saveScene(file));

  settings.radius = 0.2f;
  settings.intensity = 0.5f;
  engine.setSsaoSettings(settings);
  CHECK_NEAR(engine.ssaoSettings().radius, 0.2f, 1e-6f);

  engine.loadScene(file);
  steps.until([&engine]() { return !engine.isLoadingScene(); }, "the scene to load", 300);
  steps.step(1, [&engine]() {
    CHECK_NEAR(engine.ssaoSettings().radius, 1.5f, 1e-6f);
    CHECK_NEAR(engine.ssaoSettings().intensity, 2.0f, 1e-6f);
  });
}
