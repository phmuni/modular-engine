// Scene files: one JSON object per entity with its name and components.

#include "systems/sceneSerializer.h"

#include "components/hidden.h"
#include "components/model.h"
#include "components/name.h"
#include "foundation/core/config.h"
#include "foundation/core/logger.h"
#include "rendering/resources/material.h"
#include "components/componentJson.h"
#include "systems/cameraSystem.h"
#include "systems/lightSystem.h"
#include "systems/renderSystem.h"
#include "systems/resourceSystem.h"
#include "systems/sceneSystem.h"
#include <algorithm>
#include <filesystem>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <regex>

namespace {
constexpr const char *kFormat = "modular-engine-scene";
constexpr int kVersion = 1;

constexpr std::pair<TextureSlot, const char *> kTextureSlots[] = {{TextureSlot::Diffuse, "diffuse"},
                                                                  {TextureSlot::Specular, "specular"},
                                                                  {TextureSlot::Normal, "normal"},
                                                                  {TextureSlot::Emission, "emission"}};

// Components hold floats; written as doubles they would read 1.2000000476837158. Keeps float precision instead.
void roundFloats(nlohmann::json &value) {
  if (value.is_number_float()) {
    char text[32];
    std::snprintf(text, sizeof(text), "%.7g", value.get<double>());
    value = std::strtod(text, nullptr);
  } else if (value.is_structured()) {
    for (auto &child : value)
      roundFloats(child);
  }
}

// Vectors on one line: "position": [0.0, 1.5, -2.0] instead of five
std::string compactNumberArrays(const std::string &text) {
  static const std::regex numberArray(R"(\[\s*(-?[0-9][-+0-9.eE]*(?:,\s*-?[0-9][-+0-9.eE]*)*)\s*\])");
  static const std::regex spacing(R"(,\s+)");
  std::string result;
  auto last = text.cbegin();
  for (std::sregex_iterator it(text.begin(), text.end(), numberArray), end; it != end; ++it) {
    result.append(last, text.cbegin() + it->position());
    result += "[" + std::regex_replace((*it)[1].str(), spacing, ", ") + "]";
    last = text.cbegin() + it->position() + it->length();
  }
  result.append(last, text.cend());
  return result;
}

GLuint textureIn(const Material &material, TextureSlot slot) {
  switch (slot) {
  case TextureSlot::Diffuse:
    return material.getDiffuse();
  case TextureSlot::Specular:
    return material.getSpecular();
  case TextureSlot::Normal:
    return material.getNormal();
  case TextureSlot::Emission:
    return material.getEmission();
  }
  return 0;
}
} // namespace

SceneSerializer::SceneSerializer(EntityManager &em, ComponentManager &cm, SystemManager &sm)
    : m_entityManager(em), m_componentManager(cm), m_systemManager(sm) {
  registerBuiltIns();
}

void SceneSerializer::registerComponent(const std::string &key, SaveFn save, LoadFn load) {
  auto existing = std::find_if(m_serializers.begin(), m_serializers.end(),
                               [&](const Serializer &serializer) { return serializer.key == key; });
  if (existing != m_serializers.end())
    *existing = Serializer{key, std::move(save), std::move(load)};
  else
    m_serializers.push_back(Serializer{key, std::move(save), std::move(load)});
}

void SceneSerializer::registerBuiltIns() {
  registerComponent<Transform>("Transform");

  registerComponent(
      "Model", [this](Entity entity) { return saveModel(entity); },
      [this](Entity entity, const json &data) { loadModel(entity, data); });

  registerComponent(
      "Light",
      [this](Entity entity) -> std::optional<json> {
        if (const Light *light = m_componentManager.tryGet<Light>(entity))
          return json(*light);
        return std::nullopt;
      },
      [this](Entity entity, const json &data) {
        m_componentManager.addInPlace<Light>(entity) = data.get<Light>();
        m_systemManager.getSystem<LightSystem>().createLight(entity);
      });

  registerComponent(
      "Camera",
      [this](Entity entity) -> std::optional<json> {
        const Camera *camera = m_componentManager.tryGet<Camera>(entity);
        if (!camera)
          return std::nullopt;
        json data = *camera;
        data["active"] = m_systemManager.getSystem<CameraSystem>().getActiveCamera() == entity;
        return data;
      },
      [this](Entity entity, const json &data) {
        auto &cameraSystem = m_systemManager.getSystem<CameraSystem>();
        Camera &camera = m_componentManager.addInPlace<Camera>(entity) = data.get<Camera>();
        cameraSystem.updateFrontVector(camera);
        if (data.value("active", false))
          cameraSystem.setActiveCamera(entity);
      });

  registerComponent<Collision>("Collision");
  registerComponent<ParticleEmitter>("ParticleEmitter");
  registerComponent<GravityLens>("GravityLens");
  registerComponent<AudioSource>("AudioSource");
  registerComponent<Skybox>("Skybox");

  registerComponent(
      "Hidden",
      [this](Entity entity) -> std::optional<json> {
        if (m_componentManager.has<Hidden>(entity))
          return json::object();
        return std::nullopt;
      },
      [this](Entity entity, const json &) { m_componentManager.addInPlace<Hidden>(entity); });
}

