// Reflection probes: what a probe in a closed dark room does to a metal ball and a white one (the reflections and the
// diffuse light), the blend at the box's edge, box projection, baking to files and reading them back (stale and missing
// files), probes of additive scenes, and the spherical harmonics. The room is six slabs lit by nothing but a bright sky
// (tests/fixtures/sky/sky_blue.hdr), so what a probe changes is easy to see in the middle of the screen.

#include "components/reflectionProbe.h"
#include "components/transform.h"
#include "describe.h"
#include "foundation/core/engine.h"
#include "rendering/reflectionProbes.h"
#include "rendering/sphericalHarmonics.h"
#include "systems/editorUISystem.h"
#include "systems/renderSystem.h"
#include "systems/stateSystem.h"
#include "testing.h"
#include <SDL3/SDL.h>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <vector>

namespace {
const char *const kBlueSky = "../tests/fixtures/sky/sky_blue.hdr";

RenderSystem &renderOf(Engine &engine) { return engine.getSystemManager().getSystem<RenderSystem>(); }

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

// A closed room, 25 across and 10 high, of near-black slabs, around the test camera (0, 2, 8), under the blue sky and a
// light that gives nothing: the sky is the only light, and it cannot get in but through the walls' own lighting
void makeRoom(Engine &engine) {
  const Light none = Light::directional(glm::vec3(0.0f, -1.0f, 0.0f), glm::vec3(1.0f), 0.0f);
  engine.entities().create("NoLight").withTransform(glm::vec3(0.0f, 10.0f, 0.0f)).withLight(none).build();
  engine.entities().create("Sky").withSkybox(kBlueSky, 1.0f).build();
  const float dark = 0.05f;
  engine.create("Floor", Desc("box").at(0, -1.2f, 0).size(25, 0.4f, 25).color(dark, dark, dark));
  engine.create("Ceiling", Desc("box").at(0, 8.2f, 0).size(25, 0.4f, 25).color(dark, dark, dark));
  engine.create("WallN", Desc("box").at(0, 3.5f, -12.2f).size(25, 10, 0.4f).color(dark, dark, dark));
  engine.create("WallS", Desc("box").at(0, 3.5f, 12.2f).size(25, 10, 0.4f).color(dark, dark, dark));
  engine.create("WallE", Desc("box").at(12.2f, 3.5f, 0).size(0.4f, 10, 25).color(dark, dark, dark));
  engine.create("WallW", Desc("box").at(-12.2f, 3.5f, 0).size(0.4f, 10, 25).color(dark, dark, dark));
}

GameEntity makeMetalBall(Engine &engine) {
  GameEntity ball = engine.create("Ball", Desc("sphere").at(0, 2, 0).size(4).color(1, 1, 1));
  ball.setMetallic(1.0f);
  ball.setRoughness(0.3f);
  return ball;
}

GameEntity makeWhiteBall(Engine &engine) {
  GameEntity ball = engine.create("Ball", Desc("sphere").at(0, 2, 0).size(4).color(1, 1, 1));
  ball.setMetallic(0.0f);
  ball.setRoughness(1.0f);
  return ball;
}

// A probe whose box covers the room (a little past its walls), looking from above the ball
GameEntity makeProbe(Engine &engine, const glm::vec3 &position = glm::vec3(0, 5, 4), const glm::vec3 &size = glm::vec3(26, 12, 26),
                     float blend = 1.0f) {
  Entity probe = engine.entities().create("Probe").withTransform(position).withComponent<ReflectionProbe>().build();
  ReflectionProbe &component = engine.get<ReflectionProbe>(probe);
  component.size = size;
  component.blendDistance = blend;
  component.resolution = 64;
  return engine.entity(probe);
}

using Shots = std::shared_ptr<std::vector<glm::vec3>>;

// Takes a screenshot after 3 frames and keeps the color of its middle
void shoot(Engine &engine, testing::Steps &steps, const std::string &dir, const std::string &name, const Shots &shots) {
  steps.step(3, [&engine, dir, name]() { engine.captureScreenshot(dir + "/" + name + ".bmp", 0.0f, false); });
  steps.step(2, [dir, name, shots]() { shots->push_back(centerOf(dir + "/" + name + ".bmp")); });
}

std::string readFile(const std::string &path) {
  std::ifstream in(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}
} // namespace

TEST_CASE(reflection_probe_in_a_dark_room_darkens_a_metal_sphere) {
  const std::string dir = testing::tempDir("probe_metal");
  const Shots shots = std::make_shared<std::vector<glm::vec3>>();
  steps.step(0, [&engine]() {
    makeRoom(engine);
    makeMetalBall(engine);
  });
  shoot(engine, steps, dir, "no_probe", shots); // 0: the ball reflects the sky's lighting of the room: bright
  steps.step(0, [&engine]() { makeProbe(engine); });
  shoot(engine, steps, dir, "probe", shots); // 1: with a probe it reflects the room: dark
  steps.step(0, [&engine]() {
    CHECK_EQ(engine.reflectionProbeCount(), 1);
    CHECK_EQ(engine.reflectionProbeCaptures(), 1);
    CHECK(!engine.isReflectionProbeStale(engine.find("Probe").id()));
    CHECK_EQ(int(glGetError()), int(GL_NO_ERROR));
    engine.destroyEntity(engine.find("Probe").id());
  });
  shoot(engine, steps, dir, "removed", shots); // 2: and bright again once it is gone
  steps.step(0, [&engine, shots]() {
    CHECK_EQ(engine.reflectionProbeCount(), 0);
    CHECK_EQ(shots->size(), size_t(3));
    if (shots->size() < 3)
      return;
    const auto &s = *shots;
    CHECK(brightness(s[0]) > 120.0f);
    CHECK(brightness(s[1]) < brightness(s[0]) * 0.5f);
    CHECK(testing::isNear(s[2], s[0], 2.0f));
    printf("[probe metal] no probe %.0f, probe %.0f, removed %.0f\n", brightness(s[0]), brightness(s[1]), brightness(s[2]));
  });
}

TEST_CASE(reflection_probe_gives_a_white_sphere_the_rooms_diffuse_light) {
  const std::string dir = testing::tempDir("probe_diffuse");
  const Shots shots = std::make_shared<std::vector<glm::vec3>>();
  steps.step(0, [&engine]() {
    makeRoom(engine);
    makeWhiteBall(engine);
  });
  shoot(engine, steps, dir, "no_probe", shots); // 0
  steps.step(0, [&engine]() { makeProbe(engine); });
  shoot(engine, steps, dir, "probe", shots); // 1: the walls give it less than the sky did
  steps.step(0, [shots]() {
    CHECK_EQ(shots->size(), size_t(2));
    if (shots->size() < 2)
      return;
    const auto &s = *shots;
    printf("[probe diffuse] no probe %.0f, probe %.0f\n", brightness(s[0]), brightness(s[1]));
    CHECK(brightness(s[0]) > 60.0f);
    CHECK(brightness(s[1]) < brightness(s[0]) * 0.6f);
  });
}

TEST_CASE(reflection_probe_blends_with_the_sky_at_its_box_edge) {
  const std::string dir = testing::tempDir("probe_blend");
  const Shots shots = std::make_shared<std::vector<glm::vec3>>();
  steps.step(0, [&engine]() {
    makeRoom(engine);
    makeMetalBall(engine);
    // The ball (radius 4)'s middle pixel is 2 units inside this box's front and bottom faces
    makeProbe(engine, glm::vec3(0, 5, 0), glm::vec3(10, 10, 12), 0.5f);
  });
  shoot(engine, steps, dir, "sharp", shots); // 0: well inside the blend: all probe
  steps.step(0, [&engine]() { engine.get<ReflectionProbe>(engine.find("Probe").id()).blendDistance = 4.0f; });
  shoot(engine, steps, dir, "half", shots); // 1: about half probe, half sky
  steps.step(0, [&engine]() { engine.get<ReflectionProbe>(engine.find("Probe").id()).blendDistance = 40.0f; });
  shoot(engine, steps, dir, "faint", shots); // 2: almost none of it
  steps.step(0, [&engine]() { engine.destroyEntity(engine.find("Probe").id()); });
  shoot(engine, steps, dir, "sky", shots); // 3: the sky alone
  steps.step(0, [shots]() {
    CHECK_EQ(shots->size(), size_t(4));
    if (shots->size() < 4)
      return;
    const float probe = brightness((*shots)[0]), half = brightness((*shots)[1]);
    const float faint = brightness((*shots)[2]), sky = brightness((*shots)[3]);
    printf("[probe blend] sharp %.0f, blend 4 %.0f, blend 40 %.0f, sky %.0f\n", probe, half, faint, sky);
    CHECK(sky > probe * 2.0f);
    CHECK(half > probe + (sky - probe) * 0.2f);
    CHECK(half < sky - (sky - probe) * 0.2f);
    CHECK(faint > half);
  });
}

TEST_CASE(reflection_probe_box_projection_moves_the_reflection) {
  const std::string dir = testing::tempDir("probe_box");
  const Shots shots = std::make_shared<std::vector<glm::vec3>>();
  steps.step(0, [&engine]() {
    makeRoom(engine);
    makeMetalBall(engine);
    // A lamp on the wall behind the camera, which the middle of the ball reflects. The probe is off to the side, so a
    // lookup from its center straight back misses the lamp unless the box corrects it.
    engine.create("Lamp", Desc("box").at(0, 2, 11.8f).size(3, 3, 0.2f).color(1, 1, 1));
    engine.entity(engine.find("Lamp").id()).setEmission(glm::vec3(1.0f), 30.0f);
    makeProbe(engine, glm::vec3(-8, 2, 0), glm::vec3(26, 12, 26), 1.0f);
  });
  shoot(engine, steps, dir, "projected", shots); // 0: the lamp is there
  steps.step(0, [&engine]() { engine.get<ReflectionProbe>(engine.find("Probe").id()).boxProjection = false; });
  shoot(engine, steps, dir, "plain", shots); // 1: not
  steps.step(0, [shots]() {
    CHECK_EQ(shots->size(), size_t(2));
    if (shots->size() < 2)
      return;
    printf("[probe box] projected %.0f, plain %.0f\n", brightness((*shots)[0]), brightness((*shots)[1]));
    CHECK(brightness((*shots)[0]) > brightness((*shots)[1]) + 30.0f);
  });
}

TEST_CASE(reflection_probe_bake_files_round_trip) {
  const std::string dir = testing::tempDir("probe_bake");
  const std::string probeFile = dir + "/room.probe";
  const std::string sceneFile = dir + "/room.json";
  const Shots shots = std::make_shared<std::vector<glm::vec3>>();
  steps.step(0, [&engine, probeFile]() {
    makeRoom(engine);
    makeMetalBall(engine);
    GameEntity probe = makeProbe(engine);
    engine.get<ReflectionProbe>(probe.id()).bakedPath = probeFile; // an absolute path: where to write
  });
  steps.step(3, [&engine, probeFile, sceneFile]() {
    CHECK_EQ(engine.reflectionProbeCaptures(), 1); // never baked: captured when it appeared
    CHECK(!std::filesystem::exists(probeFile));
    CHECK_EQ(engine.bakeReflectionProbes(), 1);
    CHECK(std::filesystem::exists(probeFile));
    CHECK_EQ(engine.reflectionProbeCaptures(), 2);
    CHECK(!engine.isReflectionProbeStale(engine.find("Probe").id()));
    CHECK(testing::saveWholeScene(engine, sceneFile));
    CHECK(std::filesystem::file_size(probeFile) > 700000); // 5 levels of 6 RGB16F faces, 128 across
    printf("[probe bake] file %zu bytes\n", size_t(std::filesystem::file_size(probeFile)));
  });
  shoot(engine, steps, dir, "baked", shots); // 0

  // The scene loaded again reads the file: no capture
  steps.step(0, [&engine, sceneFile]() { engine.loadScene(sceneFile); });
  steps.until([&engine]() { return !engine.isLoadingScene(); }, "the scene to load", 300);
  shoot(engine, steps, dir, "reloaded", shots); // 1
  steps.step(0, [&engine, shots]() {
    CHECK_EQ(engine.reflectionProbeCaptures(), 2);
    CHECK_EQ(renderOf(engine).probes().fileLoadCount(), 1);
    CHECK_EQ(engine.reflectionProbeCount(), 1);
    CHECK_EQ(shots->size(), size_t(2));
    if (shots->size() == 2)
      CHECK(testing::isNear((*shots)[0], (*shots)[1], 1.5f));
    printf("[probe bake] baked %.0f, reloaded %.0f; last file read %.2f ms\n", brightness((*shots)[0]),
           brightness((*shots)[1]), renderOf(engine).probes().lastLoadMs());
  });

  // Moving the probe makes the file stale; baking again is the cure
  steps.step(0, [&engine]() {
    GameEntity probe = engine.find("Probe");
    CHECK(!probe.isReflectionProbeStale());
    probe.setPosition(glm::vec3(1.0f, 5.0f, 4.0f));
    CHECK(probe.isReflectionProbeStale());
    CHECK(probe.refreshReflectionProbe()); // in memory: the file's hash still differs from what is saved
    CHECK(!probe.isReflectionProbeStale());
    CHECK(probe.bakeReflectionProbe());
    CHECK(!probe.isReflectionProbeStale());
  });
  steps.step(0, [&engine]() { CHECK_EQ(engine.reflectionProbeCaptures(), 4); });

  // A missing file: captured when the scene loads
  steps.step(0, [&engine, probeFile, sceneFile]() {
    std::filesystem::remove(probeFile);
    engine.loadScene(sceneFile);
  });
  steps.until([&engine]() { return !engine.isLoadingScene(); }, "the scene to load", 300);
  steps.step(3, [&engine, probeFile]() {
    CHECK_EQ(engine.reflectionProbeCaptures(), 5);
    CHECK_EQ(engine.reflectionProbeCount(), 1);
    // An unreadable one (not a probe file): the same
    std::ofstream(probeFile, std::ios::binary) << "this is not a probe";
  });
  steps.step(0, [&engine, sceneFile]() { engine.loadScene(sceneFile); });
  steps.until([&engine]() { return !engine.isLoadingScene(); }, "the scene to load", 300);
  steps.step(3, [&engine]() {
    CHECK_EQ(engine.reflectionProbeCaptures(), 6);
    CHECK_EQ(engine.reflectionProbeCount(), 1);
  });
  // A file of another version is not used either
  steps.step(0, [&engine, probeFile, sceneFile]() {
    CHECK(engine.bakeReflectionProbes() == 1);
    std::string bytes = readFile(probeFile);
    CHECK(bytes.size() > 8);
    bytes[4] = 99; // the version
    std::ofstream(probeFile, std::ios::binary) << bytes;
    engine.loadScene(sceneFile);
  });
  steps.until([&engine]() { return !engine.isLoadingScene(); }, "the scene to load", 300);
  steps.step(3, [&engine]() { CHECK_EQ(engine.reflectionProbeCaptures(), 8); }); // the bake above, and this capture
}

TEST_CASE(reflection_probes_follow_their_additive_scene) {
  const std::string base = "../tests/fixtures/additive_a.json";
  const std::string room = "../tests/fixtures/probe_room.json";
  engine.loadScene(base);
  steps.until([&engine]() { return !engine.isLoadingScene(); }, "the first scene", 300);
  steps.step(3, [&engine]() { CHECK_EQ(engine.reflectionProbeCount(), 0); });
  steps.step(0, [&engine, room]() { engine.loadSceneAdditive(room); });
  steps.until([&engine]() { return !engine.isLoadingScene(); }, "the probe room", 300);
  steps.step(3, [&engine]() {
    CHECK_EQ(engine.reflectionProbeCount(), 1);
    CHECK_EQ(engine.reflectionProbeCaptures(), 1); // its file does not exist: captured at load
    CHECK_EQ(int(glGetError()), int(GL_NO_ERROR));
  });
  steps.step(0, [&engine, room]() { CHECK(engine.unloadScene(room)); });
  steps.step(3, [&engine]() {
    CHECK_EQ(engine.reflectionProbeCount(), 0);
    CHECK(!engine.find("Room Probe").valid());
    CHECK_EQ(int(glGetError()), int(GL_NO_ERROR));
  });
  steps.step(0, [&engine, room]() { engine.loadSceneAdditive(room); });
  steps.until([&engine]() { return !engine.isLoadingScene(); }, "the probe room again", 300);
  steps.step(3, [&engine]() { CHECK_EQ(engine.reflectionProbeCount(), 1); });
}

TEST_CASE(reflection_probe_scene_files_and_old_scenes) {
  // The component's fields in a scene file, and a file without them (an old scene) loading with the defaults
  const std::string file = testing::tempDir("probe_json") + "/probe.json";
  {
    std::ofstream out(file);
    out << R"({"format":"modular-engine-scene","version":1,"entities":[
      {"name":"P","components":{"Transform":{"position":[1,2,3]},"ReflectionProbe":{"size":[4,5,6],"intensity":0.5}}},
      {"name":"Q","components":{"Transform":{},"ReflectionProbe":{}}}]})";
  }
  engine.loadScene(file);
  steps.until([&engine]() { return !engine.isLoadingScene(); }, "the scene to load", 300);
  steps.step(1, [&engine]() {
    const ReflectionProbe &p = engine.get<ReflectionProbe>(engine.find("P").id());
    CHECK_NEAR(p.size, glm::vec3(4, 5, 6), 1e-6f);
    CHECK_NEAR(p.intensity, 0.5f, 1e-6f);
    CHECK_NEAR(p.blendDistance, 1.0f, 1e-6f); // not in the file: the default
    CHECK(p.boxProjection);
    CHECK_EQ(p.resolution, 128);
    CHECK(p.bakedPath.empty());
    CHECK(engine.hasComponent(engine.find("P").id(), "ReflectionProbe"));
    CHECK_EQ(ReflectionProbe::clampResolution(100), 128);
    CHECK_EQ(ReflectionProbe::clampResolution(10), 64);
    CHECK_EQ(ReflectionProbe::clampResolution(1000), 256);
  });
  steps.step(0, [&engine]() {
    const std::string saved = testing::tempDir("probe_json2") + "/saved.json";
    CHECK(testing::saveWholeScene(engine, saved));
    const std::string text = readFile(saved);
    CHECK(text.find("\"ReflectionProbe\"") != std::string::npos);
    CHECK(text.find("\"blendDistance\"") != std::string::npos);
  });
}

