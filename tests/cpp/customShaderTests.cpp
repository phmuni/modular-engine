// Custom surface shaders: the file format (no GL), setting a shader on one part or all of them without touching other
// entities of the same model, parameters, files that do not compile, reloading an edited file, and scenes and prefabs
// keeping the shader and its values.

#include "components/model.h"
#include "components/transform.h"
#include "describe.h"
#include "foundation/core/engine.h"
#include "rendering/resources/customShader.h"
#include "rendering/resources/material.h"
#include "systems/resourceSystem.h"
#include "systems/sceneSerializer.h"
#include "testing.h"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>

namespace {
const char *const kDissolve = "shaders/examples/dissolve.glsl";
const char *const kWater = "shaders/examples/water.glsl";

// A shader file with a float, a vec3 colour, a vec2, a vec4 and a texture, for the parameter tests
const char *const kParamShader = R"(
uniform float strength = 2.5;
uniform vec3 tintColor = vec3(0.1, 0.2, 0.3);
uniform vec2 scroll = vec2(1.0, 2.0);
uniform vec4 extra;
uniform sampler2D maskTexture;
// uniform float commentedOut = 9.0;
void surface(inout Surface s) {
    s.albedo *= tintColor * strength + texture(maskTexture, s.uv + scroll * time).rgb + extra.rgb;
}
)";

void writeFile(const std::string &path, const std::string &text) {
  std::ofstream out(path);
  out << text;
}

// A model file of two parts (two materials), since the engine's own models have one
std::string twoPartModel(const std::string &name) {
  const std::string dir = testing::tempDir(name);
  writeFile(dir + "/two.mtl", "newmtl red\nKd 1 0 0\nnewmtl blue\nKd 0 0 1\n");
  writeFile(dir + "/two.obj",
            "mtllib two.mtl\nv 0 0 0\nv 1 0 0\nv 0 1 0\nv 2 0 0\nv 3 0 0\nv 2 1 0\nvn 0 0 1\n"
            "o first\nusemtl red\nf 1//1 2//1 3//1\no second\nusemtl blue\nf 4//1 5//1 6//1\n");
  return dir + "/two.obj";
}

// A file edited just now can carry the same time as the one it replaced; move it on so a reload sees the change
void touchLater(const std::string &path) {
  std::filesystem::last_write_time(path, std::filesystem::file_time_type::clock::now() + std::chrono::seconds(5));
}

ResourceSystem &resourcesOf(Engine &engine) { return engine.getSystemManager().getSystem<ResourceSystem>(); }

uint32_t materialOf(Engine &engine, const GameEntity &entity, size_t part) {
  return engine.get<Model>(entity.id()).materialHandles.at(part);
}

uint32_t shaderHandleOf(Engine &engine, const GameEntity &entity, size_t part) {
  return resourcesOf(engine).getMaterial(materialOf(engine, entity, part)).getShaderHandle();
}
} // namespace

