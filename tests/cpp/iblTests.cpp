// Image-based lighting: the cube maps made from the sky's panorama (made again when it changes, freed when it goes), what
// they do to a metal sphere on screen, the uniform environment of a sky with only a color, and the Skybox's
// environmentIntensity in scene files and the API (the Lua side is tests/lua/environment.lua). The panoramas are
// tests/fixtures/sky/*.hdr (see tools/make_test_sky.py).

#include "components/skybox.h"
#include "describe.h"
#include "foundation/core/engine.h"
#include "rendering/ibl.h"
#include "systems/renderSystem.h"
#include "testing.h"
#include <SDL3/SDL.h>
#include <cmath>
#include <fstream>
#include <functional>
#include <memory>
#include <nlohmann/json.hpp>
#include <sstream>
#include <vector>

namespace {
const char *const kBlueSky = "../tests/fixtures/sky/sky_blue.hdr";
const char *const kRedSky = "../tests/fixtures/sky/sky_red.hdr";

const RenderSystem &renderOf(Engine &engine) { return engine.getSystemManager().getSystem<RenderSystem>(); }

// The average color of one face of a cube map level 0 (RGB, linear)
glm::vec3 averageOfFace(GLuint cube, int face, int size) {
  std::vector<float> texels(static_cast<size_t>(size) * size * 4);
  glBindTexture(GL_TEXTURE_CUBE_MAP, cube);
  glGetTexImage(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, 0, GL_RGBA, GL_FLOAT, texels.data());
  glm::vec3 sum(0.0f);
  for (size_t i = 0; i < texels.size(); i += 4)
    sum += glm::vec3(texels[i], texels[i + 1], texels[i + 2]);
  return sum / static_cast<float>(size * size);
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

float brightness(const glm::vec3 &color) { return color.x + color.y + color.z; }

// A metal ball in front of the camera, lit by a light that gives nothing (so the sky is the only light)
void makeMetalBall(Engine &engine) {
  Light none = Light::directional(glm::vec3(0.0f, -1.0f, 0.0f), glm::vec3(1.0f), 0.0f);
  none.ambient = 0.0f;
  engine.entities().create("NoLight").withTransform(glm::vec3(0.0f, 10.0f, 0.0f)).withLight(none).build();
  GameEntity ball = engine.create("Ball", Desc("sphere").at(0, 2, 0).size(4));
  ball.setMetallic(1.0f);
  ball.setRoughness(0.3f);
}

std::string readFile(const std::string &path) {
  std::ifstream in(path);
  std::stringstream text;
  text << in.rdbuf();
  return text.str();
}
} // namespace

TEST_CASE(ibl_maps_from_a_panorama) {
  steps.step(2, [&engine]() {
    CHECK(!renderOf(engine).ibl().hasMaps()); // no sky: no maps
    engine.entities().create("Sky").withSkybox(kBlueSky, 1.0f).build();
  });
  steps.step(3, [&engine]() {
    const Ibl &ibl = renderOf(engine).ibl();
    CHECK(ibl.hasMaps());
    CHECK_EQ(ibl.generations(), 1);
    CHECK(ibl.irradiance() != 0 && ibl.prefiltered() != 0 && ibl.brdfLut() != 0);
    CHECK_EQ(int(glGetError()), int(GL_NO_ERROR));
    // The irradiance is not black, and it follows the image: the sky above is blue, the ground below a dark brown
    const glm::vec3 up = averageOfFace(ibl.irradiance(), 2, Ibl::kIrradianceSize);
    const glm::vec3 down = averageOfFace(ibl.irradiance(), 3, Ibl::kIrradianceSize);
    CHECK(up.x + up.y + up.z > 0.3f);
    CHECK(up.z > up.x);
    CHECK(down.x > down.z);
    CHECK(glm::length(down) > 0.01f);
    // The pre-filtered map's level 0 is the environment itself, which holds the sun: well above 1
    CHECK(averageOfFace(ibl.prefiltered(), 2, Ibl::kPrefilterSize).z > 0.3f);
    CHECK(ibl.lastGpuMs() > 0.0);
    // The rotation and the sky's intensity change nothing about the maps: they are applied when lighting
    Skybox &sky = engine.get<Skybox>(engine.find("Sky").id());
    sky.rotation = 90.0f;
    sky.intensity = 2.0f;
  });
  steps.step(3, [&engine]() { CHECK_EQ(renderOf(engine).ibl().generations(), 1); });
  // Another image: made again
  steps.step(0, [&engine]() { engine.get<Skybox>(engine.find("Sky").id()).path = kRedSky; });
  steps.step(3, [&engine]() {
    const Ibl &ibl = renderOf(engine).ibl();
    CHECK(ibl.hasMaps());
    CHECK_EQ(ibl.generations(), 2);
    const glm::vec3 up = averageOfFace(ibl.irradiance(), 2, Ibl::kIrradianceSize);
    CHECK(up.x > up.z); // red now
    CHECK_EQ(int(glGetError()), int(GL_NO_ERROR));
  });
  // The same image again (toggling the field back and forth) is not made again until it differs
  steps.step(0, [&engine]() { engine.get<Skybox>(engine.find("Sky").id()).environmentIntensity = 0.5f; });
  steps.step(3, [&engine]() { CHECK_EQ(renderOf(engine).ibl().generations(), 2); });
  // No image (or a missing one): the maps are freed and the sky's color lights alone
  steps.step(0, [&engine]() { engine.get<Skybox>(engine.find("Sky").id()).path = ""; });
  steps.step(3, [&engine]() { CHECK(!renderOf(engine).ibl().hasMaps()); });
  steps.step(0, [&engine]() { engine.get<Skybox>(engine.find("Sky").id()).path = "../tests/fixtures/sky/missing.hdr"; });
  steps.step(3, [&engine]() {
    CHECK(!renderOf(engine).ibl().hasMaps());
    CHECK_EQ(renderOf(engine).ibl().generations(), 2);
  });
}

TEST_CASE(ibl_lights_a_metal_sphere) {
  const std::string dir = testing::tempDir("ibl_shots");
  const auto shots = std::make_shared<std::vector<glm::vec3>>();
  const auto shoot = [&engine, &steps, dir, shots](const std::string &name) {
    steps.step(3, [&engine, dir, name]() { engine.captureScreenshot(dir + "/" + name + ".bmp", 0.0f, false); });
    steps.step(2, [dir, name, shots]() { shots->push_back(centerOf(dir + "/" + name + ".bmp")); });
  };
  steps.step(0, [&engine]() { makeMetalBall(engine); });

  shoot("no_sky"); // 0: with no sky a metal sphere and a light that gives nothing is black
  steps.step(0, [&engine]() { engine.entities().create("Sky").withSkybox(kBlueSky, 1.0f).build(); });
  shoot("sky"); // 1: under the panorama it shows the sky
  steps.step(0, [&engine]() { engine.setEnvironmentIntensity(0.0f); });
  shoot("off"); // 2: the intensity 0 is the same as no sky lighting
  steps.step(0, [&engine]() {
    engine.setEnvironmentIntensity(1.0f);
    engine.get<Skybox>(engine.find("Sky").id()).path = "";
    engine.setSkyColor(glm::vec3(0.4f, 0.5f, 0.6f));
  });
  shoot("color"); // 3: a sky of one color lights from every direction
  steps.step(0, [&engine]() { engine.setSkyColor(glm::vec3(0.0f)); });
  shoot("black"); // 4: and a black one adds nothing

  steps.step(0, [shots]() {
    CHECK_EQ(shots->size(), size_t(5));
    if (shots->size() < 5)
      return;
    const auto &s = *shots;
    CHECK(brightness(s[0]) < 6.0f);
    CHECK(brightness(s[1]) > 60.0f);
    CHECK(brightness(s[2]) < 6.0f);
    CHECK(brightness(s[3]) > 60.0f);
    CHECK(s[3].z > s[3].x); // the bluish color
    CHECK(brightness(s[4]) < 6.0f);
  });
}

TEST_CASE(ibl_environment_intensity_api_and_scene_files) {
  CHECK_NEAR(engine.environmentIntensity(), 1.0f, 1e-6f); // no sky yet: the default
  engine.setEnvironmentIntensity(0.25f);                   // makes the scene's Skybox
  GameEntity sky = engine.find("Sky");
  CHECK(sky.valid());
  CHECK_NEAR(engine.get<Skybox>(sky.id()).environmentIntensity, 0.25f, 1e-6f);
  CHECK_NEAR(engine.environmentIntensity(), 0.25f, 1e-6f);
  engine.setEnvironmentIntensity(-3.0f);
  CHECK_NEAR(engine.environmentIntensity(), 0.0f, 1e-6f);
  engine.setEnvironmentIntensity(0.5f);
  CHECK_NEAR(Skybox().environmentIntensity, 1.0f, 1e-6f);

  const std::string file = testing::tempDir("ibl_scene") + "/scene.json";
  CHECK(testing::saveWholeScene(engine, file));
  const std::string saved = readFile(file);
  CHECK(saved.find("\"environmentIntensity\"") != std::string::npos);

  // A file from before: no such field
  nlohmann::json json = nlohmann::json::parse(saved);
  int removed = 0;
  std::function<void(nlohmann::json &)> visit = [&](nlohmann::json &node) {
    if (node.is_object()) {
      removed += static_cast<int>(node.erase("environmentIntensity"));
      for (auto &item : node.items())
        visit(item.value());
    } else if (node.is_array()) {
      for (auto &item : node)
        visit(item);
    }
  };
  visit(json);
  CHECK_EQ(removed, 1);
  std::ofstream(file) << json.dump(2);

  engine.setEnvironmentIntensity(0.1f);
  engine.loadScene(file);
  steps.until([&engine]() { return !engine.isLoadingScene(); }, "the scene to load", 300);
  steps.step(1, [&engine]() { CHECK_NEAR(engine.environmentIntensity(), 1.0f, 1e-6f); });
}
