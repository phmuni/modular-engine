// Clustered lighting: the automatic light range and its window, hundreds of point lights lighting what is under them
// (pixels, from a camera over a floor), the cap and the full index list, and which lights get the cube shadows.

#include "components/camera.h"
#include "components/light.h"
#include "describe.h"
#include "foundation/core/config.h"
#include "foundation/core/engine.h"
#include "foundation/core/logger.h"
#include "rendering/clusteredLights.h"
#include "systems/cameraSystem.h"
#include "systems/lightSystem.h"
#include "systems/renderSystem.h"
#include "testing.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <memory>
#include <set>
#include <vector>

namespace {
RenderSystem &renderOf(Engine &engine) { return engine.getSystemManager().getSystem<RenderSystem>(); }
LightSystem &lightsOf(Engine &engine) { return engine.getSystemManager().getSystem<LightSystem>(); }

// A screenshot's pixels (RGB, 0-255) and a way to read the one a world point lands on
struct Shot {
  int width = 0, height = 0;
  std::vector<glm::vec3> pixels;
  glm::mat4 viewProjection{1.0f};

  glm::vec3 at(const glm::vec3 &world) const {
    const glm::vec4 clip = viewProjection * glm::vec4(world, 1.0f);
    const glm::vec2 ndc = glm::vec2(clip) / clip.w;
    const int x = std::clamp(static_cast<int>((ndc.x * 0.5f + 0.5f) * width), 0, width - 1);
    const int y = std::clamp(static_cast<int>((1.0f - (ndc.y * 0.5f + 0.5f)) * height), 0, height - 1);
    return pixels[static_cast<size_t>(y) * width + x];
  }
};

float brightness(const glm::vec3 &color) { return color.x + color.y + color.z; }

Shot readShot(Engine &engine, const std::string &bmpPath) {
  Shot shot;
  SDL_Surface *loaded = SDL_LoadBMP(bmpPath.c_str());
  if (!loaded) {
    testing::context().fail("cannot read the screenshot " + bmpPath);
    return shot;
  }
  SDL_Surface *rgba = SDL_ConvertSurface(loaded, SDL_PIXELFORMAT_RGBA32);
  SDL_DestroySurface(loaded);
  if (!rgba)
    return shot;
  shot.width = rgba->w;
  shot.height = rgba->h;
  for (int y = 0; y < rgba->h; ++y)
    for (int x = 0; x < rgba->w; ++x) {
      const uint8_t *pixel = static_cast<const uint8_t *>(rgba->pixels) + y * rgba->pitch + x * 4;
      shot.pixels.emplace_back(pixel[0], pixel[1], pixel[2]);
    }
  SDL_DestroySurface(rgba);
  Camera &camera = engine.get<Camera>(engine.find("TestCamera").id());
  auto &cameras = engine.getSystemManager().getSystem<CameraSystem>();
  shot.viewProjection = cameras.getProjectionMatrix(camera) * cameras.getViewMatrix(camera);
  return shot;
}

void aimCamera(Engine &engine, const glm::vec3 &from, const glm::vec3 &target) {
  engine.get<Camera>(engine.find("TestCamera").id()).lookAt(from, target);
}

void makeFloor(Engine &engine) {
  engine.create("Floor", Desc("box").at(0, -0.1f, 0).size(100, 0.2f, 100).color(0.8f, 0.8f, 0.8f));
}

Entity makeLight(Engine &engine, const std::string &name, const glm::vec3 &position, const Light &light) {
  return engine.entities().create(name).withTransform(position).withLight(light).build();
}
} // namespace

