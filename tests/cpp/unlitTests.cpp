// Unlit parts: the override per part (index, name, all) through the API and the material, the scene files keeping it,
// and what it shows on screen (a black unlit sphere under a light is exactly black, where a lit one is not). The Lua
// side is tests/lua/unlit.lua.

#include "components/model.h"
#include "describe.h"
#include "foundation/core/engine.h"
#include "rendering/resources/material.h"
#include "systems/resourceSystem.h"
#include "testing.h"
#include <SDL3/SDL.h>
#include <fstream>
#include <memory>
#include <sstream>
#include <vector>

namespace {
// Two parts: "metal" (part 0) and "plain" (part 1), as in pbrTests
const char *const kPbrModel = "../tests/fixtures/pbr/pbr.gltf";

ResourceSystem &resourcesOf(Engine &engine) { return engine.getSystemManager().getSystem<ResourceSystem>(); }

const Material &partMaterial(Engine &engine, const GameEntity &entity, size_t part) {
  return resourcesOf(engine).getMaterial(engine.get<Model>(entity.id()).materialHandles.at(part));
}

std::string readFile(const std::string &path) {
  std::ifstream in(path);
  std::stringstream text;
  text << in.rdbuf();
  return text.str();
}

// The color at the middle of a screenshot (0-255 per channel)
glm::vec3 centerOf(const std::string &bmpPath) {
  SDL_Surface *loaded = SDL_LoadBMP(bmpPath.c_str());
  if (!loaded) {
    testing::context().fail("cannot read the screenshot " + bmpPath);
    return glm::vec3(-1.0f);
  }
  SDL_Surface *rgba = SDL_ConvertSurface(loaded, SDL_PIXELFORMAT_RGBA32);
  SDL_DestroySurface(loaded);
  glm::vec3 color(-1.0f);
  if (rgba) {
    const uint8_t *row = static_cast<const uint8_t *>(rgba->pixels) + (rgba->h / 2) * rgba->pitch;
    const uint8_t *pixel = row + (rgba->w / 2) * 4;
    color = glm::vec3(pixel[0], pixel[1], pixel[2]);
    SDL_DestroySurface(rgba);
  }
  return color;
}
} // namespace

TEST_CASE(unlit_override_is_per_part) {
  GameEntity a = engine.create("A", Desc(kPbrModel).at(0, 0, 0));
  GameEntity b = engine.create("B", Desc(kPbrModel).at(3, 0, 0));
  CHECK_EQ(engine.get<Model>(a.id()).materialHandles.size(), size_t(2));
  CHECK(!partMaterial(engine, a, 0).isUnlit());
  CHECK(!partMaterial(engine, a, 1).isUnlit());

  // One part by number: only that part of that entity
  a.setUnlit(true, 1);
  CHECK(partMaterial(engine, a, 1).isUnlit());
  CHECK(!partMaterial(engine, a, 0).isUnlit());
  CHECK(!partMaterial(engine, b, 1).isUnlit());

  // By name: a part the model does not have changes nothing (a warning)
  a.setUnlit(true, "NoSuchPart");
  CHECK(!partMaterial(engine, a, 0).isUnlit());

  // Every part, and off again
  a.setUnlit(true);
  CHECK(partMaterial(engine, a, 0).isUnlit());
  CHECK(partMaterial(engine, a, 1).isUnlit());
  CHECK(!partMaterial(engine, b, 0).isUnlit());
  a.setUnlit(false, 0);
  CHECK(!partMaterial(engine, a, 0).isUnlit());
  CHECK(partMaterial(engine, a, 1).isUnlit());

  // The other values of the part are kept
  CHECK_NEAR(partMaterial(engine, a, 1).getRoughness(), 0.8f, 1e-6f);
}

TEST_CASE(unlit_scene_round_trip) {
  const std::string file = testing::tempDir("unlit_scene") + "/scene.json";
  GameEntity box = engine.create("UnlitBox", Desc("box"));
  GameEntity lit = engine.create("LitBox", Desc("box").at(3, 0, 0));
  box.setUnlit(true, 0);
  CHECK(testing::saveWholeScene(engine, file));

  // The unlit part is written, and the lit box has nothing to say about it
  const std::string saved = readFile(file);
  CHECK(saved.find("\"unlit\": true") != std::string::npos);
  CHECK_EQ(saved.find("\"unlit\""), saved.rfind("\"unlit\""));

  box.destroy();
  lit.destroy();
  engine.loadScene(file);
  steps.until([&engine]() { return !engine.isLoadingScene(); }, "the scene to load", 300);
  steps.step(1, [&engine]() {
    GameEntity loaded = engine.find("UnlitBox");
    CHECK(loaded.valid());
    if (!loaded.valid())
      return;
    CHECK(partMaterial(engine, loaded, 0).isUnlit());
    GameEntity other = engine.find("LitBox");
    CHECK(other.valid());
    if (other.valid())
      CHECK(!partMaterial(engine, other, 0).isUnlit());
  });
}

TEST_CASE(unlit_black_sphere_is_black_under_a_light) {
  const std::string dir = testing::tempDir("unlit_shots");
  const auto shots = std::make_shared<std::vector<glm::vec3>>();
  const auto shoot = [&engine, &steps, dir, shots](const std::string &name) {
    steps.step(3, [&engine, dir, name]() { engine.captureScreenshot(dir + "/" + name + ".bmp", 0.0f, false); });
    steps.step(2, [dir, name, shots]() { shots->push_back(centerOf(dir + "/" + name + ".bmp")); });
  };

  // A light, and a sphere in front of the camera: white and lit, as the control that the readback works
  steps.step(0, [&engine]() {
    Light sun = Light::directional(glm::vec3(0.0f, -1.0f, -0.5f), glm::vec3(1.0f), 1.0f);
    engine.entities().create("Sun").withLight(sun).build();
    GameEntity ball = engine.create("Ball", Desc("sphere").at(0, 2, 0).size(4));
    ball.setColor(glm::vec3(1.0f));
  });
  shoot("white_lit"); // 0
  // Black, lit: the lights shade it (only the reflection of the sun is left)
  steps.step(0, [&engine]() { engine.find("Ball").setColor(glm::vec3(0.0f)); });
  shoot("black_lit"); // 1
  // Black and unlit: the color alone, so exactly black
  steps.step(0, [&engine]() { engine.find("Ball").setUnlit(true); });
  shoot("black_unlit"); // 2

  steps.step(0, [&engine, shots]() {
    CHECK_EQ(shots->size(), size_t(3));
    if (shots->size() < 3)
      return;
    const auto &s = *shots;
    CHECK(s[0].x + s[0].y + s[0].z > 60.0f); // the control: a lit white sphere is bright
    CHECK(s[2].x < 1.0f && s[2].y < 1.0f && s[2].z < 1.0f); // the unlit black one is black
    CHECK(engine.find("Ball").valid());
    if (engine.find("Ball").valid())
      CHECK(partMaterial(engine, engine.find("Ball"), 0).isUnlit());
  });
}