TEST_CASE(spherical_harmonics_follow_the_environment) {
  // A cube map of one color, then of an upper hemisphere: the diffuse light they give in a few directions
  const int size = 16;
  std::vector<float> faces[6];
  auto fill = [&](auto radianceOf) {
    for (int face = 0; face < 6; ++face) {
      faces[face].assign(static_cast<size_t>(size) * size * 4, 1.0f);
      for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
          const float u = 2.0f * (x + 0.5f) / size - 1.0f, v = 2.0f * (y + 0.5f) / size - 1.0f;
          glm::vec3 d;
          switch (face) {
          case 0: d = glm::vec3(1, -v, -u); break;
          case 1: d = glm::vec3(-1, -v, u); break;
          case 2: d = glm::vec3(u, 1, v); break;
          case 3: d = glm::vec3(u, -1, -v); break;
          case 4: d = glm::vec3(u, -v, 1); break;
          default: d = glm::vec3(-u, -v, -1); break;
          }
          faces[face][(static_cast<size_t>(y) * size + x) * 4] = radianceOf(glm::normalize(d));
        }
    }
  };
  const float *pointers[6];
  auto project = [&]() {
    for (int i = 0; i < 6; ++i)
      pointers[i] = faces[i].data();
    return sh::fromCubeFaces(pointers, size);
  };
  fill([](const glm::vec3 &) { return 2.0f; });
  Sh9 uniform = project();
  for (const glm::vec3 &n : {glm::vec3(0, 1, 0), glm::vec3(1, 0, 0), glm::normalize(glm::vec3(1, 1, 1))})
    CHECK_NEAR(sh::evaluate(uniform, n).x, 2.0f, 0.02f);
  fill([](const glm::vec3 &d) { return d.y > 0.0f ? 1.0f : 0.0f; });
  Sh9 hemisphere = project();
  const float up = sh::evaluate(hemisphere, glm::vec3(0, 1, 0)).x;
  const float side = sh::evaluate(hemisphere, glm::vec3(1, 0, 0)).x;
  const float down = sh::evaluate(hemisphere, glm::vec3(0, -1, 0)).x;
  printf("[sh] hemisphere: up %.3f (1), side %.3f (0.5), down %.3f (0)\n", up, side, down);
  CHECK_NEAR(up, 1.0f, 0.08f);
  CHECK_NEAR(side, 0.5f, 0.05f);
  CHECK_NEAR(down, 0.0f, 0.08f);
}