TEST_CASE(clustered_light_range_and_window) {
  // The default attenuation (1, 0.09, 0.032) and intensity 1: where 1 / (c + l d + q d^2) is 1/256 of... the brightness
  // threshold 1/256: 0.032 d^2 + 0.09 d + 1 = 256
  const float range = lightRange(1.0f, 0.09f, 0.032f, 1.0f);
  CHECK_NEAR(range, 87.8f, 0.2f);
  const float brightnessAtRange = 1.0f / (1.0f + 0.09f * range + 0.032f * range * range);
  CHECK_NEAR(brightnessAtRange, EngineConfig::LIGHT_RANGE_THRESHOLD, 1e-5f);
  // A brighter light reaches further, a dimmer one less; a light below the threshold at its center reaches nowhere
  CHECK(lightRange(1.0f, 0.09f, 0.032f, 4.0f) > range);
  CHECK(lightRange(1.0f, 0.09f, 0.032f, 0.25f) < range);
  CHECK_NEAR(lightRange(1.0f, 0.09f, 0.032f, 0.0f), 0.0f, 1e-9f);
  // No falloff at all: the largest range, not infinity
  CHECK_NEAR(lightRange(1.0f, 0.0f, 0.0f, 1.0f), EngineConfig::LIGHT_MAX_RANGE, 1e-3f);
  // Linear and quadratic alone
  CHECK_NEAR(lightRange(1.0f, 1.0f, 0.0f, 1.0f), 255.0f, 0.5f);
  CHECK_NEAR(lightRange(1.0f, 0.0f, 1.0f, 1.0f), std::sqrt(255.0f), 0.01f);

  // The window: 1 close to the light (so the brightness there is the old formula's), smooth, exactly 0 from the range on
  CHECK_NEAR(lightWindow(0.0f, range), 1.0f, 1e-7f);
  CHECK(lightWindow(0.1f * range, range) > 0.9997f);
  CHECK(lightWindow(2.0f, range) > 0.999999f); // 2 units from a light with the default attenuation
  CHECK_NEAR(lightWindow(0.5f * range, range), 0.8789f, 1e-3f); // (1 - 1/16)^2
  CHECK(lightWindow(0.9f * range, range) < 0.2f);
  CHECK(lightWindow(0.9999f * range, range) < 1e-3f);
  CHECK_EQ(lightWindow(range, range), 0.0f);
  CHECK_EQ(lightWindow(range * 3.0f, range), 0.0f);
  float previous = 1.0f;
  for (int i = 1; i <= 100; ++i) {
    const float value = lightWindow(range * i / 100.0f, range);
    CHECK(value <= previous);
    previous = value;
  }
}

TEST_CASE(light_attenuation_curve) {
  // Inverse-square-like and soft at the light: 1 there, 1/2 at the core radius (1/25 of the range, inside the
  // window), exactly 0 at the range; the shape scales with the range
  for (float range : {4.0f, 40.0f, 400.0f}) {
    const float core = range * EngineConfig::LIGHT_CORE_FRACTION;
    CHECK_NEAR(lightAttenuation(0.0f, range), 1.0f, 1e-7f);
    CHECK_NEAR(lightAttenuation(core, range), lightWindow(core, range) * 0.5f, 1e-6f);
    CHECK_NEAR(lightAttenuation(range * 0.2f, range), lightWindow(range * 0.2f, range) / 26.0f, 1e-6f); // 5 cores out: 1 / (1 + 25)
    CHECK_EQ(lightAttenuation(range, range), 0.0f);
    CHECK_EQ(lightAttenuation(range + 1.0f, range), 0.0f);
    CHECK(lightAttenuation(0.999f * range, range) < 1e-5f);
    float previous = 1.0f;
    for (int i = 1; i <= 100; ++i) {
      const float value = lightAttenuation(range * i / 100.0f, range);
      CHECK(value <= previous);
      previous = value;
    }
  }
  CHECK_EQ(lightAttenuation(1.0f, 0.0f), 0.0f);
  // Near the light it is inverse-square: 1 / (1 + (d / core)^2) of the window, a quarter as bright at 1.73 cores
  const float far = 40.0f;
  const float d = std::sqrt(3.0f) * far * EngineConfig::LIGHT_CORE_FRACTION;
  CHECK_NEAR(lightAttenuation(d, far), lightWindow(d, far) * 0.25f, 1e-6f);
}

