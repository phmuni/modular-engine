// Lights as in other engines: placed and aimed by their entity (local direction and offset, turned with the entity and
// with the parent it follows), a range, no per-light ambient; and the migration of scene files of format 1.

#include "components/camera.h"
#include "components/light.h"
#include "components/transform.h"
#include "describe.h"
#include "foundation/core/config.h"
#include "foundation/core/engine.h"
#include "rendering/clusteredLights.h"
#include "systems/cameraSystem.h"
#include "systems/lightSystem.h"
#include "systems/transformSystem.h"
#include "testing.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <nlohmann/json.hpp>
#include <vector>

namespace {
LightSystem &lightsOf(Engine &engine) { return engine.getSystemManager().getSystem<LightSystem>(); }

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

glm::vec3 rotated(const glm::vec3 &degrees, const glm::vec3 &vector) {
  Transform transform;
  transform.rotation = degrees;
  return glm::mat3(TransformSystem::calculateModelMatrix(transform)) * vector;
}

glm::vec3 directionOf(const GpuLight &light) { return glm::vec3(light.data[2]); }
glm::vec3 positionOf(const GpuLight &light) { return glm::vec3(light.data[0]); }

void waitForScene(testing::Steps &steps, Engine &engine) {
  steps.until([&engine]() { return !engine.isLoadingScene(); }, "the scene to load", 300);
}

Entity entityNamed(Engine &engine, const char *name) { return engine.findEntity(name); }
} // namespace

TEST_CASE(light_world_values_turn_with_the_entity) {
  // The offset and the direction are local: the world values are R * local (+ the position for the offset), R the
  // Transform's rotation in the engine's Euler order
  Light spot = Light::spot(glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(1.0f), 1.0f, 10.0f, 20.0f);
  spot.position = glm::vec3(0.0f, 0.0f, -2.0f);
  const glm::vec3 degrees(20.0f, 90.0f, -35.0f);
  Transform transform(glm::vec3(1.0f, 2.0f, 3.0f), degrees, glm::vec3(2.0f, 3.0f, 4.0f)); // scale does not matter
  const glm::vec3 direction = LightSystem::worldDirection(spot, &transform);
  const glm::vec3 position = LightSystem::worldPosition(spot, &transform);
  CHECK(testing::isNear(direction, rotated(degrees, glm::vec3(0.0f, 0.0f, -1.0f)), 1e-5f));
  CHECK(testing::isNear(position, glm::vec3(1.0f, 2.0f, 3.0f) + rotated(degrees, glm::vec3(0.0f, 0.0f, -2.0f)), 1e-5f));
  // Unturned: the local values are the world ones; a quarter turn about Y takes -Z to -X
  Transform plain(glm::vec3(1.0f, 2.0f, 3.0f), glm::vec3(0.0f), glm::vec3(1.0f));
  CHECK(testing::isNear(LightSystem::worldDirection(spot, &plain), glm::vec3(0.0f, 0.0f, -1.0f), 1e-6f));
  Transform quarter(glm::vec3(0.0f), glm::vec3(0.0f, 90.0f, 0.0f), glm::vec3(1.0f));
  CHECK(testing::isNear(LightSystem::worldDirection(spot, &quarter), glm::vec3(-1.0f, 0.0f, 0.0f), 1e-5f));
  CHECK(testing::isNear(LightSystem::worldPosition(spot, &quarter), glm::vec3(-2.0f, 0.0f, 0.0f), 1e-5f));
  // Without a Transform they are the local values
  CHECK(testing::isNear(LightSystem::worldDirection(spot, nullptr), glm::vec3(0.0f, 0.0f, -1.0f), 1e-6f));
  CHECK(testing::isNear(LightSystem::worldPosition(spot, nullptr), glm::vec3(0.0f, 0.0f, -2.0f), 1e-6f));
}