// The editor draws a selected probe's box and its Properties section (set MODULAR_TEST_SCREENSHOT to a .bmp path to
// look at them); the buttons are ImGui clicks this does not drive, their calls are tested above.
TEST_CASE(reflection_probe_editor_panel_draws) {
  steps.step(0, [&engine]() {
    makeRoom(engine);
    makeMetalBall(engine);
    GameEntity probe = makeProbe(engine, glm::vec3(0, 3, 0), glm::vec3(14, 8, 14), 2.0f);
    auto &editor = engine.getSystemManager().getSystem<EditorUISystem>();
    engine.getSystemManager().getSystem<StateSystem>().setToggle(Toggle::ShowUI, true);
    editor.selectedEntity = probe.id();
    if (std::getenv("MODULAR_TEST_SCREENSHOT"))
      engine.setWindowSize(1500, 1000);
  });
  steps.step(8, [&engine]() {
    if (const char *path = std::getenv("MODULAR_TEST_SCREENSHOT"))
      engine.captureScreenshot(path, 0.0f, false);
  });
  steps.step(3, [&engine]() {
    CHECK_EQ(int(glGetError()), int(GL_NO_ERROR));
    engine.getSystemManager().getSystem<StateSystem>().setToggle(Toggle::ShowUI, false);
  });
}