TEST_CASE(custom_shader_file_format) {
  const CustomShaderCode code = parseCustomShader(kParamShader);
  CHECK(code.error.empty());
  CHECK(code.hasSurface);
  CHECK(!code.hasVertex);
  CHECK(!code.blend);
  CHECK(code.castsShadow);
  CHECK_EQ(code.params.size(), size_t(5)); // the commented-out one is not a parameter
  CHECK_EQ(code.params[0].name, std::string("strength"));
  CHECK(code.params[0].type == ShaderParamType::Float);
  CHECK_NEAR(code.params[0].defaultValue.x, 2.5f, 1e-6f);
  CHECK(code.params[1].type == ShaderParamType::Vec3);
  CHECK_NEAR(glm::vec3(code.params[1].defaultValue), glm::vec3(0.1f, 0.2f, 0.3f), 1e-6f);
  CHECK(code.params[2].type == ShaderParamType::Vec2);
  CHECK_NEAR(code.params[2].defaultValue.y, 2.0f, 1e-6f);
  CHECK(code.params[3].type == ShaderParamType::Vec4);
  CHECK(code.params[4].type == ShaderParamType::Texture);
  CHECK(code.fragmentStage.find("surface") != std::string::npos);

  // Each stage only gets its own function; the shared part (uniforms, helpers) goes to both
  const CustomShaderCode both = parseCustomShader(
      "#pragma blend\n#pragma noshadow\nuniform float a;\nfloat helper(float x) { return x * a; }\n"
      "void vertex(inout VertexData v) { v.position.y += helper(1.0); }\n"
      "void surface(inout Surface s) { if (s.alpha > 0.5) { s.alpha = 1.0; } }\n");
  CHECK(both.error.empty());
  CHECK(both.hasSurface && both.hasVertex && both.blend && !both.castsShadow);
  CHECK(both.fragmentStage.find("void vertex") == std::string::npos);
  CHECK(both.fragmentStage.find("void surface") != std::string::npos);
  CHECK(both.vertexStage.find("void surface") == std::string::npos);
  CHECK(both.vertexStage.find("void vertex") != std::string::npos);
  CHECK(both.vertexStage.find("helper") != std::string::npos);
  // Line numbers in compile errors stay those of the file
  CHECK_EQ(std::count(both.fragmentStage.begin(), both.fragmentStage.end(), '\n'), std::ptrdiff_t(6));

  CHECK(!parseCustomShader("float nothing() { return 1.0; }").error.empty());
  CHECK(!parseCustomShader("").error.empty());

  // Values: a number fills a vector, a texture is a path
  CHECK(shaderValueFits(ShaderValue(1.0f), ShaderParamType::Vec3));
  CHECK(!shaderValueFits(ShaderValue(glm::vec2(1, 2)), ShaderParamType::Vec3));
  CHECK(!shaderValueFits(ShaderValue(std::string("a.png")), ShaderParamType::Float));
  CHECK(shaderValueFits(ShaderValue(std::string("a.png")), ShaderParamType::Texture));
  CHECK_NEAR(std::get<glm::vec3>(shaderValueAs(ShaderValue(0.5f), ShaderParamType::Vec3)), glm::vec3(0.5f), 1e-6f);
}

TEST_CASE(custom_shader_per_part_and_per_entity) {
  const std::string model = twoPartModel("shader_parts");
  GameEntity a = engine.create("A", Desc(model).at(0, 0, 0));
  GameEntity b = engine.create("B", Desc(model).at(3, 0, 0));
  const size_t parts = engine.get<Model>(a.id()).materialHandles.size();
  CHECK(parts >= 2);
  auto &resources = resourcesOf(engine);
  const uint32_t standard = resources.getMaterial(0).getShaderHandle();
  CHECK_EQ(a.shader(), std::string());
  CHECK_EQ(shaderHandleOf(engine, a, 0), standard);

  // One part: only that part of that entity
  a.setShader(kDissolve, 0);
  CHECK_EQ(a.shader(0), std::string(kDissolve));
  CHECK_EQ(a.shader(1), std::string());
  CHECK_EQ(a.shader(), std::string(kDissolve)); // the first part by default
  CHECK(shaderHandleOf(engine, a, 0) != standard);
  CHECK_EQ(shaderHandleOf(engine, a, 1), standard);
  CHECK_EQ(b.shader(0), std::string());
  CHECK_EQ(shaderHandleOf(engine, b, 0), standard);
  CHECK(materialOf(engine, a, 0) != materialOf(engine, b, 0));

  // Every part
  b.setShader(kDissolve);
  for (size_t part = 0; part < parts; ++part) {
    CHECK_EQ(b.shader(static_cast<int>(part)), std::string(kDissolve));
    CHECK(shaderHandleOf(engine, b, part) != standard);
  }
  CHECK_EQ(shaderHandleOf(engine, a, 0), shaderHandleOf(engine, b, 0)); // one compiled program for the file
  CHECK_EQ(resources.customShaderForHandle(shaderHandleOf(engine, a, 0))->path, std::string(kDissolve));
  CHECK(resources.customShaderForHandle(standard) == nullptr);

  // Parameters belong to the part, not to the entities sharing the file
  a.setShaderParam("dissolve", 0.9f, 0);
  b.setShaderParam("dissolve", 0.1f);
  CHECK_NEAR(std::get<float>(*a.shaderParam("dissolve", 0)), 0.9f, 1e-6f);
  CHECK_NEAR(std::get<float>(*b.shaderParam("dissolve", 0)), 0.1f, 1e-6f);

  // Back to the standard shader, for one part and for all
  a.setShader("", 0);
  CHECK_EQ(a.shader(0), std::string());
  CHECK_EQ(shaderHandleOf(engine, a, 0), standard);
  CHECK(!a.shaderParam("dissolve", 0).has_value());
  b.setShader("");
  for (size_t part = 0; part < parts; ++part)
    CHECK_EQ(shaderHandleOf(engine, b, part), standard);

  // A part that does not exist, and a handle without a model, do nothing
  a.setShader(kDissolve, static_cast<int>(parts) + 5);
  CHECK_EQ(a.shader(static_cast<int>(parts) + 5), std::string());
  GameEntity empty = engine.create("Empty", Desc());
  empty.setShader(kDissolve);
  CHECK_EQ(empty.shader(), std::string());

  // Frames with the shader in use (instancing, batches) go by without trouble
  a.setShader(kDissolve);
  steps.step(3, [&engine, a]() { CHECK(a.valid()); });
}