TEST_CASE(light_directional_follows_the_entity_rotation) {
  const auto sun = std::make_shared<Entity>(-1);
  steps.step(0, [&engine, sun]() {
    // The default direction is straight down; the entity is turned 90 degrees about X: down becomes -Z
    *sun = engine.entities().create("Sun").withTransform(glm::vec3(0.0f), glm::vec3(90.0f, 0.0f, 0.0f))
               .withLight(Light::directional(glm::vec3(0.0f, -1.0f, 0.0f))).build();
  });
  steps.step(3, [&engine, sun]() {
    const auto &directional = lightsOf(engine).directionalLights();
    CHECK_EQ(int(directional.size()), 1);
    if (directional.empty())
      return;
    CHECK(testing::isNear(directionOf(directional[0]), glm::vec3(0.0f, 0.0f, -1.0f), 1e-5f));
    // Turned again, the light follows the next frame
    engine.get<Transform>(*sun).rotation = glm::vec3(0.0f, 0.0f, 90.0f);
  });
  steps.step(3, [&engine]() {
    const auto &directional = lightsOf(engine).directionalLights();
    CHECK_EQ(int(directional.size()), 1);
    if (!directional.empty())
      CHECK(testing::isNear(directionOf(directional[0]), glm::vec3(1.0f, 0.0f, 0.0f), 1e-5f));
  });
}

TEST_CASE(light_offset_position_is_rotated) {
  steps.step(0, [&engine]() {
    Light lamp = Light::point(glm::vec3(1.0f), 1.0f).withRange(30.0f);
    lamp.position = glm::vec3(0.0f, 0.0f, -2.0f);
    engine.entities().create("Lamp").withTransform(glm::vec3(5.0f, 3.0f, 0.0f), glm::vec3(0.0f, 90.0f, 0.0f)).withLight(lamp).build();
    aimCamera(engine, glm::vec3(5.0f, 3.0f, 10.0f), glm::vec3(5.0f, 3.0f, 0.0f));
  });
  steps.step(3, [&engine]() {
    const auto &lights = lightsOf(engine).clusterLights();
    CHECK_EQ(int(lights.size()), 1);
    if (!lights.empty())
      CHECK(testing::isNear(positionOf(lights[0]), glm::vec3(3.0f, 3.0f, 0.0f), 1e-4f)); // -Z turned to -X
  });
}

TEST_CASE(light_spot_on_a_child_turns_with_its_parent) {
  const auto parent = std::make_shared<Entity>(-1);
  const std::string dir = testing::tempDir("light_child");
  const auto before = std::make_shared<Shot>();
  const auto after = std::make_shared<Shot>();
  steps.step(0, [&engine, parent]() {
    makeFloor(engine);
    *parent = engine.entities().create("Pivot").withTransform(glm::vec3(0.0f, 5.0f, 0.0f)).build();
    // A torch on the pivot: local direction down, a wide cone, reaching the floor 5 units below
    engine.entities()
        .create("Torch")
        .withTransform(glm::vec3(0.0f, 5.0f, 0.0f))
        .withParent(*parent)
        .withLight(Light::spot(glm::vec3(0.0f, -1.0f, 0.0f), glm::vec3(1.0f), 6.0f, 12.0f, 16.0f).withRange(100.0f))
        .build();
    aimCamera(engine, glm::vec3(0.0f, 45.0f, 25.0f), glm::vec3(0.0f, 0.0f, -4.0f));
  });
  steps.step(3, [&engine, dir]() {
    const auto &lights = lightsOf(engine).clusterLights();
    CHECK_EQ(int(lights.size()), 1);
    if (!lights.empty())
      CHECK(testing::isNear(directionOf(lights[0]), glm::vec3(0.0f, -1.0f, 0.0f), 1e-5f));
    engine.captureScreenshot(dir + "/before.bmp", 0.0f, false);
  });
  steps.step(2, [&engine, dir, before, parent]() {
    *before = readShot(engine, dir + "/before.bmp");
    // The parent leans 60 degrees about X: the torch's down becomes (0, -cos 60, -sin 60) and the pool of light moves
    // to z = -5 tan 60 = -8.66 on the floor
    engine.get<Transform>(*parent).rotation = glm::vec3(60.0f, 0.0f, 0.0f);
  });
  steps.step(3, [&engine, dir]() {
    const auto &lights = lightsOf(engine).clusterLights();
    CHECK_EQ(int(lights.size()), 1);
    if (!lights.empty())
      CHECK(testing::isNear(directionOf(lights[0]), glm::vec3(0.0f, -0.5f, -0.866025f), 1e-4f));
    engine.captureScreenshot(dir + "/after.bmp", 0.0f, false);
  });
  steps.step(2, [&engine, dir, before, after]() {
    *after = readShot(engine, dir + "/after.bmp");
    CHECK(!before->pixels.empty() && !after->pixels.empty());
    if (before->pixels.empty() || after->pixels.empty())
      return;
    const glm::vec3 below(0.0f, 0.0f, 0.0f), moved(0.0f, 0.0f, -8.66f);
    CHECK(brightness(before->at(below)) > 100.0f);
    CHECK(brightness(before->at(moved)) < 5.0f);
    CHECK(brightness(after->at(below)) < 5.0f);
    CHECK(brightness(after->at(moved)) > 100.0f);
  });
}