TEST_CASE(clustered_lights_limits_and_defines) {
  steps.step(2, [&engine]() {
    const LightingLimits &limits = lightingLimits();
    CHECK(limits.maxLights >= 1 && limits.maxLights <= EngineConfig::MAX_CLUSTERED_LIGHTS);
    CHECK(limits.maxUniformBlockSize >= 16384); // the GL minimum
    CHECK(limits.lightBytes <= limits.maxUniformBlockSize);
    CHECK(limits.gridBytes <= limits.maxUniformBlockSize);
    CHECK(limits.indexBlockBytes <= limits.maxUniformBlockSize);
    CHECK_EQ(limits.indexBytes, limits.indexBlocks * limits.indexBlockBytes);
    const std::string defines = lightingShaderDefines();
    CHECK(defines.find("#define MAX_CLUSTER_LIGHTS " + std::to_string(limits.maxLights)) != std::string::npos);
    CHECK(defines.find("#define CLUSTER_GRID_Z 24") != std::string::npos);
    CHECK_EQ(int(glGetError()), int(GL_NO_ERROR));
  });
}

TEST_CASE(clustered_lights_pixels_and_range) {
  const std::string dir = testing::tempDir("clustered_pixels");
  const auto shot = std::make_shared<Shot>();
  const auto heatShot = std::make_shared<Shot>();
  steps.step(0, [&engine]() {
    makeFloor(engine);
    // 180 lights on a grid over x 0..38, z -16..16 (created first), then one alone far to the -x side: the 181st, which
    // the old 10-light limit never reached. Short range (12 units), so the grid does not blow out and bloom
    // into the dark floor around it.
    const Light light = Light::point(glm::vec3(1.0f), 2.0f).withRange(12.0f);
    for (int ix = 0; ix < 20; ++ix)
      for (int iz = 0; iz < 9; ++iz)
        makeLight(engine, "Grid", glm::vec3(ix * 2.0f, 1.5f, -16.0f + iz * 4.0f), light);
    makeLight(engine, "Lonely", glm::vec3(-40.0f, 1.5f, 0.0f), light);
    aimCamera(engine, glm::vec3(0.0f, 70.0f, 30.0f), glm::vec3(0.0f, 0.0f, 0.0f));
  });
  steps.step(3, [&engine, dir]() { engine.captureScreenshot(dir + "/lit.bmp", 0.0f, false); });
  steps.step(2, [&engine, dir, shot]() { *shot = readShot(engine, dir + "/lit.bmp"); });
  steps.step(0, [&engine]() { engine.setLightHeatMap(true); });
  steps.step(3, [&engine, dir]() { engine.captureScreenshot(dir + "/heat.bmp", 0.0f, false); });
  steps.step(2, [&engine, dir, heatShot]() {
    *heatShot = readShot(engine, dir + "/heat.bmp");
    engine.setLightHeatMap(false);
  });
  steps.step(0, [&engine, shot, heatShot]() {
    CHECK(shot->width > 0 && !shot->pixels.empty());
    if (shot->pixels.empty())
      return;
    const ClusteredLights::Stats &stats = renderOf(engine).lightStats();
    CHECK_EQ(stats.lights, 181);
    CHECK_EQ(lightsOf(engine).visibleLightCount(), 181);
    CHECK_EQ(stats.dropped, 0);
    CHECK(stats.pairs > 181);
    LOG_I("[test] 181 lights: %d pairs, cluster build %.3f ms", stats.pairs, stats.buildMs);
    CHECK_EQ(int(glGetError()), int(GL_NO_ERROR));

    // Under a grid light, and under the lonely 181st (a light of the 11th or later is lit now): lit
    CHECK(brightness(shot->at(glm::vec3(20.0f, 0.0f, 0.0f))) > 60.0f);
    CHECK(brightness(shot->at(glm::vec3(-40.0f, 0.0f, 0.0f))) > 60.0f);
    // Far from every light (the nearest is more than its 12 units away): exactly dark
    CHECK(brightness(shot->at(glm::vec3(-40.0f, 0.0f, 30.0f))) < 3.0f);
    CHECK(brightness(shot->at(glm::vec3(46.0f, 0.0f, 30.0f))) < 3.0f);
    CHECK(brightness(shot->at(glm::vec3(-20.0f, 0.0f, 0.0f))) < 3.0f); // between the lonely light and the grid

    // The heat map colors what lights reach it, and leaves the dark floor gray (not black)
    CHECK(!heatShot->pixels.empty());
    if (!heatShot->pixels.empty()) {
      const glm::vec3 lit = heatShot->at(glm::vec3(20.0f, 0.0f, 0.0f));
      const glm::vec3 dark = heatShot->at(glm::vec3(-40.0f, 0.0f, 30.0f));
      CHECK(lit != shot->at(glm::vec3(20.0f, 0.0f, 0.0f)));
      CHECK(dark.x > 10.0f && std::abs(dark.x - dark.y) < 8.0f); // gray
      CHECK(lit.x - lit.z > 30.0f || lit.z - lit.x > 30.0f || lit.y - lit.z > 30.0f); // not gray
    }
  });
}