// A model is its file plus, per part, what the entity changed from the file's materials
std::optional<SceneSerializer::json> SceneSerializer::saveModel(Entity entity) const {
  const Model *model = m_componentManager.tryGet<Model>(entity);
  if (!model)
    return std::nullopt;

  auto &resources = m_systemManager.getSystem<ResourceSystem>();
  std::string path = resources.getMeshPath(model->meshHandle);
  if (path.empty()) {
    LOG_W("[Scene] Entity %d has a model with no file; it is not saved", entity);
    return std::nullopt;
  }

  json data;
  data["path"] = EngineConfig::relativePath(path);
  if (model->opacity != 1.0f)
    data["opacity"] = model->opacity;

  const uint32_t shader = resources.getMaterial(0).getShaderHandle();
  const std::vector<uint32_t> *original = resources.getModelMaterials(model->meshHandle, shader);
  json parts = json::array();
  for (size_t i = 0; i < model->materialHandles.size(); ++i) {
    const uint32_t originalHandle = original && i < original->size() ? (*original)[i] : 0;
    if (model->materialHandles[i] == originalHandle)
      continue;
    const Material &now = resources.getMaterial(model->materialHandles[i]);
    const Material &before = resources.getMaterial(originalHandle);

    json part;
    for (const auto &[slot, name] : kTextureSlots) {
      GLuint texture = textureIn(now, slot);
      if (texture == textureIn(before, slot) || texture == 0)
        continue;
      std::string texturePath = resources.getTexturePath(texture);
      if (slot == TextureSlot::Diffuse && texturePath.rfind("solid_", 0) == 0)
        part["color"] = now.getDiffuseColor();
      else if (!texturePath.empty())
        part["textures"][name] = EngineConfig::relativePath(texturePath);
    }
    if (now.getEmissionColor() != before.getEmissionColor() ||
        now.getEmissionStrength() != before.getEmissionStrength())
      part["emission"] = {{"color", now.getEmissionColor()}, {"strength", now.getEmissionStrength()}};
    if (now.getShininess() != before.getShininess())
      part["shininess"] = now.getShininess();

    if (!part.empty()) {
      part["part"] = i;
      parts.push_back(std::move(part));
    }
  }
  if (!parts.empty())
    data["parts"] = std::move(parts);
  return data;
}

void SceneSerializer::loadModel(Entity entity, const json &data) {
  auto &resources = m_systemManager.getSystem<ResourceSystem>();
  const uint32_t shader = resources.getMaterial(0).getShaderHandle();
  ModelLoadResult loaded = resources.loadModel(EngineConfig::resolvePath(data.at("path").get<std::string>()), shader);
  m_componentManager.addInPlace<Model>(entity, loaded.meshHandle, std::move(loaded.materialHandles),
                                       data.value("opacity", 1.0f));
  m_systemManager.getSystem<RenderSystem>().markBatchesDirty();

  for (const json &part : data.value("parts", json::array())) {
    const int index = part.at("part").get<int>();
    if (part.contains("color"))
      resources.setSolidColor(entity, index, part["color"].get<glm::vec3>());
    if (part.contains("textures"))
      for (const auto &[slot, name] : kTextureSlots)
        if (part["textures"].contains(name))
          resources.setTexture(entity, index, slot, part["textures"][name].get<std::string>());
    if (part.contains("emission"))
      resources.setEmission(entity, index, part["emission"].value("color", glm::vec3(0.0f)),
                            part["emission"].value("strength", 0.0f));
    if (part.contains("shininess"))
      resources.setShininess(entity, index, part["shininess"].get<float>());
  }
}