TEST_CASE(light_sun_shadow_follows_the_entity_rotation) {
  const std::string dir = testing::tempDir("light_sun_shadow");
  const auto shot = std::make_shared<Shot>();
  steps.step(0, [&engine]() {
    makeFloor(engine);
    engine.create("Pillar", Desc("box").at(0, 2.0f, 0).size(1, 4, 1).color(0.8f, 0.8f, 0.8f));
    // Straight down turned 45 degrees about Z: the light travels along (sin 45, -cos 45, 0), so the pillar's shadow
    // falls to +X. No other light, no ambient: shadow is black.
    engine.entities().create("Sun").withTransform(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 45.0f))
        .withLight(Light::directional(glm::vec3(0.0f, -1.0f, 0.0f))).build();
    aimCamera(engine, glm::vec3(0.0f, 40.0f, 20.0f), glm::vec3(0.0f, 0.0f, 0.0f));
  });
  steps.step(4, [&engine, dir]() { engine.captureScreenshot(dir + "/sun.bmp", 0.0f, false); });
  steps.step(2, [&engine, dir, shot]() {
    *shot = readShot(engine, dir + "/sun.bmp");
    CHECK(!shot->pixels.empty());
    if (shot->pixels.empty())
      return;
    CHECK(brightness(shot->at(glm::vec3(-4.0f, 0.0f, 0.0f))) > 100.0f);  // lit on the side the light comes from
    CHECK(brightness(shot->at(glm::vec3(2.5f, 0.0f, 0.0f))) < 5.0f);    // in the pillar's shadow, to +X
    CHECK(brightness(shot->at(glm::vec3(0.0f, 0.0f, 3.0f))) > 100.0f);   // and not to the side
  });
}

TEST_CASE(light_range_lights_inside_and_nothing_beyond) {
  const std::string dir = testing::tempDir("light_range");
  const auto shot = std::make_shared<Shot>();
  steps.step(0, [&engine]() {
    makeFloor(engine);
    engine.entities().create("Lamp").withTransform(glm::vec3(0.0f, 0.5f, 0.0f))
        .withLight(Light::point(glm::vec3(1.0f), 3.0f).withRange(24.0f)).build();
    aimCamera(engine, glm::vec3(0.0f, 45.0f, 25.0f), glm::vec3(0.0f, 0.0f, 0.0f));
  });
  steps.step(3, [&engine, dir]() { engine.captureScreenshot(dir + "/range.bmp", 0.0f, false); });
  steps.step(2, [&engine, dir, shot]() {
    *shot = readShot(engine, dir + "/range.bmp");
    CHECK(!shot->pixels.empty());
    if (shot->pixels.empty())
      return;
    CHECK(brightness(shot->at(glm::vec3(0.0f, 0.0f, 0.0f))) > 100.0f);
    CHECK(brightness(shot->at(glm::vec3(3.0f, 0.0f, 0.0f))) > 20.0f);
    CHECK_EQ(brightness(shot->at(glm::vec3(24.5f, 0.0f, 0.0f))), 0.0f); // beyond the range: exactly nothing
    CHECK_EQ(brightness(shot->at(glm::vec3(0.0f, 0.0f, -25.0f))), 0.0f);
    // Inside the range but far from the light (x = 11): fainter than near it
    CHECK(brightness(shot->at(glm::vec3(11.0f, 0.0f, 0.0f))) < brightness(shot->at(glm::vec3(3.0f, 0.0f, 0.0f))));
  });
}