TEST_CASE(clustered_light_beyond_its_range_adds_nothing) {
  const std::string dir = testing::tempDir("clustered_range");
  const auto shot = std::make_shared<Shot>();
  const auto range = std::make_shared<float>(0.0f);
  steps.step(0, [&engine, range]() {
    makeFloor(engine);
    const Light light = Light::point(glm::vec3(1.0f), 1.0f).withRange(16.0f);
    *range = light.range;
    makeLight(engine, "Lamp", glm::vec3(0.0f, 0.5f, 0.0f), light);
    aimCamera(engine, glm::vec3(0.0f, 45.0f, 25.0f), glm::vec3(0.0f, 0.0f, 0.0f));
  });
  steps.step(3, [&engine, dir]() { engine.captureScreenshot(dir + "/range.bmp", 0.0f, false); });
  steps.step(2, [&engine, dir, shot, range]() {
    *shot = readShot(engine, dir + "/range.bmp");
    CHECK(!shot->pixels.empty());
    if (shot->pixels.empty())
      return;
    CHECK_NEAR(*range, 16.0f, 0.05f);
    // The floor is 0.5 below the light: a point 5 units out is lit, one 0.5 inside the range is faint at most, and
    // beyond the range the light adds exactly nothing (the floor has no other light, no ambient, no sky)
    CHECK(brightness(shot->at(glm::vec3(0.0f, 0.0f, 0.0f))) > 100.0f);
    CHECK(brightness(shot->at(glm::vec3(5.0f, 0.0f, 0.0f))) > 20.0f);
    CHECK_EQ(brightness(shot->at(glm::vec3(*range + 0.5f, 0.0f, 0.0f))), 0.0f);
    CHECK_EQ(brightness(shot->at(glm::vec3(0.0f, 0.0f, -(*range + 2.0f)))), 0.0f);
    CHECK_EQ(brightness(shot->at(glm::vec3(30.0f, 0.0f, 5.0f))), 0.0f);
  });
}