bool SceneSerializer::save(const std::string &path) const {
  std::vector<Entity> entities(m_entityManager.entities().begin(), m_entityManager.entities().end());
  std::sort(entities.begin(), entities.end()); // creation order, so saving twice gives the same file

  json list = json::array();
  for (Entity entity : entities) {
    json components = json::object();
    for (const Serializer &serializer : m_serializers) {
      try {
        if (std::optional<json> data = serializer.save(entity))
          components[serializer.key] = std::move(*data);
      } catch (const std::exception &error) {
        LOG_W("[Scene] Could not save %s of entity %d: %s", serializer.key.c_str(), entity, error.what());
      }
    }
    if (components.empty())
      continue; // nothing that can be saved, e.g. only game components without a serializer

    json entry;
    if (const Name *name = m_componentManager.tryGet<Name>(entity))
      entry["name"] = name->name;
    entry["components"] = std::move(components);
    list.push_back(std::move(entry));
  }

  json scene = {{"format", kFormat}, {"version", kVersion}, {"entities", std::move(list)}};
  roundFloats(scene);

  std::string resolved = EngineConfig::resolvePath(path);
  std::error_code error;
  std::filesystem::create_directories(std::filesystem::path(resolved).parent_path(), error);
  std::ofstream file(resolved);
  if (!file) {
    LOG_E("[Scene] Could not write %s", resolved.c_str());
    return false;
  }
  file << compactNumberArrays(scene.dump(2)) << '\n';
  LOG_I("[Scene] Saved %zu entities to %s", scene["entities"].size(), resolved.c_str());
  return static_cast<bool>(file);
}

bool SceneSerializer::load(const std::string &path) {
  std::string resolved = EngineConfig::resolvePath(path);
  std::ifstream file(resolved);
  if (!file) {
    LOG_E("[Scene] Could not open %s", resolved.c_str());
    return false;
  }

  json scene;
  try {
    scene = json::parse(file);
  } catch (const json::parse_error &error) {
    LOG_E("[Scene] %s is not valid JSON: %s", resolved.c_str(), error.what());
    return false;
  }
  if (!scene.is_object() || scene.value("format", "") != kFormat || !scene.contains("entities") ||
      !scene["entities"].is_array()) {
    LOG_E("[Scene] %s is not a scene file", resolved.c_str());
    return false;
  }
  if (scene.value("version", 0) > kVersion)
    LOG_W("[Scene] %s is from a newer engine (version %d); loading what this one knows", resolved.c_str(),
          scene.value("version", 0));

  clear();

  for (const json &entry : scene["entities"]) {
    Entity entity = m_entityManager.createEntity();
    if (entry.contains("name"))
      m_componentManager.addInPlace<Name>(entity, entry["name"].get<std::string>());

    const json &components = entry.contains("components") ? entry["components"] : json::object();
    for (const Serializer &serializer : m_serializers) {
      if (!components.contains(serializer.key))
        continue;
      try {
        serializer.load(entity, components[serializer.key]);
      } catch (const std::exception &error) {
        LOG_W("[Scene] Could not load %s of entity %d: %s", serializer.key.c_str(), entity, error.what());
      }
    }
    for (const auto &item : components.items()) {
      const std::string &key = item.key();
      bool known = std::any_of(m_serializers.begin(), m_serializers.end(),
                               [&](const Serializer &serializer) { return serializer.key == key; });
      if (!known && m_warnedKeys.insert(key).second)
        LOG_W("[Scene] No serializer for component \"%s\"; it is skipped", key.c_str());
    }
  }

  LOG_I("[Scene] Loaded %zu entities from %s", scene["entities"].size(), resolved.c_str());
  return true;
}

std::vector<std::string> SceneSerializer::modelPaths(const std::string &path) const {
  std::vector<std::string> paths;
  std::ifstream file(EngineConfig::resolvePath(path));
  json scene = json::parse(file, nullptr, false);
  if (scene.is_discarded() || !scene.contains("entities") || !scene["entities"].is_array())
    return paths;
  for (const json &entry : scene["entities"]) {
    const json *model = entry.contains("components") && entry["components"].contains("Model")
                            ? &entry["components"]["Model"]
                            : nullptr;
    if (model && model->contains("path") && (*model)["path"].is_string()) {
      std::string resolved = EngineConfig::resolvePath((*model)["path"].get<std::string>());
      if (std::find(paths.begin(), paths.end(), resolved) == paths.end())
        paths.push_back(std::move(resolved));
    }
  }
  return paths;
}

void SceneSerializer::clear() {
  auto &sceneSystem = m_systemManager.getSystem<SceneSystem>();
  std::vector<Entity> entities(m_entityManager.entities().begin(), m_entityManager.entities().end());
  for (Entity entity : entities)
    sceneSystem.destroyEntity(entity);
}

std::vector<std::string> SceneSerializer::listScenes(const std::string &directory) const {
  std::vector<std::string> scenes;
  std::error_code error;
  for (const auto &entry : std::filesystem::directory_iterator(EngineConfig::resolvePath(directory), error))
    if (entry.is_regular_file() && entry.path().extension() == ".json")
      scenes.push_back(EngineConfig::relativePath(entry.path().string()));
  std::sort(scenes.begin(), scenes.end());
  return scenes;
}