TEST_CASE(light_default_range_and_with_range) {
  const Light light = Light::point();
  CHECK(light.range > 0.0f);
  CHECK_EQ(Light::point().withRange(7.5f).range, 7.5f);
  CHECK_EQ(Light::spot(glm::vec3(0, -1, 0), glm::vec3(1), 1.0f, 10.0f, 20.0f).withRange(3.0f).withShadows(9.0f).range, 3.0f);
  // Zero range reaches nowhere: no light is uploaded
  // (checked below with the scene's lights)
}

TEST_CASE(light_zero_range_is_not_uploaded) {
  steps.step(0, [&engine]() {
    engine.entities().create("Dead").withTransform(glm::vec3(0.0f, 2.0f, 0.0f)).withLight(Light::point().withRange(0.0f)).build();
    engine.entities().create("Live").withTransform(glm::vec3(0.0f, 2.0f, -3.0f)).withLight(Light::point().withRange(10.0f)).build();
    aimCamera(engine, glm::vec3(0.0f, 5.0f, 10.0f), glm::vec3(0.0f, 0.0f, 0.0f));
  });
  steps.step(3, [&engine]() { CHECK_EQ(int(lightsOf(engine).clusterLights().size()), 1); });
}

TEST_CASE(light_legacy_scene_is_converted_exactly) {
  const std::string file = "../tests/fixtures/scenes/legacy_lights_v1.json";
  engine.loadScene(file);
  waitForScene(steps, engine);
  steps.step(1, [&engine]() {
    auto &components = engine.getComponentManager();

    // The spot: old world values were direction (0.2, -1, 0.5) and offset (0.5, 1, -2), the entity's rotation ignored
    const Entity spot = entityNamed(engine, "Turned Spot");
    CHECK(spot != -1);
    const Light &light = components.get<Light>(spot);
    const Transform &transform = components.get<Transform>(spot);
    CHECK(testing::isNear(LightSystem::worldDirection(light, &transform), glm::vec3(0.2f, -1.0f, 0.5f), 1e-4f));
    CHECK(testing::isNear(LightSystem::worldPosition(light, &transform),
                          glm::vec3(1.0f, 2.0f, 3.0f) + glm::vec3(0.5f, 1.0f, -2.0f), 1e-4f));
    // The range: what the old constants (1, 0.14, 0.07) and brightness 1 * (2 + 0.1) reached
    CHECK_NEAR(light.range, lightRange(1.0f, 0.14f, 0.07f, 2.1f), 1e-3f);
    CHECK(light.range > 0.0f);

    // The sun on a turned entity: its world direction is the old one
    const Entity turnedSun = entityNamed(engine, "Turned Sun");
    CHECK(testing::isNear(LightSystem::worldDirection(components.get<Light>(turnedSun), &components.get<Transform>(turnedSun)),
                          glm::vec3(-0.4f, -1.0f, -0.6f), 1e-4f));

    // The lamp has the default attenuation and ambient (the file does not say): the range of 1 * ... (3 + 0.2)
    CHECK_NEAR(components.get<Light>(entityNamed(engine, "Lamp")).range, lightRange(1.0f, 0.09f, 0.032f, 3.2f), 1e-3f);

    // The suns' ambient went into the scene's (there was no Skybox: one was added)
    const Entity sky = entityNamed(engine, "Sky");
    CHECK(sky != -1);
    const glm::vec3 expected = glm::pow(glm::vec3(1.0f, 0.8f, 0.6f), glm::vec3(2.2f)) * 0.3f +
                               glm::pow(glm::vec3(0.6f, 0.7f, 1.0f), glm::vec3(2.2f)) * 0.1f;
    const glm::vec3 actual = glm::pow(engine.ambientColor(), glm::vec3(2.2f)) * engine.ambientIntensity();
    CHECK(testing::isNear(actual, expected, 1e-4f));
  });
  // Saving writes the new format: version 2, a range and no ambient, constant, linear or quadratic
  const std::string saved = testing::tempDir("light_legacy") + "/saved.json";
  steps.step(0, [&engine, saved]() { CHECK(testing::saveWholeScene(engine, saved)); });
  steps.step(1, [saved]() {
    std::ifstream in(saved);
    const nlohmann::json scene = nlohmann::json::parse(in, nullptr, false);
    CHECK(!scene.is_discarded());
    if (scene.is_discarded())
      return;
    CHECK_EQ(scene.value("version", 0), 2);
    int lights = 0;
    for (const auto &entity : scene["entities"]) {
      if (!entity["components"].contains("Light"))
        continue;
      ++lights;
      const auto &light = entity["components"]["Light"];
      CHECK(light.contains("range"));
      CHECK(!light.contains("ambient") && !light.contains("constant") && !light.contains("linear") &&
            !light.contains("quadratic"));
    }
    CHECK_EQ(lights, 4);
  });
}