TEST_CASE(clustered_lights_over_the_cap_and_full_index_list) {
  const auto count = std::make_shared<int>(0);
  steps.step(0, [&engine, count]() {
    makeFloor(engine);
    // 300 lights with the default (far reaching) attenuation: more than the cap, and every cluster of the view holds
    // far more of them than the index list can: the least important are left out, with a warning (once)
    for (int i = 0; i < 300; ++i)
      makeLight(engine, "Crowd", glm::vec3(-20.0f + (i % 20) * 2.0f, 1.0f + (i % 7), -10.0f + (i / 20) * 1.5f),
                Light::point(glm::vec3(1.0f), 1.0f));
    *count = 300;
    aimCamera(engine, glm::vec3(0.0f, 20.0f, 20.0f), glm::vec3(0.0f, 0.0f, -5.0f));
  });
  steps.step(4, [&engine]() {
    const LightingLimits &limits = lightingLimits();
    const ClusteredLights::Stats &stats = renderOf(engine).lightStats();
    CHECK_EQ(lightsOf(engine).candidateLightCount(), 300);
    CHECK_EQ(lightsOf(engine).visibleLightCount(), limits.maxLights);
    CHECK_EQ(stats.lights, limits.maxLights);
    CHECK(stats.dropped > 0);
    CHECK(stats.indexEntries <= limits.indexBytes);
    CHECK(stats.buildMs >= 0.0);
    LOG_I("[test] %d lights over the cap: %d pairs, %d dropped, cluster build %.3f ms", stats.lights, stats.pairs,
          stats.dropped, stats.buildMs);
    CHECK_EQ(int(glGetError()), int(GL_NO_ERROR));
  });
  // A few more frames with the overflow: nothing crashes
  steps.step(5, [&engine]() { CHECK_EQ(int(glGetError()), int(GL_NO_ERROR)); });
}

TEST_CASE(clustered_light_shadows_go_to_the_most_important) {
  const auto lights = std::make_shared<std::vector<Entity>>();
  const auto slots = std::make_shared<std::vector<int>>(10, -1);
  const auto chosen = [&engine, lights](std::vector<int> &slotOf) {
    std::fill(slotOf.begin(), slotOf.end(), -1);
    for (const PointShadowCaster &caster : lightsOf(engine).pointShadows())
      for (size_t i = 0; i < lights->size(); ++i)
        if ((*lights)[i] == caster.entity)
          slotOf[i] = caster.slot;
  };
  steps.step(0, [&engine, lights]() {
    engine.setState(Toggle::Shadows, true);
    makeFloor(engine);
    // Ten shadowed lights in a row, light 0 the nearest to the first camera
    for (int i = 0; i < 10; ++i)
      lights->push_back(
          makeLight(engine, "Shadowed", glm::vec3(0.0f, 2.0f, 20.0f - 4.0f * i), Light::point(glm::vec3(1.0f), 1.0f).withShadows(25.0f)));
    aimCamera(engine, glm::vec3(0.0f, 10.0f, 45.0f), glm::vec3(0.0f, 0.0f, 0.0f));
  });
  steps.step(3, [&engine, slots, chosen]() {
    chosen(*slots);
    int count = 0;
    for (size_t i = 0; i < slots->size(); ++i) {
      const bool has = (*slots)[i] >= 0;
      count += has ? 1 : 0;
      CHECK_EQ(has, i < 8); // the 8 nearest
    }
    CHECK_EQ(count, EngineConfig::MAX_POINT_SHADOWS);
    CHECK_EQ(renderOf(engine).getPointShadowCount(), EngineConfig::MAX_POINT_SHADOWS);
    std::set<int> distinct(slots->begin(), slots->end());
    distinct.erase(-1);
    CHECK_EQ(distinct.size(), size_t(8)); // each in its own slot
    CHECK_EQ(int(glGetError()), int(GL_NO_ERROR));
  });
  const auto before = std::make_shared<std::vector<int>>(10, -1);
  steps.step(0, [&engine, slots, before]() {
    *before = *slots;
    aimCamera(engine, glm::vec3(0.0f, 10.0f, -45.0f), glm::vec3(0.0f, 0.0f, 0.0f)); // now light 9 is the nearest
  });
  steps.step(3, [&engine, slots, before, chosen]() {
    chosen(*slots);
    // The far end of the row now: lights 2 to 9 have shadows, 0 and 1 do not
    for (size_t i = 0; i < slots->size(); ++i)
      CHECK_EQ((*slots)[i] >= 0, i >= 2);
    // The ones that kept shadows kept their slots (no needless redraw of their cubes)
    for (size_t i = 2; i < 8; ++i)
      CHECK_EQ((*slots)[i], (*before)[i]);
    CHECK_EQ(int(glGetError()), int(GL_NO_ERROR));
  });
}

