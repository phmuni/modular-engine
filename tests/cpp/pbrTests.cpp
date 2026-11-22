// PBR materials: what glTF and OBJ files import, the API that changes them, old scene files (Blinn-Phong shininess)
// loading as roughness, and custom surface shaders that use the PBR fields (or still ask for the removed ones).

#include "components/model.h"
#include "describe.h"
#include "foundation/core/config.h"
#include "foundation/core/engine.h"
#include "rendering/resources/customShader.h"
#include "rendering/resources/material.h"
#include "systems/resourceSystem.h"
#include "systems/sceneSerializer.h"
#include "testing.h"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <sstream>

namespace {
// Two parts: "metal" (metallic 1, roughness 0.25, an ORM texture as metallic-roughness and occlusion with strength
// 0.5, red emission with KHR_materials_emissive_strength 4) and "plain" (metallic 0.5, roughness 0.8, no textures)
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

void writeFile(const std::string &path, const std::string &text) {
  std::ofstream out(path);
  out << text;
}
} // namespace

TEST_CASE(pbr_gltf_import) {
  GameEntity entity = engine.create("Pbr", Desc(kPbrModel));
  const size_t parts = engine.get<Model>(entity.id()).materialHandles.size();
  CHECK_EQ(parts, size_t(2));
  if (parts < 2)
    return;
  const Material &standard = resourcesOf(engine).getMaterial(0);

  const Material &metal = partMaterial(engine, entity, 0);
  CHECK_NEAR(metal.getMetallic(), 1.0f, 1e-6f);
  CHECK_NEAR(metal.getRoughness(), 0.25f, 1e-6f);
  CHECK_NEAR(metal.getOcclusionStrength(), 0.5f, 1e-6f);
  // The ORM image is read as the metallic-roughness and the occlusion texture (not the white stand-ins)
  CHECK(metal.getMetallicRoughness() != 0 && metal.getMetallicRoughness() != standard.getMetallicRoughness());
  CHECK(metal.getOcclusion() != 0 && metal.getOcclusion() != standard.getOcclusion());
  // Emissive factor, scaled by KHR_materials_emissive_strength
  CHECK_NEAR(metal.getEmissionColor(), glm::vec3(1.0f, 0.0f, 0.0f), 1e-6f);
  CHECK_NEAR(metal.getEmissionStrength(), 4.0f, 1e-6f);

  const Material &plain = partMaterial(engine, entity, 1);
  CHECK_NEAR(plain.getMetallic(), 0.5f, 1e-6f);
  CHECK_NEAR(plain.getRoughness(), 0.8f, 1e-6f);
  CHECK_NEAR(plain.getOcclusionStrength(), 1.0f, 1e-6f);
  CHECK_EQ(plain.getMetallicRoughness(), standard.getMetallicRoughness());
  CHECK_EQ(plain.getOcclusion(), standard.getOcclusion());
}

TEST_CASE(pbr_obj_shininess_becomes_roughness) {
  const std::string dir = testing::tempDir("pbr_obj");
  writeFile(dir + "/m.mtl",
            "newmtl shiny\nKd 1 0 0\nKs 1 1 1\nNs 48\nnewmtl matte\nKd 0 1 0\nNs 0\n");
  writeFile(dir + "/m.obj",
            "mtllib m.mtl\nv 0 0 0\nv 1 0 0\nv 0 1 0\nv 2 0 0\nv 3 0 0\nv 2 1 0\nvn 0 0 1\n"
            "o first\nusemtl shiny\nf 1//1 2//1 3//1\no second\nusemtl matte\nf 4//1 5//1 6//1\n");
  GameEntity entity = engine.create("Obj", Desc(dir + "/m.obj"));
  CHECK_EQ(engine.get<Model>(entity.id()).materialHandles.size(), size_t(2));
  // roughness = sqrt(2 / (Ns + 2)); Ks is ignored, so nothing is metallic
  CHECK_NEAR(partMaterial(engine, entity, 0).getRoughness(), 0.2f, 1e-5f);
  CHECK_NEAR(partMaterial(engine, entity, 0).getMetallic(), 0.0f, 1e-6f);
  CHECK_NEAR(partMaterial(engine, entity, 1).getRoughness(), 1.0f, 1e-5f);
}