TEST_CASE(light_legacy_scene_with_a_sky_adds_to_its_ambient) {
  engine.loadScene("../tests/fixtures/scenes/legacy_lights_sky_v1.json");
  waitForScene(steps, engine);
  steps.step(1, [&engine]() {
    // The Skybox's own ambient (0.5, 0.6, 0.8) * 0.1 plus the sun's: its color * 0.2, both in linear
    const glm::vec3 expected = glm::pow(glm::vec3(0.5f, 0.6f, 0.8f), glm::vec3(2.2f)) * 0.1f +
                               glm::pow(glm::vec3(1.0f, 0.9f, 0.8f), glm::vec3(2.2f)) * 0.2f;
    const glm::vec3 actual = glm::pow(engine.ambientColor(), glm::vec3(2.2f)) * engine.ambientIntensity();
    CHECK(testing::isNear(actual, expected, 1e-4f));
    int skies = 0;
    engine.getComponentManager().forEachComponent<Skybox>([&skies](Entity, Skybox &) { ++skies; });
    CHECK_EQ(skies, 1); // no second one
    CHECK(testing::isNear(engine.skyColor(), glm::vec3(0.3f, 0.4f, 0.5f), 1e-6f)); // the sky itself is untouched
  });
}

TEST_CASE(light_current_scenes_load_unchanged) {
  // A scene saved now is already format 2: loading it again converts nothing (the values come back as they were)
  const std::string file = testing::tempDir("light_current") + "/current.json";
  steps.step(0, [&engine, file]() {
    Light spot = Light::spot(glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(1.0f, 0.5f, 0.25f), 4.0f, 10.0f, 20.0f).withRange(33.0f);
    spot.position = glm::vec3(0.25f, 0.5f, -0.75f);
    engine.entities().create("Spot").withTransform(glm::vec3(1.0f), glm::vec3(0.0f, 70.0f, 0.0f)).withLight(spot).build();
    CHECK(testing::saveWholeScene(engine, file));
    engine.loadScene(file);
  });
  waitForScene(steps, engine);
  steps.step(1, [&engine]() {
    const Entity entity = entityNamed(engine, "Spot");
    CHECK(entity != -1);
    const Light &light = engine.get<Light>(entity);
    CHECK_NEAR(light.range, 33.0f, 1e-4f);
    CHECK(testing::isNear(light.direction, glm::vec3(0.0f, 0.0f, -1.0f), 1e-5f));
    CHECK(testing::isNear(light.position, glm::vec3(0.25f, 0.5f, -0.75f), 1e-5f));
  });
}