TEST_CASE(custom_shader_params) {
  const std::string file = testing::tempDir("shader_params") + "/params.glsl";
  writeFile(file, kParamShader);
  GameEntity e = engine.create("E", Desc("box"));
  e.setShader(file);
  CHECK_EQ(e.shader(), file);
  CHECK(resourcesOf(engine).findCustomShader(file)->ok);

  // The file's defaults until set
  CHECK_NEAR(std::get<float>(*e.shaderParam("strength")), 2.5f, 1e-6f);
  CHECK_NEAR(std::get<glm::vec3>(*e.shaderParam("tintColor")), glm::vec3(0.1f, 0.2f, 0.3f), 1e-6f);
  const auto maskTexture = e.shaderParam("maskTexture");
  CHECK(maskTexture.has_value());
  CHECK_EQ(std::get<std::string>(*maskTexture), std::string());
  CHECK(!e.shaderParam("nothing").has_value());

  // Each kind round-trips
  e.setShaderParam("strength", 4.0f);
  e.setShaderParam("tintColor", glm::vec3(1, 0.5f, 0.25f));
  e.setShaderParam("scroll", glm::vec2(3, 4));
  e.setShaderParam("extra", glm::vec4(1, 2, 3, 4));
  e.setShaderParam("maskTexture", std::string("models/box/diffuse.png"));
  CHECK_NEAR(std::get<float>(*e.shaderParam("strength")), 4.0f, 1e-6f);
  CHECK_NEAR(std::get<glm::vec3>(*e.shaderParam("tintColor")), glm::vec3(1, 0.5f, 0.25f), 1e-6f);
  CHECK_NEAR(std::get<glm::vec2>(*e.shaderParam("scroll")), glm::vec2(3, 4), 1e-6f);
  CHECK_NEAR(std::get<glm::vec4>(*e.shaderParam("extra")).w, 4.0f, 1e-6f);
  const auto setTexture = e.shaderParam("maskTexture");
  CHECK_EQ(std::get<std::string>(*setTexture), std::string("models/box/diffuse.png"));
  const Material &material = resourcesOf(engine).getMaterial(materialOf(engine, e, 0));
  CHECK(material.getShaderParams().at("maskTexture").texture != 0);

  // A number fills a vector; a wrong kind of value, or a name the shader does not have, changes nothing
  e.setShaderParam("tintColor", 0.5f);
  CHECK_NEAR(std::get<glm::vec3>(*e.shaderParam("tintColor")), glm::vec3(0.5f), 1e-6f);
  e.setShaderParam("strength", glm::vec3(1, 2, 3));
  CHECK_NEAR(std::get<float>(*e.shaderParam("strength")), 4.0f, 1e-6f);
  e.setShaderParam("strength", std::string("a.png"));
  CHECK_NEAR(std::get<float>(*e.shaderParam("strength")), 4.0f, 1e-6f);
  e.setShaderParam("nothing", 1.0f);
  CHECK(!e.shaderParam("nothing").has_value());

  // Without a custom shader there is nothing to set
  GameEntity plain = engine.create("Plain", Desc("box"));
  plain.setShaderParam("strength", 4.0f);
  CHECK(!plain.shaderParam("strength").has_value());

  steps.step(3, [e]() { CHECK(e.valid()); });
}