TEST_CASE(pbr_set_metallic_and_roughness) {
  GameEntity a = engine.create("A", Desc(kPbrModel).at(0, 0, 0));
  GameEntity b = engine.create("B", Desc(kPbrModel).at(3, 0, 0));
  CHECK_EQ(engine.get<Model>(a.id()).materialHandles.size(), size_t(2));

  // A plain material starts dielectric and half rough
  GameEntity box = engine.create("Box", Desc("box"));
  CHECK_NEAR(partMaterial(engine, box, 0).getMetallic(), 0.0f, 1e-6f);
  CHECK_NEAR(partMaterial(engine, box, 0).getRoughness(), 0.5f, 1e-6f);

  // One part by number, then the other by name: only that entity's, the same model's other entity keeps its values
  a.setMetallic(0.25f, 1);
  a.setRoughness(0.1f, 1);
  CHECK_NEAR(partMaterial(engine, a, 1).getMetallic(), 0.25f, 1e-6f);
  CHECK_NEAR(partMaterial(engine, a, 1).getRoughness(), 0.1f, 1e-6f);
  CHECK_NEAR(partMaterial(engine, a, 0).getMetallic(), 1.0f, 1e-6f);
  CHECK_NEAR(partMaterial(engine, b, 1).getMetallic(), 0.5f, 1e-6f);
  CHECK_NEAR(partMaterial(engine, b, 1).getRoughness(), 0.8f, 1e-6f);

  // Every part, and values outside 0 to 1 are clamped
  a.setMetallic(2.0f);
  a.setRoughness(-1.0f);
  for (size_t part = 0; part < 2; ++part) {
    CHECK_NEAR(partMaterial(engine, a, part).getMetallic(), 1.0f, 1e-6f);
    CHECK_NEAR(partMaterial(engine, a, part).getRoughness(), 0.0f, 1e-6f);
  }
  CHECK_NEAR(partMaterial(engine, b, 0).getRoughness(), 0.25f, 1e-6f);

  // A part name the model does not have
  GameEntity named = engine.create("Named", Desc("box"));
  named.setRoughness(0.9f, "NoSuchPart"); // a warning, nothing set
  CHECK_NEAR(partMaterial(engine, named, 0).getRoughness(), 0.5f, 1e-6f);
}

TEST_CASE(pbr_scene_files_and_old_shininess) {
  const std::string file = testing::tempDir("pbr_scene") + "/scene.json";
  GameEntity box = engine.create("Box", Desc("box"));
  box.setMetallic(0.7f);
  box.setRoughness(0.3f);
  CHECK(testing::saveWholeScene(engine, file));

  // New files write "metallic" and "roughness", never "shininess"
  const std::string saved = readFile(file);
  CHECK(saved.find("\"metallic\"") != std::string::npos);
  CHECK(saved.find("\"roughness\"") != std::string::npos);
  CHECK(saved.find("shininess") == std::string::npos);

  // A file from before PBR: the part has a shininess (the Blinn-Phong exponent) instead
  nlohmann::json json = nlohmann::json::parse(saved);
  int edited = 0;
  std::function<void(nlohmann::json &)> visit = [&](nlohmann::json &node) {
    if (node.is_object()) {
      if (node.contains("parts") && node["parts"].is_array())
        for (auto &part : node["parts"]) {
          part.erase("metallic");
          part.erase("roughness");
          part["shininess"] = 48.0;
          ++edited;
        }
      for (auto &item : node.items())
        visit(item.value());
    } else if (node.is_array()) {
      for (auto &item : node)
        visit(item);
    }
  };
  visit(json);
  CHECK_EQ(edited, 1);
  writeFile(file, json.dump(2));

  box.destroy();
  engine.loadScene(file);
  steps.until([&engine]() { return !engine.isLoadingScene(); }, "the scene to load", 300);
  steps.step(1, [&engine, file]() {
    GameEntity box = engine.find("Box");
    CHECK(box.valid());
    if (!box.valid())
      return;
    // sqrt(2 / (48 + 2)) = 0.2; a Blinn-Phong material is not metallic
    CHECK_NEAR(partMaterial(engine, box, 0).getRoughness(), 0.2f, 1e-5f);
    CHECK_NEAR(partMaterial(engine, box, 0).getMetallic(), 0.0f, 1e-6f);
    // Saved again, it is in the new form
    CHECK(testing::saveWholeScene(engine, file));
    const std::string again = readFile(file);
    CHECK(again.find("\"roughness\"") != std::string::npos);
    CHECK(again.find("shininess") == std::string::npos);
  });
}

TEST_CASE(pbr_custom_shader_fields) {
  const std::string dir = testing::tempDir("pbr_shader");
  const std::string good = dir + "/good.glsl";
  const std::string old = dir + "/old.glsl";
  writeFile(good, "void surface(inout Surface s) {\n  s.metallic = 1.0;\n  s.roughness = 0.2;\n  s.occlusion *= 0.5;\n"
                  "  s.albedo *= s.metallic;\n}\n");
  writeFile(old, "void surface(inout Surface s) {\n  s.shininess = 32.0;\n}\n");
  auto &resources = resourcesOf(engine);
  const uint32_t standard = resources.getMaterial(0).getShaderHandle();
  GameEntity a = engine.create("A", Desc("box"));
  GameEntity b = engine.create("B", Desc("box"));
  a.setShader(good);
  b.setShader(old); // does not compile: Surface has no shininess anymore

  const auto handleOf = [&](const GameEntity &e) { return partMaterial(engine, e, 0).getShaderHandle(); };
  CHECK(resources.findCustomShader(good) && resources.findCustomShader(good)->ok);
  CHECK(handleOf(a) != standard);
  // It reports the error and the part keeps the standard shader; the game goes on
  CHECK(resources.findCustomShader(old) && !resources.findCustomShader(old)->ok);
  CHECK_EQ(handleOf(b), standard);
  CHECK_EQ(b.shader(), old);

  // The shipped example that used the specular fields compiles with the PBR ones
  GameEntity water = engine.create("Water", Desc("box"));
  water.setShader("shaders/examples/water.glsl");
  CHECK(resources.findCustomShader("shaders/examples/water.glsl") &&
        resources.findCustomShader("shaders/examples/water.glsl")->ok);

  steps.step(3, [a, b]() { CHECK(a.valid() && b.valid()); });
}
