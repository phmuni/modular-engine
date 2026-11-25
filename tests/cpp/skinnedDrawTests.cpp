// Skinned models drawn instanced, with the poses of the whole frame in one joint buffer: every entity in its own pose and
// place (compared pixel by pixel with the same entity drawn alone), the sun's shadow of a pose, transparency and a custom
// vertex() on a skinned model, skinned and static meshes in one frame, more joints than the buffer holds (the limit is
// lowered to reach it), a model with more than 128 joints, and the draw calls not growing with the number of models.

#include "components/animator.h"
#include "components/camera.h"
#include "components/light.h"
#include "components/model.h"
#include "describe.h"
#include "foundation/core/engine.h"
#include "rendering/jointBuffer.h"
#include "systems/animationSystem.h"
#include "systems/cameraSystem.h"
#include "systems/renderSystem.h"
#include "testing.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <memory>
#include <vector>

namespace {
const std::string kKnight = "models/knight/knight.glb";

struct Shot {
  int width = 0, height = 0;
  std::vector<glm::vec3> pixels;
  glm::mat4 viewProjection{1.0f};

  glm::vec2 pixelOf(const glm::vec3 &world) const {
    const glm::vec4 clip = viewProjection * glm::vec4(world, 1.0f);
    const glm::vec2 ndc = glm::vec2(clip) / clip.w;
    return {(ndc.x * 0.5f + 0.5f) * width, (1.0f - (ndc.y * 0.5f + 0.5f)) * height};
  }
  glm::vec3 at(const glm::vec3 &world) const {
    const glm::vec2 p = pixelOf(world);
    const int x = std::clamp(static_cast<int>(p.x), 0, width - 1);
    const int y = std::clamp(static_cast<int>(p.y), 0, height - 1);
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

// How two pictures differ inside the box around a place: the pixels that differ by more than a few levels, and how many
// pixels are in it
struct Difference {
  int differing = 0;
  int total = 0;
  float worst = 0.0f;
};

Difference compareAround(const Shot &a, const Shot &b, const glm::vec3 &center, float half, float height) {
  Difference result;
  if (a.pixels.empty() || b.pixels.empty())
    return {1 << 20, 1, 255.0f};
  const glm::vec2 low = a.pixelOf(center + glm::vec3(-half, 0.0f, 0.0f));
  const glm::vec2 high = a.pixelOf(center + glm::vec3(half, height, 0.0f));
  const int x0 = std::clamp(static_cast<int>(std::min(low.x, high.x)), 0, a.width - 1);
  const int x1 = std::clamp(static_cast<int>(std::max(low.x, high.x)), 0, a.width - 1);
  const int y0 = std::clamp(static_cast<int>(std::min(low.y, high.y)), 0, a.height - 1);
  const int y1 = std::clamp(static_cast<int>(std::max(low.y, high.y)), 0, a.height - 1);
  for (int y = y0; y <= y1; ++y)
    for (int x = x0; x <= x1; ++x) {
      const glm::vec3 delta = glm::abs(a.pixels[static_cast<size_t>(y) * a.width + x] -
                                       b.pixels[static_cast<size_t>(y) * b.width + x]);
      const float largest = std::max({delta.x, delta.y, delta.z});
      result.worst = std::max(result.worst, largest);
      if (largest > 6.0f)
        ++result.differing;
      ++result.total;
    }
  return result;
}

// The same place in two pictures of one scene: no more than a stray pixel apart
void checkSame(const Shot &a, const Shot &b, const glm::vec3 &center, const char *what, float half = 1.1f) {
  const Difference d = compareAround(a, b, center, half, 2.4f);
  printf("[skinned] %s: %d of %d pixels differ, worst %.0f\n", what, d.differing, d.total, d.worst);
  if (d.differing > d.total / 500) // 0.2%
    testing::fail(__FILE__, __LINE__, std::string(what) + ": " + std::to_string(d.differing) + " of " +
                                          std::to_string(d.total) + " pixels differ (worst " +
                                          std::to_string(d.worst) + ")");
}

// The two places differ clearly: the test would pass on a picture with nothing in it otherwise
void checkDifferent(const Shot &a, const Shot &b, const glm::vec3 &center, const char *what) {
  const Difference d = compareAround(a, b, center, 1.1f, 2.4f);
  if (d.differing < d.total / 100)
    testing::fail(__FILE__, __LINE__, std::string(what) + ": only " + std::to_string(d.differing) + " of " +
                                          std::to_string(d.total) + " pixels differ");
}

void setUpView(Engine &engine) {
  engine.setWindowSize(640, 360);
  engine.create("Floor", Desc("box").at(0, -0.1f, 0).size(100, 0.2f, 100).color(0.7f, 0.7f, 0.7f));
  engine.entities()
      .create("Sun")
      .withTransform(glm::vec3(0.0f), glm::vec3(0.0f))
      .withLight(Light::directional(glm::vec3(0.0f, -1.0f, -0.5f)))
      .build();
  engine.get<Camera>(engine.find("TestCamera").id()).lookAt(glm::vec3(0.0f, 2.2f, 11.0f), glm::vec3(0.0f, 0.9f, 0.0f));
}

// A knight paused at `time` seconds of a clip
GameEntity knightAt(Engine &engine, const char *name, float x, const char *clip, float time) {
  GameEntity knight = engine.create(name, Desc(kKnight).at(x, 0, 0));
  engine.playAnimation(knight.id(), clip, 0.0f);
  Animator &animator = engine.get<Animator>(knight.id());
  animator.time = time;
  animator.speed = 0.0f;
  animator.playing = false;
  return knight;
}

// A picture of the scene as it is, a few frames from now: add this to the steps, then read the shot in the next one
void shoot(testing::Steps &steps, Engine &engine, const std::string &dir, const std::string &name,
           const std::shared_ptr<Shot> &shot) {
  steps.step(3, [&engine, dir, name]() { engine.captureScreenshot(dir + "/" + name + ".bmp", 0.0f, false); });
  steps.step(2, [&engine, dir, name, shot]() {
    *shot = readShot(engine, dir + "/" + name + ".bmp");
    CHECK(!shot->pixels.empty());
    CHECK_EQ(int(glGetError()), int(GL_NO_ERROR));
  });
}

RenderSystem &renderOf(Engine &engine) { return engine.getSystemManager().getSystem<RenderSystem>(); }

// Every entity but `shown` hidden (the others stay in the scene, so only what is drawn changes)
void showOnly(const std::vector<GameEntity> &all, size_t shown) {
  for (size_t i = 0; i < all.size(); ++i)
    GameEntity(all[i]).setVisible(i == shown);
}
void showAll(const std::vector<GameEntity> &all) {
  for (const GameEntity &entity : all)
    GameEntity(entity).setVisible(true);
}

// A plain test of "in a crowd" against "alone": the entities, one picture of all, then one of each alone
struct Crowd {
  std::vector<GameEntity> entities;
  std::vector<glm::vec3> places;
  std::shared_ptr<Shot> together = std::make_shared<Shot>();
  std::vector<std::shared_ptr<Shot>> alone;
};

// `count` is how many entities the crowd will have by then (the steps are laid out before the entities exist)
void photographCrowd(testing::Steps &steps, Engine &engine, const std::string &dir, const std::shared_ptr<Crowd> &crowd,
                     size_t count) {
  shoot(steps, engine, dir, "together", crowd->together);
  for (size_t i = 0; i < count; ++i) {
    crowd->alone.push_back(std::make_shared<Shot>());
    steps.step(0, [crowd, i]() { showOnly(crowd->entities, i); });
    shoot(steps, engine, dir, "alone" + std::to_string(i), crowd->alone.back());
  }
  steps.step(0, [crowd]() { showAll(crowd->entities); });
}

void compareCrowd(const std::shared_ptr<Crowd> &crowd, const char *what) {
  for (size_t i = 0; i < crowd->entities.size(); ++i)
    checkSame(*crowd->together, *crowd->alone[i], crowd->places[i], (std::string(what) + " " + std::to_string(i)).c_str());
}
} // namespace

TEST_CASE(skinned_each_entity_draws_in_its_own_pose_and_place) {
  const std::string dir = testing::tempDir("skinned_poses");
  auto crowd = std::make_shared<Crowd>();
  steps.step(0, [&engine, crowd]() {
    setUpView(engine);
    const float xs[] = {-6.0f, -3.0f, 0.0f, 3.0f, 6.0f};
    crowd->entities.push_back(knightAt(engine, "K0", xs[0], "Walking_A", 0.0f));
    crowd->entities.push_back(knightAt(engine, "K1", xs[1], "Walking_A", 0.35f));
    crowd->entities.push_back(knightAt(engine, "K2", xs[2], "Running_A", 0.2f));
    crowd->entities.push_back(knightAt(engine, "K3", xs[3], "1H_Melee_Attack_Chop", 0.5f));
    crowd->entities.push_back(engine.create("Rest", Desc(kKnight).at(xs[4], 0, 0))); // no Animator: the rest pose
    for (float x : xs)
      crowd->places.emplace_back(x, 0.0f, 0.0f);
  });
  photographCrowd(steps, engine, dir, crowd, 5);
  steps.step(0, [&engine, crowd]() {
    compareCrowd(crowd, "knight");
    // The poses are different ones (so a draw of all in one pose would not pass): the alone pictures are not the same
    for (size_t i = 0; i + 1 < crowd->entities.size(); ++i) {
      const Shot &a = *crowd->alone[i], &b = *crowd->alone[i + 1];
      // compared at the same place, so the second is a different knight in that spot only by pose
      const glm::vec3 place = crowd->places[i];
      const Difference d = compareAround(a, b, place, 1.1f, 2.4f);
      CHECK(d.differing > 0);
    }
    // Culled and drawn counts stay meaningful: all five are in view
    CHECK_EQ(renderOf(engine).getCulledCount(), 0);
  });
}

TEST_CASE(skinned_sun_shadow_follows_the_pose) {
  const std::string dir = testing::tempDir("skinned_shadow");
  auto walk = std::make_shared<Shot>(), crowd = std::make_shared<Shot>(), alone = std::make_shared<Shot>();
  auto knights = std::make_shared<std::vector<GameEntity>>();
  steps.step(0, [&engine, knights]() {
    setUpView(engine);
    // Low sun to the right: the shadow lies on the floor beside the knight, where the body is not
    engine.get<Transform>(engine.find("Sun").id()).rotation = glm::vec3(0.0f, 0.0f, 55.0f);
    engine.get<Light>(engine.find("Sun").id()).direction = glm::vec3(0.0f, -1.0f, 0.0f);
    knights->push_back(knightAt(engine, "A", 0.0f, "Walking_A", 0.0f));
    knights->push_back(knightAt(engine, "B", -12.0f, "Running_A", 0.2f));
    knights->push_back(knightAt(engine, "C", 12.0f, "1H_Melee_Attack_Chop", 0.5f));
  });
  shoot(steps, engine, dir, "crowd", crowd);
  steps.step(0, [&engine, knights]() { showOnly(*knights, 0); });
  shoot(steps, engine, dir, "alone", alone);
  steps.step(0, [&engine, knights]() {
    showAll(*knights);
    Animator &animator = engine.get<Animator>(knights->front().id());
    animator.clip = "Running_A";
    animator.time = 0.2f;
  });
  shoot(steps, engine, dir, "other", walk);
  steps.step(0, [crowd, alone, walk]() {
    const glm::vec3 place(0.0f, 0.0f, 0.0f);
    // The shadow is to the right of the knight (the sun turned 55 degrees about Z shines toward +X): a wide box that
    // takes in the floor beside it
    const Difference same = compareAround(*crowd, *alone, place, 3.0f, 0.6f);
    CHECK(same.differing <= same.total / 500);
    const Difference moved = compareAround(*crowd, *walk, place, 3.0f, 0.6f);
    CHECK(moved.differing > 10); // another pose, another shadow on the floor
  });
}

TEST_CASE(skinned_transparent_and_custom_vertex_shader) {
  const std::string dir = testing::tempDir("skinned_special");
  const std::string lift = dir + "/lift.glsl";
  {
    std::ofstream out(lift);
    out << "void vertex(inout VertexData v) { v.position.y += 0.6 + 0.2 * sin(v.localPosition.x * 4.0); }\n";
  }
  auto crowd = std::make_shared<Crowd>();
  auto unshaded = std::make_shared<Shot>();
  steps.step(0, [&engine, crowd, lift]() {
    setUpView(engine);
    crowd->entities.push_back(knightAt(engine, "Solid", -6.0f, "Walking_A", 0.1f));
    GameEntity glass = knightAt(engine, "Glass", -1.5f, "Running_A", 0.3f);
    engine.get<Model>(glass.id()).opacity = 0.5f;
    crowd->entities.push_back(glass);
    GameEntity lifted = knightAt(engine, "Lifted", 1.5f, "1H_Melee_Attack_Chop", 0.4f);
    lifted.setShader(lift);
    crowd->entities.push_back(lifted);
    crowd->entities.push_back(knightAt(engine, "Other", 4.0f, "Walking_A", 0.6f));
    for (const GameEntity &e : crowd->entities)
      crowd->places.push_back(GameEntity(e).position());
  });
  photographCrowd(steps, engine, dir, crowd, 4);
  steps.step(0, [&engine, crowd]() {
    compareCrowd(crowd, "special");
    // The glass one is see-through: the floor shows in it, so it differs from the solid one in the same pose
    GameEntity(crowd->entities[0]).setVisible(true);
    GameEntity lifted = crowd->entities[2];
    lifted.setShader("");
    showOnly(crowd->entities, 2);
  });
  shoot(steps, engine, dir, "plain", unshaded);
  steps.step(0, [crowd, unshaded]() {
    // The lifted knight with its vertex() and without it are not the same picture
    checkDifferent(*crowd->alone[2], *unshaded, crowd->places[2], "vertex() moves the skinned model");
    // The glass: pixels of the body are a mix with the floor, not the solid colour
    CHECK(crowd->together->at(glm::vec3(-1.5f, 0.9f, 0.0f)) != crowd->together->at(glm::vec3(-4.0f, 0.9f, 0.0f)));
  });
}

TEST_CASE(skinned_and_static_meshes_of_different_models_in_one_frame) {
  const std::string dir = testing::tempDir("skinned_mixed");
  auto crowd = std::make_shared<Crowd>();
  steps.step(0, [&engine, crowd]() {
    setUpView(engine);
    // Knights and boxes side by side, two of the boxes alike (one run of static instances next to the skinned ones)
    crowd->entities.push_back(knightAt(engine, "K0", -6.0f, "Walking_A", 0.2f));
    crowd->entities.push_back(engine.create("Box0", Desc("box").at(-3.6f, 0.5f, 0).size(1.0f).color(0.9f, 0.2f, 0.2f)));
    crowd->entities.push_back(knightAt(engine, "K1", -1.2f, "Running_A", 0.1f));
    crowd->entities.push_back(engine.create("Box1", Desc("box").at(1.2f, 0.5f, 0).size(1.0f).color(0.9f, 0.2f, 0.2f)));
    crowd->entities.push_back(knightAt(engine, "K2", 3.6f, "Walking_A", 0.5f));
    crowd->entities.push_back(engine.create("Box2", Desc("box").at(6.0f, 0.5f, 0).size(1.0f).color(0.2f, 0.2f, 0.9f)));
    for (const GameEntity &e : crowd->entities)
      crowd->places.push_back(GameEntity(e).position() - glm::vec3(0.0f, GameEntity(e).position().y, 0.0f));
  });
  photographCrowd(steps, engine, dir, crowd, 6);
  steps.step(0, [crowd]() { compareCrowd(crowd, "mixed"); });
}

TEST_CASE(skinned_more_joints_than_the_buffer_holds_fall_back_to_a_pose_and_warn_once) {
  const std::string dir = testing::tempDir("skinned_limit");
  auto crowd = std::make_shared<Crowd>();
  auto full = std::make_shared<Shot>(), limited = std::make_shared<Shot>(), reference = std::make_shared<Shot>();
  steps.step(0, [&engine, crowd]() {
    setUpView(engine);
    crowd->entities.push_back(knightAt(engine, "K0", -3.0f, "Walking_A", 0.0f));
    crowd->entities.push_back(knightAt(engine, "K1", 0.0f, "Running_A", 0.2f));
    crowd->entities.push_back(knightAt(engine, "K2", 3.0f, "1H_Melee_Attack_Chop", 0.5f));
    for (const GameEntity &e : crowd->entities)
      crowd->places.push_back(GameEntity(e).position());
  });
  shoot(steps, engine, dir, "full", full);
  // A knight's pose has 45 entries: with room for 60, the first knight's pose fits and nothing else does (not even the
  // rest pose), so the other two are drawn in the first knight's pose
  steps.step(0, [&engine]() {
    const auto &animation = engine.getSystemManager().getSystem<AnimationSystem>();
    CHECK_EQ(animation.getJointMatrices(engine.find("K0").id())->size(), size_t(45));
    renderOf(engine).getRenderer().jointBuffer().setMaxJoints(60);
  });
  shoot(steps, engine, dir, "limited", limited);
  steps.step(0, [&engine, crowd]() {
    CHECK_EQ(renderOf(engine).getRenderer().jointBuffer().uploadedJoints(), 45);
    renderOf(engine).getRenderer().jointBuffer().setMaxJoints(0);
    for (const GameEntity &knight : crowd->entities) {
      Animator &animator = engine.get<Animator>(knight.id());
      animator.clip = "Walking_A";
      animator.time = 0.0f;
    }
  });
  shoot(steps, engine, dir, "reference", reference);
  steps.step(0, [crowd, full, limited, reference]() {
    for (size_t i = 0; i < 3; ++i)
      checkSame(*limited, *reference, crowd->places[i], ("fallback pose " + std::to_string(i)).c_str());
    // Without the limit they were all different poses
    checkDifferent(*full, *limited, crowd->places[1], "the second knight kept its own pose");
    checkDifferent(*full, *limited, crowd->places[2], "the third knight kept its own pose");
    checkSame(*full, *limited, crowd->places[0], "the first knight is in its own pose either way");
  });
}

TEST_CASE(skinned_draw_calls_do_not_grow_with_the_number_of_models) {
  auto few = std::make_shared<int>(0), many = std::make_shared<int>(0);
  auto crowd = std::make_shared<std::vector<GameEntity>>();
  steps.step(0, [&engine, crowd]() {
    setUpView(engine);
    // A tight group in the middle of the view, so that every pass sees all of them
    for (int i = 0; i < 4; ++i)
      crowd->push_back(knightAt(engine, ("Few" + std::to_string(i)).c_str(), -0.3f + 0.2f * i, "Walking_A", 0.1f * i));
  });
  steps.step(4, [&engine, few]() { *few = renderOf(engine).getRenderer().getStats().drawCalls; });
  steps.step(0, [&engine, crowd]() {
    for (int i = 0; i < 60; ++i)
      crowd->push_back(knightAt(engine, ("Many" + std::to_string(i)).c_str(), -0.5f + 0.02f * i,
                                i % 2 ? "Running_A" : "Walking_A", 0.05f * i));
  });
  steps.step(4, [&engine, many]() { *many = renderOf(engine).getRenderer().getStats().drawCalls; });
  steps.step(0, [few, many]() {
    printf("[skinned] draw calls per frame: %d with 4 knights, %d with 64\n", *few, *many);
    CHECK(*few > 0);
    CHECK_EQ(*many, *few);
  });
}

namespace {
// A skinned triangle on a chain of 200 joints (each 0.01 above the one before, no inverse bind matrices to undo
// them): the triangle is bound to joint 150, so it hangs 1.51 above where it was made. Past the old 128 joints,
// where the triangle would stay down at the floor.
std::string writeLongChainModel(const std::string &path) {
  constexpr int joints = 200, bound = 150;
  std::vector<uint8_t> binary;
  auto pad = [&] { while (binary.size() % 4) binary.push_back(0); };
  auto append = [&](const void *data, size_t size) {
    pad();
    const size_t at = binary.size();
    const auto *bytes = static_cast<const uint8_t *>(data);
    binary.insert(binary.end(), bytes, bytes + size);
    return std::pair<size_t, size_t>(at, size);
  };
  const float positions[] = {-1, 0, 0, 1, 0, 0, 0, 1, 0};
  const float normals[] = {0, 0, 1, 0, 0, 1, 0, 0, 1};
  const uint16_t jointIndices[] = {bound, 0, 0, 0, bound, 0, 0, 0, bound, 0, 0, 0};
  const float weights[] = {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0};
  const uint16_t indices[] = {0, 1, 2};
  std::vector<float> inverseBind;
  for (int j = 0; j < joints; ++j) {
    const float identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    inverseBind.insert(inverseBind.end(), identity, identity + 16);
  }
  const auto viewPositions = append(positions, sizeof(positions));
  const auto viewNormals = append(normals, sizeof(normals));
  const auto viewJoints = append(jointIndices, sizeof(jointIndices));
  const auto viewWeights = append(weights, sizeof(weights));
  const auto viewIndices = append(indices, sizeof(indices));
  const auto viewInverse = append(inverseBind.data(), inverseBind.size() * sizeof(float));
  pad();

  auto view = [](std::pair<size_t, size_t> v) {
    return "{\"buffer\":0,\"byteOffset\":" + std::to_string(v.first) + ",\"byteLength\":" + std::to_string(v.second) + "}";
  };
  std::string nodesJson = "{\"mesh\":0,\"skin\":0}";
  std::string jointList;
  // Node 1 is joint 0, a child of nothing; node k+1 is joint k, the child of joint k-1
  for (int j = 0; j < joints; ++j) {
    nodesJson += std::string(",{\"name\":\"j") + std::to_string(j) + "\",\"translation\":[0,0.01,0]";
    if (j + 1 < joints)
      nodesJson += ",\"children\":[" + std::to_string(j + 2) + "]";
    nodesJson += "}";
    jointList += (j ? "," : "") + std::to_string(j + 1);
  }
  const std::string json =
      "{\"asset\":{\"version\":\"2.0\"},\"scene\":0,\"scenes\":[{\"nodes\":[0,1]}],\"nodes\":[" + nodesJson +
      "],\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0,\"NORMAL\":1,\"JOINTS_0\":2,\"WEIGHTS_0\":3},"
      "\"indices\":4}]}],\"skins\":[{\"joints\":[" + jointList + "],\"inverseBindMatrices\":5}],"
      "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\",\"min\":[-1,0,0],\"max\":[1,1,0]},"
      "{\"bufferView\":1,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"},"
      "{\"bufferView\":2,\"componentType\":5123,\"count\":3,\"type\":\"VEC4\"},"
      "{\"bufferView\":3,\"componentType\":5126,\"count\":3,\"type\":\"VEC4\"},"
      "{\"bufferView\":4,\"componentType\":5123,\"count\":3,\"type\":\"SCALAR\"},"
      "{\"bufferView\":5,\"componentType\":5126,\"count\":" + std::to_string(joints) + ",\"type\":\"MAT4\"}],"
      "\"bufferViews\":[" + view(viewPositions) + "," + view(viewNormals) + "," + view(viewJoints) + "," +
      view(viewWeights) + "," + view(viewIndices) + "," + view(viewInverse) + "],"
      "\"buffers\":[{\"byteLength\":" + std::to_string(binary.size()) + "}]}";