TEST_CASE(custom_shader_that_does_not_compile_falls_back) {
  const std::string dir = testing::tempDir("shader_broken");
  const std::string broken = dir + "/broken.glsl";
  const std::string noFunction = dir + "/nothing.glsl";
  const std::string fixable = dir + "/fixable.glsl";
  writeFile(broken, "void surface(inout Surface s) { s.albedo = vec3(; }\n");
  writeFile(noFunction, "uniform float a;\n");
  writeFile(fixable, "uniform float amount = 1.0;\nvoid surface(inout Surface s) { s.albedo = vec3(nonsense); }\n");

  auto &resources = resourcesOf(engine);
  const uint32_t standard = resources.getMaterial(0).getShaderHandle();
  GameEntity a = engine.create("A", Desc("box"));
  GameEntity b = engine.create("B", Desc("box"));
  GameEntity c = engine.create("C", Desc("box"));
  GameEntity missing = engine.create("Missing", Desc("box"));
  a.setShader(broken);
  b.setShader(noFunction);
  c.setShader(fixable);
  missing.setShader(dir + "/absent.glsl");

  // Each keeps drawing with the standard shader, and remembers what it was asked for
  for (GameEntity *e : {&a, &b, &c, &missing}) {
    CHECK_EQ(shaderHandleOf(engine, *e, 0), standard);
    CHECK(!resources.findCustomShader(e->shader())->ok);
  }
  CHECK_EQ(a.shader(), broken);
  // Values can still be stored for a shader that cannot be checked yet
  c.setShaderParam("amount", 3.0f);
  CHECK_NEAR(std::get<float>(*c.shaderParam("amount")), 3.0f, 1e-6f);

  // The engine goes on: frames pass, and the file fixed on disk takes over when it is read again
  steps.step(3, [&engine, c, fixable, standard]() {
    CHECK(c.valid());
    writeFile(fixable, "uniform float amount = 1.0;\nvoid surface(inout Surface s) { s.albedo *= amount; }\n");
    touchLater(fixable);
    CHECK(resourcesOf(engine).reloadChangedShaders());
    CHECK(shaderHandleOf(engine, c, 0) != standard);
    CHECK(resourcesOf(engine).customShaderForHandle(shaderHandleOf(engine, c, 0)) != nullptr);
    CHECK_NEAR(std::get<float>(*c.shaderParam("amount")), 3.0f, 1e-6f); // the value kept through the failure
  });

  // An edit that breaks it again keeps the last working program
  steps.step(2, [&engine, c, fixable]() {
    const uint32_t working = shaderHandleOf(engine, c, 0);
    writeFile(fixable, "void surface(inout Surface s) { oops }\n");
    touchLater(fixable);
    resourcesOf(engine).reloadChangedShaders();
    CHECK_EQ(shaderHandleOf(engine, c, 0), working);
    CHECK(resourcesOf(engine).findCustomShader(fixable)->ok);
  });
  steps.step(3, [c]() { CHECK(c.valid()); });
}

TEST_CASE(custom_shader_alpha_modes) {
  // `#pragma blend` makes the part blend (the transparent pass); the example water does, dissolve does not
  GameEntity water = engine.create("Water", Desc("box"));
  GameEntity dissolve = engine.create("Dissolve", Desc("box"));
  water.setShader(kWater);
  dissolve.setShader(kDissolve);
  auto &resources = resourcesOf(engine);
  CHECK(resources.getMaterial(materialOf(engine, water, 0)).getAlphaMode() == AlphaMode::Blend);
  CHECK(resources.getMaterial(materialOf(engine, dissolve, 0)).getAlphaMode() == AlphaMode::Opaque);
  CHECK(resources.hasBlendMaterials());
  CHECK(!resources.findCustomShader(kWater)->code.castsShadow);
  water.setShader("");
  CHECK(resources.getMaterial(materialOf(engine, water, 0)).getAlphaMode() == AlphaMode::Opaque);
  water.setShader(kWater);
  steps.step(3, [water]() { CHECK(water.valid()); });
}