TEST_CASE(clustered_light_shadows_off_and_cube_array_size) {
  steps.step(0, [&engine]() {
    engine.setState(Toggle::Shadows, false);
    makeFloor(engine);
    for (int i = 0; i < 3; ++i)
      makeLight(engine, "Shadowed", glm::vec3(i * 3.0f, 2.0f, 0.0f), Light::point(glm::vec3(1.0f), 1.0f).withShadows(25.0f));
    aimCamera(engine, glm::vec3(0.0f, 10.0f, 15.0f), glm::vec3(0.0f, 0.0f, 0.0f));
  });
  steps.step(3, [&engine]() {
    CHECK(lightsOf(engine).pointShadows().empty());
    CHECK_EQ(lightsOf(engine).visibleLightCount(), 3);
    engine.setState(Toggle::Shadows, true);
  });
  steps.step(3, [&engine]() {
    CHECK_EQ(lightsOf(engine).pointShadows().size(), size_t(3));
    // The array: MAX_POINT_SHADOWS cubes of 6 layers
    GLint layers = 0, size = 0;
    glBindTexture(GL_TEXTURE_CUBE_MAP_ARRAY, engine.getSystemManager().getSystem<RenderSystem>().getRenderer().getPointShadowMap());
    glGetTexLevelParameteriv(GL_TEXTURE_CUBE_MAP_ARRAY, 0, GL_TEXTURE_DEPTH, &layers);
    glGetTexLevelParameteriv(GL_TEXTURE_CUBE_MAP_ARRAY, 0, GL_TEXTURE_WIDTH, &size);
    CHECK_EQ(int(layers), EngineConfig::MAX_POINT_SHADOWS * 6);
    CHECK_EQ(int(size), EngineConfig::POINT_SHADOW_SIZE);
    CHECK_EQ(int(glGetError()), int(GL_NO_ERROR));
  });
}

TEST_CASE(clustered_lights_dungeon_scene_with_256_lights) {
  // The dungeon of the regression case (180 lights, 5 with shadows), topped up to the cap of 256, seen from inside it:
  // the CPU cost of building the clusters, and pairs past the first index block (65536)
  engine.loadScene("../assets/scenes/many_lights.json");
  steps.until([&engine]() { return !engine.isLoadingScene(); }, "the scene to load", 300);
  steps.step(0, [&engine]() {
    const Light light = Light::point(glm::vec3(1.0f, 0.8f, 0.6f), 2.0f).withRange(9.1f);
    for (int i = 0; i < 76; ++i)
      makeLight(engine, "Extra", glm::vec3(-27.0f + i * 0.75f, 2.0f, (i % 5 - 2) * 4.0f), light);
  });
  steps.step(5, [&engine]() {
    const ClusteredLights::Stats &stats = renderOf(engine).lightStats();
    const int cap = std::min(256, lightingLimits().maxLights);
    CHECK_EQ(stats.lights, cap);
    // The index list holds all of this scene's pairs (it spans several blocks): no light is cut
    CHECK_EQ(stats.dropped, 0);
    CHECK(stats.indexEntries <= lightingLimits().indexBytes);
    CHECK(stats.pairs > 65536);
    // The build time is only logged: Debug builds and CI runners are many times slower than a Release laptop
    CHECK_EQ(lightsOf(engine).pointShadows().size(), size_t(5));
    LOG_I("[test] dungeon: %d lights, %d pairs of %d, cluster build %.3f ms", stats.lights, stats.pairs,
          lightingLimits().indexBytes, stats.buildMs);
    CHECK_EQ(int(glGetError()), int(GL_NO_ERROR));
  });
}