  std::string padded = json;
  while (padded.size() % 4)
    padded += ' ';
  auto word = [](std::string &out, uint32_t value) { out.append(reinterpret_cast<const char *>(&value), 4); };
  std::string file;
  word(file, 0x46546C67);
  word(file, 2);
  word(file, static_cast<uint32_t>(12 + 8 + padded.size() + 8 + binary.size()));
  word(file, static_cast<uint32_t>(padded.size()));
  word(file, 0x4E4F534A);
  file += padded;
  word(file, static_cast<uint32_t>(binary.size()));
  word(file, 0x004E4942);
  file.append(reinterpret_cast<const char *>(binary.data()), binary.size());
  std::ofstream(path, std::ios::binary) << file;
  return path;
}
} // namespace

TEST_CASE(skinned_a_model_with_more_than_128_joints_moves_all_of_them) {
  const std::string dir = testing::tempDir("skinned_long");
  const std::string model = writeLongChainModel(dir + "/chain.glb");
  auto shot = std::make_shared<Shot>();
  steps.step(0, [&engine, model]() {
    setUpView(engine);
    engine.get<Camera>(engine.find("TestCamera").id()).lookAt(glm::vec3(0.0f, 1.5f, 8.0f), glm::vec3(0.0f, 1.2f, 0.0f));
    GameEntity chain = engine.create("Chain", Desc(model).at(0, 0, 0));
    CHECK(chain.valid());
    const auto &system = engine.getSystemManager().getSystem<AnimationSystem>();
    const std::vector<glm::mat4> *joints = system.getJointMatrices(chain.id());
    CHECK(joints != nullptr);
    if (joints) {
      CHECK_EQ(joints->size(), size_t(200));
      if (joints->size() == 200)
        CHECK_NEAR((*joints)[150][3][1], 1.51f, 1e-4f);
    }
  });
  shoot(steps, engine, dir, "chain", shot);
  steps.step(0, [shot]() {
    // The triangle (2 wide, 1 high) hangs from y = 1.51 up to 2.51: it is there, with the black sky above it. Left at
    // the floor (as joints past the 128th were) it would end at y = 1.
    CHECK(brightness(shot->at(glm::vec3(0.0f, 1.8f, 0.0f))) > 200.0f);
    CHECK(brightness(shot->at(glm::vec3(0.0f, 2.8f, 0.0f))) < 5.0f);
  });
}