TEST_CASE(custom_shader_scene_round_trip) {
  const std::string file = testing::tempDir("shader_scene") + "/scene.json";
  const std::string model = twoPartModel("shader_scene_model");
  GameEntity a = engine.create("A", Desc(model).at(0, 0, 0));
  GameEntity b = engine.create("B", Desc(model).at(3, 0, 0));
  GameEntity plain = engine.create("Plain", Desc("box").at(6, 0, 0));
  a.setShader(kDissolve, 0);
  a.setShaderParam("dissolve", 0.7f, 0);
  a.setShaderParam("edgeColor", glm::vec3(0.1f, 0.2f, 0.3f), 0);
  b.setShader(kWater);
  b.setShaderParam("waveHeight", 0.4f);
  CHECK(engine.saveScene(file));

  a.destroy();
  b.destroy();
  plain.destroy();
  engine.loadScene(file);
  steps.until([&engine]() { return !engine.isLoadingScene(); }, "the scene to load", 300);
  steps.step(1, [&engine]() {
    GameEntity a = engine.find("A");
    GameEntity b = engine.find("B");
    GameEntity plain = engine.find("Plain");
    CHECK(a.valid() && b.valid() && plain.valid());
    CHECK_EQ(a.shader(0), std::string(kDissolve));
    CHECK_EQ(a.shader(1), std::string()); // only the part that had it
    CHECK_NEAR(std::get<float>(*a.shaderParam("dissolve", 0)), 0.7f, 1e-5f);
    CHECK_NEAR(std::get<glm::vec3>(*a.shaderParam("edgeColor", 0)), glm::vec3(0.1f, 0.2f, 0.3f), 1e-5f);
    CHECK_NEAR(std::get<float>(*a.shaderParam("noiseScale", 0)), 5.0f, 1e-5f); // not set: the file's default
    const size_t parts = engine.get<Model>(b.id()).materialHandles.size();
    for (size_t part = 0; part < parts; ++part)
      CHECK_EQ(b.shader(static_cast<int>(part)), std::string(kWater));
    CHECK_NEAR(std::get<float>(*b.shaderParam("waveHeight")), 0.4f, 1e-5f);
    CHECK_EQ(plain.shader(), std::string());
  });
}

TEST_CASE(custom_shader_prefab_overrides) {
  const std::string dir = testing::tempDir("shader_prefab");
  const std::string prefab = dir + "/statue.json";
  const std::string scene = dir + "/scene.json";

  GameEntity statue = engine.create("Statue", Desc("box"));
  statue.setShader(kDissolve);
  statue.setShaderParam("dissolve", 0.3f);
  auto &serializer = engine.getSceneSerializer();
  CHECK(serializer.applyInstance(statue.id(), prefab));
  statue.destroy();

  const Entity first = serializer.instantiate(prefab)[0];
  const Entity second = serializer.instantiate(prefab)[0];
  engine.get<Transform>(second).position = glm::vec3(4, 0, 0);
  GameEntity one(engine, first), two(engine, second);
  CHECK_EQ(one.shader(), std::string(kDissolve));
  CHECK_NEAR(std::get<float>(*one.shaderParam("dissolve")), 0.3f, 1e-5f);
  CHECK(serializer.instanceOverrides(first).empty());

  // An instance changes the value: an override, which the other instance does not share
  two.setShaderParam("dissolve", 0.8f);
  CHECK(!serializer.instanceOverrides(second).empty());
  CHECK(serializer.instanceOverrides(first).empty());

  // Overrides survive a scene save and load, and the other instance still follows the prefab
  CHECK(engine.saveScene(scene));
  engine.loadScene(scene);
  steps.until([&engine]() { return !engine.isLoadingScene(); }, "the scene to load", 300);
  steps.step(1, [&engine]() {
    std::vector<GameEntity> found;
    engine.getComponentManager().forEachComponent<Model>([&](Entity entity, Model &) { found.emplace_back(engine, entity); });
    CHECK_EQ(found.size(), size_t(2));
    float low = 1.0f, high = 0.0f;
    for (GameEntity &e : found) {
      CHECK_EQ(e.shader(), std::string(kDissolve));
      const float value = std::get<float>(*e.shaderParam("dissolve"));
      low = std::min(low, value);
      high = std::max(high, value);
    }
    CHECK_NEAR(low, 0.3f, 1e-5f);
    CHECK_NEAR(high, 0.8f, 1e-5f);
  });
}
