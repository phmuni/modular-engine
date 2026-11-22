// Scene files: one JSON object per entity with its name and components.

#include "systems/sceneSerializer.h"

#include "components/hidden.h"
#include "components/model.h"
#include "components/name.h"
#include "components/parent.h"
#include "components/prefabInstance.h"
#include "foundation/core/componentRegistry.h"
#include "foundation/core/config.h"
#include "foundation/core/logger.h"
#include "rendering/resources/material.h"
#include "components/componentJson.h"
#include "systems/cameraSystem.h"
#include "systems/lightSystem.h"
#include "systems/renderSystem.h"
#include "systems/resourceSystem.h"
#include "systems/sceneSystem.h"
#include "systems/transformSystem.h"
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
                                                                  {TextureSlot::Normal, "normal"},
                                                                  {TextureSlot::Emission, "emission"},
                                                                  {TextureSlot::MetallicRoughness, "metallicRoughness"},
                                                                  {TextureSlot::Occlusion, "occlusion"}};

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

// A custom shader value as JSON: a number, an array of 2 to 4 numbers, or a texture's path
nlohmann::json shaderValueToJson(const ShaderValue &value) {
  using json = nlohmann::json;
  if (const float *number = std::get_if<float>(&value))
    return *number;
  if (const glm::vec2 *v = std::get_if<glm::vec2>(&value))
    return json::array({v->x, v->y});
  if (const glm::vec3 *v = std::get_if<glm::vec3>(&value))
    return json::array({v->x, v->y, v->z});
  if (const glm::vec4 *v = std::get_if<glm::vec4>(&value))
    return json::array({v->x, v->y, v->z, v->w});
  return std::get<std::string>(value);
}

std::optional<ShaderValue> shaderValueFromJson(const nlohmann::json &data) {
  if (data.is_number())
    return data.get<float>();
  if (data.is_string())
    return data.get<std::string>();
  if (data.is_array() && data.size() >= 2 && data.size() <= 4) {
    float v[4] = {0, 0, 0, 0};
    for (size_t i = 0; i < data.size(); ++i) {
      if (!data[i].is_number())
        return std::nullopt;
      v[i] = data[i].get<float>();
    }
    if (data.size() == 2)
      return glm::vec2(v[0], v[1]);
    if (data.size() == 3)
      return glm::vec3(v[0], v[1], v[2]);
    return glm::vec4(v[0], v[1], v[2], v[3]);
  }
  return std::nullopt;
}

GLuint textureIn(const Material &material, TextureSlot slot) {
  switch (slot) {
  case TextureSlot::Diffuse:
    return material.getDiffuse();
  case TextureSlot::Normal:
    return material.getNormal();
  case TextureSlot::Emission:
    return material.getEmission();
  case TextureSlot::MetallicRoughness:
    return material.getMetallicRoughness();
  case TextureSlot::Occlusion:
    return material.getOcclusion();
  }
  return 0;
}
} // namespace

SceneSerializer::SceneSerializer(EntityManager &em, ComponentManager &cm, SystemManager &sm)
    : m_entityManager(em), m_componentManager(cm), m_systemManager(sm) {
  registerBuiltIns();
}

void SceneSerializer::registerComponent(const std::string &key, SaveFn save, LoadFn load, RemoveFn remove) {
  auto existing = std::find_if(m_serializers.begin(), m_serializers.end(),
                               [&](const Serializer &serializer) { return serializer.key == key; });
  if (existing != m_serializers.end())
    *existing = Serializer{key, std::move(save), std::move(load), std::move(remove)};
  else
    m_serializers.push_back(Serializer{key, std::move(save), std::move(load), std::move(remove)});
}

namespace {
nlohmann::json fieldToJson(FieldKind kind, void *field) {
  switch (kind) {
  case FieldKind::Float: return *static_cast<float *>(field);
  case FieldKind::Int: return *static_cast<int *>(field);
  case FieldKind::Bool: return *static_cast<bool *>(field);
  case FieldKind::String: return *static_cast<std::string *>(field);
  case FieldKind::Vec2: return *static_cast<glm::vec2 *>(field);
  case FieldKind::Vec3: return *static_cast<glm::vec3 *>(field);
  case FieldKind::Vec4: return *static_cast<glm::vec4 *>(field);
  }
  return nullptr;
}

void fieldFromJson(FieldKind kind, void *field, const nlohmann::json &value) {
  switch (kind) {
  case FieldKind::Float: *static_cast<float *>(field) = value.get<float>(); break;
  case FieldKind::Int: *static_cast<int *>(field) = value.get<int>(); break;
  case FieldKind::Bool: *static_cast<bool *>(field) = value.get<bool>(); break;
  case FieldKind::String: *static_cast<std::string *>(field) = value.get<std::string>(); break;
  case FieldKind::Vec2: *static_cast<glm::vec2 *>(field) = value.get<glm::vec2>(); break;
  case FieldKind::Vec3: *static_cast<glm::vec3 *>(field) = value.get<glm::vec3>(); break;
  case FieldKind::Vec4: *static_cast<glm::vec4 *>(field) = value.get<glm::vec4>(); break;
  }
}
} // namespace

void SceneSerializer::registerReflected(const ReflectedComponent &component) {
  registerComponent(
      component.name,
      [this, &component](Entity entity) -> std::optional<json> {
        void *data = component.get(m_componentManager, entity);
        if (!data)
          return std::nullopt;
        json fields = json::object();
        for (const ReflectedField &field : component.fields)
          fields[field.name] = fieldToJson(field.kind, field.access(data));
        return fields;
      },
      [this, &component](Entity entity, const json &fields) {
        if (!component.add) {
          LOG_W("[SceneSerializer] %s has no default constructor, so it cannot be loaded", component.name.c_str());
          return;
        }
        void *data = component.add(m_componentManager, entity);
        for (const ReflectedField &field : component.fields) {
          auto it = fields.find(field.name);
          if (it == fields.end())
            continue;
          try {
            fieldFromJson(field.kind, field.access(data), *it);
          } catch (const json::exception &) {
            LOG_W("[SceneSerializer] %s.%s has a value of the wrong type, keeping the default", component.name.c_str(),
                  field.name.c_str());
          }
        }
      });
}

void SceneSerializer::registerBuiltIns() {
  registerComponent<Transform>("Transform");

  registerComponent(
      "Model", [this](Entity entity) { return saveModel(entity); },
      [this](Entity entity, const json &data) { loadModel(entity, data); },
      [this](Entity entity) {
        m_componentManager.removeComponent<Model>(entity);
        m_systemManager.getSystem<RenderSystem>().markBatchesDirty();
      });

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
      },
      [this](Entity entity) {
        m_systemManager.getSystem<LightSystem>().destroyLight(entity);
        m_componentManager.removeComponent<Light>(entity);
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
      },
      [this](Entity entity) {
        auto &cameraSystem = m_systemManager.getSystem<CameraSystem>();
        if (cameraSystem.getActiveCamera() == entity)
          cameraSystem.removeActiveCamera();
        m_componentManager.removeComponent<Camera>(entity);
      });

  registerComponent<CameraRig>("CameraRig");
  registerComponent<Collision>("Collision");
  registerComponent<ParticleEmitter>("ParticleEmitter");
  registerComponent<GravityLens>("GravityLens");
  registerComponent<AudioSource>("AudioSource");
  registerComponent<Skybox>("Skybox");
  registerComponent<ReflectionProbe>("ReflectionProbe");
  registerComponent<RigidBody>("RigidBody");
  registerComponent<CharacterController>("CharacterController");
  registerComponent<Animator>("Animator");
  registerComponent<AnimationStateMachine>("AnimationStateMachine");
  registerComponent<Script>("Script");
  registerComponent<Tags>("Tags");

  registerComponent(
      "Hidden",
      [this](Entity entity) -> std::optional<json> {
        if (m_componentManager.has<Hidden>(entity))
          return json::object();
        return std::nullopt;
      },
      [this](Entity entity, const json &) { m_componentManager.addInPlace<Hidden>(entity); },
      [this](Entity entity) { m_componentManager.removeComponent<Hidden>(entity); });

  // The link to a prefab file (see the SceneSerializer header). Scene files also get the overrides, found now.
  registerComponent(
      "PrefabInstance",
      [this](Entity entity) -> std::optional<json> {
        const PrefabInstance *instance = m_componentManager.tryGet<PrefabInstance>(entity);
        if (!instance)
          return std::nullopt;
        json data = {{"path", instance->path}};
        // A nested instance's changes belong to the instance that holds it (found through that one), unless that
        // one's file is not there: then it stands alone
        if (m_writeOverrides && enclosingInstance(entity) == -1) {
          json overrides = instanceOverridesPatch(entity);
          if (overrides.is_array()) // null: the file was not there to compare with, so the entities stay as they are
            data["overrides"] = std::move(overrides);
        }
        return data;
      },
      [this](Entity entity, const json &data) {
        m_componentManager.addInPlace<PrefabInstance>(entity).path = data.at("path").get<std::string>();
        if (data.contains("overrides") && data["overrides"].is_array())
          m_loadedOverrides.emplace_back(entity, data["overrides"]);
      },
      [this](Entity entity) { m_componentManager.removeComponent<PrefabInstance>(entity); });
  registerComponent(
      "PrefabPart",
      [this](Entity entity) -> std::optional<json> {
        if (const PrefabPart *part = m_componentManager.tryGet<PrefabPart>(entity)) {
          json data = {{"index", part->index}};
          if (part->outer >= 0)
            data["outer"] = part->outer;
          return data;
        }
        return std::nullopt;
      },
      [this](Entity entity, const json &data) {
        PrefabPart &part = m_componentManager.addInPlace<PrefabPart>(entity);
        part.index = data.value("index", 0);
        part.outer = data.value("outer", -1);
      },
      [this](Entity entity) { m_componentManager.removeComponent<PrefabPart>(entity); });
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
    if (now.getMetallic() != before.getMetallic())
      part["metallic"] = now.getMetallic();
    if (now.getRoughness() != before.getRoughness())
      part["roughness"] = now.getRoughness();
    if (!now.getShaderPath().empty()) {
      part["shader"] = now.getShaderPath();
      json params = json::object();
      for (const auto &[paramName, param] : now.getShaderParams())
        params[paramName] = shaderValueToJson(param.value);
      if (!params.empty())
        part["shaderParams"] = std::move(params);
    }

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
  ModelLoadResult loaded = resources.loadModel(EngineConfig::resolveAsset(data.at("path").get<std::string>()), shader);
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
    if (part.contains("metallic"))
      resources.setMetallic(entity, index, part["metallic"].get<float>());
    if (part.contains("roughness"))
      resources.setRoughness(entity, index, part["roughness"].get<float>());
    // Scenes saved before PBR have the Blinn-Phong exponent; "roughness" wins when both are there
    else if (part.contains("shininess"))
      resources.setRoughness(entity, index, std::sqrt(2.0f / (std::max(part["shininess"].get<float>(), 0.0f) + 2.0f)));
    // The shader before its values; a shader that does not compile still keeps its path and values (see
    // ResourceSystem::setMaterialShader), so the scene saves again as it was
    if (part.contains("shader") && part["shader"].is_string()) {
      resources.setShader(entity, index, part["shader"].get<std::string>());
      if (part.contains("shaderParams") && part["shaderParams"].is_object())
        for (const auto &[paramName, data] : part["shaderParams"].items())
          if (std::optional<ShaderValue> value = shaderValueFromJson(data))
            resources.setShaderParam(entity, index, paramName, *value);
    }
  }
}

SceneSerializer::json SceneSerializer::entitiesToJson(const std::vector<Entity> &entities, Entity root,
                                                      std::vector<Entity> *written) const {
  // Parents are written as their place in this list, since entity numbers change from run to run
  std::vector<Entity> saved;
  std::vector<json> entries;
  for (Entity entity : entities) {
    json components = json::object();
    // A prefab holds a nested instance as one entry: its place and the link, not the entities of the other prefab
    std::optional<json> link;
    if (root != -1 && entity != root && canReference(entity))
      link = nestedLink(entity);
    for (const Serializer &serializer : m_serializers) {
      if (link && serializer.key != "Transform")
        continue;
      try {
        if (std::optional<json> data = serializer.save(entity))
          components[serializer.key] = std::move(*data);
      } catch (const std::exception &error) {
        LOG_W("[Scene] Could not save %s of entity %d: %s", serializer.key.c_str(), entity, error.what());
      }
    }
    if (link)
      components["PrefabInstance"] = *link;
    if (components.empty())
      continue; // nothing that can be saved, e.g. only game components without a serializer
    if (entity == root && components.contains("Transform"))
      components["Transform"]["position"] = {0.0f, 0.0f, 0.0f}; // a prefab's root sits where it is instantiated
    if (!link && root != -1) { // the entities of a prefab file are not linked to it
      components.erase("PrefabInstance");
      components.erase("PrefabPart");
    }

    json entry;
    if (const Name *name = m_componentManager.tryGet<Name>(entity); name && !link)
      entry["name"] = name->name;
    entry["components"] = std::move(components);
    saved.push_back(entity);
    entries.push_back(std::move(entry));
  }
  for (size_t i = 0; i < saved.size(); ++i) {
    const Parent *parent = saved[i] == root ? nullptr : m_componentManager.tryGet<Parent>(saved[i]);
    if (!parent)
      continue;
    auto it = std::find(saved.begin(), saved.end(), parent->entity);
    if (it != saved.end())
      entries[i]["components"]["Parent"] = {{"parent", it - saved.begin()}, {"local", parent->local}};
  }
  json list = json::array();
  for (json &entry : entries)
    list.push_back(std::move(entry));
  if (written)
    *written = std::move(saved);
  return list;
}

bool SceneSerializer::writeFile(const std::string &path, json entities, bool withLayers) const {
  json scene = {{"format", kFormat}, {"version", kVersion}};
  if (withLayers && m_layers && !m_layers->isDefault())
    scene["layers"] = m_layers->toJson();
  scene["entities"] = std::move(entities);
  roundFloats(scene);

  std::string resolved = EngineConfig::resolveAsset(path);
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

bool SceneSerializer::save(const std::string &path, bool includeRuntime, int scene) const {
  std::vector<Entity> entities;
  for (Entity entity : m_entityManager.entities()) {
    const bool wanted = scene != -1 ? m_entityManager.sceneOf(entity) == scene
                                    : includeRuntime || !m_entityManager.isRuntime(entity);
    if (wanted)
      entities.push_back(entity);
  }
  std::sort(entities.begin(), entities.end()); // creation order, so saving twice gives the same file
  m_writeOverrides = true; // linked prefab instances record what they changed from their file
  json list = entitiesToJson(entities, -1);
  m_writeOverrides = false;
  return writeFile(path, std::move(list), true);
}

std::string SceneSerializer::fingerprint(int scene) const {
  std::vector<Entity> entities;
  for (Entity entity : m_entityManager.entities())
    if (scene == -1 || m_entityManager.sceneOf(entity) == scene)
      entities.push_back(entity);
  std::sort(entities.begin(), entities.end());
  std::string text = entitiesToJson(entities, -1).dump();
  if (scene == -1) { // moving an entity to another scene is a change, though what each holds may not differ
    for (Entity entity : entities)
      text += ',' + std::to_string(m_entityManager.sceneOf(entity));
  }
  return text;
}

std::string SceneSerializer::sceneKey(const std::string &path) {
  if (path.empty())
    return {};
  return std::filesystem::path(EngineConfig::resolveAsset(path)).lexically_normal().generic_string();
}

bool SceneSerializer::savePrefab(Entity root, const std::string &path) const {
  if (!m_entityManager.isAlive(root)) {
    LOG_E("[Scene] savePrefab: entity %d does not exist", root);
    return false;
  }
  const std::vector<Entity> entities = prefabEntities(root);
  const std::string cycle = nestingCycle(std::filesystem::path(EngineConfig::resolveAsset(path)).lexically_normal().generic_string(),
                                         root, entities);
  if (!cycle.empty()) {
    LOG_E("[Scene] Not writing %s: it would contain itself (through %s)", EngineConfig::resolveAsset(path).c_str(),
          cycle.c_str());
    return false;
  }
  noteFileBefore(EngineConfig::resolveAsset(path));
  return writeFile(path, entitiesToJson(entities, root));
}

std::string SceneSerializer::copyToJson(Entity root) const { return copyToJson(std::vector<Entity>{root}); }

std::string SceneSerializer::copyToJson(const std::vector<Entity> &roots) const {
  std::vector<Entity> entities;
  auto &transformSystem = m_systemManager.getSystem<TransformSystem>();
  for (Entity root : roots) {
    if (!m_entityManager.isAlive(root) || std::find(entities.begin(), entities.end(), root) != entities.end())
      continue;
    entities.push_back(root);
    for (Entity descendant : transformSystem.getDescendants(m_componentManager, root))
      if (std::find(entities.begin(), entities.end(), descendant) == entities.end())
        entities.push_back(descendant);
  }
  if (entities.empty())
    return {};
  // No root given: the roots keep their positions; their parents are not in the list, so they are not written either
  std::vector<Entity> written;
  json list = entitiesToJson(entities, -1, &written);
  // A part of a prefab copied without its instance's root would link to nothing (or to the wrong instance, pasted
  // under another): it becomes a plain entity
  for (size_t i = 0; i < written.size(); ++i)
    if (list[i]["components"].contains("PrefabPart") &&
        std::find(entities.begin(), entities.end(), instanceRoot(written[i])) == entities.end())
      list[i]["components"].erase("PrefabPart");
  json scene = {{"format", kFormat}, {"version", kVersion}, {"entities", std::move(list)}};
  roundFloats(scene);
  return scene.dump();
}

std::vector<Entity> SceneSerializer::pasteFromJson(const std::string &text) {
  const json scene = json::parse(text, nullptr, false);
  if (scene.is_discarded() || !scene.is_object() || scene.value("format", "") != kFormat ||
      !scene.contains("entities") || !scene["entities"].is_array())
    return {};
  std::vector<Entity> created = createEntities(scene["entities"]);
  m_loadedOverrides.clear(); // a pasted instance is as it was copied, not brought up to date with its file
  return created;
}

std::optional<SceneSerializer::json> SceneSerializer::readFile(const std::string &path, bool log) const {
  std::string resolved = EngineConfig::resolveAsset(path);
  std::ifstream file(resolved);
  if (!file) {
    if (log)
      LOG_E("[Scene] Could not open %s", resolved.c_str());
    return std::nullopt;
  }
  json scene;
  try {
    scene = json::parse(file);
  } catch (const json::parse_error &error) {
    if (log)
      LOG_E("[Scene] %s is not valid JSON: %s", resolved.c_str(), error.what());
    return std::nullopt;
  }
  if (!scene.is_object() || scene.value("format", "") != kFormat || !scene.contains("entities") ||
      !scene["entities"].is_array()) {
    if (log)
      LOG_E("[Scene] %s is not a scene file", resolved.c_str());
    return std::nullopt;
  }
  if (scene.value("version", 0) > kVersion)
    LOG_W("[Scene] %s is from a newer engine (version %d); loading what this one knows", resolved.c_str(),
          scene.value("version", 0));
  return scene;
}

std::vector<Entity> SceneSerializer::createEntities(const json &entries) {
  std::vector<Entity> created;
  m_loadedOverrides.clear();
  struct Link {
    Entity child;
    size_t parentIndex;
    Transform local;
  };
  std::vector<Link> links;

  for (const json &entry : entries) {
    Entity entity = m_entityManager.createEntity();
    created.push_back(entity);
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
    // Parents can come later in the file: linked once every entity exists
    if (components.contains("Parent")) {
      try {
        const json &parent = components["Parent"];
        links.push_back({entity, parent.at("parent").get<size_t>(), parent.value("local", Transform())});
      } catch (const std::exception &error) {
        LOG_W("[Scene] Could not load Parent of entity %d: %s", entity, error.what());
      }
    }
    for (const auto &item : components.items()) {
      const std::string &key = item.key();
      bool known = key == "Parent" || std::any_of(m_serializers.begin(), m_serializers.end(),
                                                  [&](const Serializer &serializer) { return serializer.key == key; });
      if (!known && m_warnedKeys.insert(key).second)
        LOG_W("[Scene] No serializer for component \"%s\"; it is skipped", key.c_str());
    }
  }

  for (const Link &link : links) {
    if (link.parentIndex >= created.size()) {
      LOG_W("[Scene] Entity %d names parent %zu, past the end of the file", link.child, link.parentIndex);
      continue;
    }
    // The file holds both the child's place in the world and relative to its parent; the parent's says where the
    // parent was, so nothing moves until one of them does
    Parent &parent = m_componentManager.addInPlace<Parent>(link.child);
    parent.entity = created[link.parentIndex];
    parent.local = link.local;
    parent.lastLocal = link.local;
    if (const Transform *world = m_componentManager.tryGet<Transform>(link.child))
      parent.lastWorld = *world;
    if (const Transform *parentWorld = m_componentManager.tryGet<Transform>(parent.entity))
      parent.lastParent = *parentWorld;
  }
  return created;
}

void SceneSerializer::mergeSceneLayers(const json &scene, const std::string &key, bool additive) {
  if (!m_layers)
    return;
  // Still as the last merge left them: nothing changed them since (code's changes in between become the base)
  const bool untouched = m_sceneLayersMerged && m_layers->version() == m_sceneLayersVersion;
  if (!additive) {
    // Back to the base first, so the previous scene's layers do not stay
    if (untouched)
      m_layers->fromJson(m_baseLayers);
    m_baseLayers = m_layers->toJson();
    m_sceneLayersMerged = false;
    m_sceneLayers.clear();
  } else if (!untouched) {
    m_baseLayers = m_layers->toJson();
    m_sceneLayersMerged = false;
    m_sceneLayers.clear();
  }
  const auto layers = scene.find("layers");
  if (layers == scene.end() || !layers->is_object())
    return;
  m_layers->merge(*layers);
  m_sceneLayers.emplace_back(key, *layers);
  m_sceneLayersMerged = true;
  m_sceneLayersVersion = m_layers->version();
}

void SceneSerializer::unmergeSceneLayers(const std::string &key) {
  const auto found = std::find_if(m_sceneLayers.begin(), m_sceneLayers.end(),
                                  [&](const auto &entry) { return entry.first == key; });
  if (found == m_sceneLayers.end())
    return;
  m_sceneLayers.erase(found);
  if (!m_layers || !m_sceneLayersMerged || m_layers->version() != m_sceneLayersVersion)
    return; // code changed the layers since: they are its now
  m_layers->fromJson(m_baseLayers);
  for (const auto &entry : m_sceneLayers)
    m_layers->merge(entry.second);
  m_sceneLayersMerged = !m_sceneLayers.empty();
  m_sceneLayersVersion = m_layers->version();
}

bool SceneSerializer::load(const std::string &path) {
  const std::optional<json> scene = readFile(path);
  if (!scene)
    return false;
  const std::string key = sceneKey(path);
  mergeSceneLayers(*scene, key, false);
  clear();
  m_entityManager.closeAllScenes();
  const int id = m_entityManager.openScene(path, key);
  m_entityManager.setCreatingInto(id);
  createEntities((*scene)["entities"]);
  m_entityManager.setCreatingInto(-1);
  syncLoadedInstances();
  LOG_I("[Scene] Loaded %zu entities from %s", (*scene)["entities"].size(), EngineConfig::resolveAsset(path).c_str());
  return true;
}

bool SceneSerializer::loadAdditive(const std::string &path) {
  const std::string key = sceneKey(path);
  if (m_entityManager.findSceneByKey(key) != -1) {
    LOG_I("[Scene] %s is already open", EngineConfig::resolveAsset(path).c_str());
    return false;
  }
  const std::optional<json> scene = readFile(path);
  if (!scene)
    return false;
  auto &cameraSystem = m_systemManager.getSystem<CameraSystem>();
  const Entity activeCamera = cameraSystem.getActiveCamera();
  mergeSceneLayers(*scene, key, true);
  const int id = m_entityManager.openScene(path, key);
  m_entityManager.setCreatingInto(id);
  createEntities((*scene)["entities"]);
  m_entityManager.setCreatingInto(-1);
  syncLoadedInstances();
  if (activeCamera != -1 && m_entityManager.isAlive(activeCamera))
    cameraSystem.setActiveCamera(activeCamera); // a camera the file marks active does not take the view over
  LOG_I("[Scene] Added %zu entities from %s", (*scene)["entities"].size(), EngineConfig::resolveAsset(path).c_str());
  return true;
}

bool SceneSerializer::unloadScene(int id) {
  const EntityManager::Scene *scene = m_entityManager.findScene(id);
  if (!scene)
    return false;
  const std::string key = scene->key;
  auto &sceneSystem = m_systemManager.getSystem<SceneSystem>();
  for (Entity entity : m_entityManager.entitiesOf(id))
    if (m_entityManager.isAlive(entity))
      sceneSystem.destroyEntity(entity); // its children (all in the same scene) go with it
  m_entityManager.closeScene(id);
  unmergeSceneLayers(key);
  return true;
}

std::vector<Entity> SceneSerializer::instantiate(const std::string &path) {
  const PrefabFile *prefab = prefabFile(EngineConfig::resolveAsset(path), true);
  if (!prefab)
    return {};
  if (prefab->cyclic) {
    LOG_E("[Scene] Prefab %s contains itself (directly or through another prefab); it is not placed",
          EngineConfig::resolveAsset(path).c_str());
    return {};
  }
  std::vector<Entity> created = createEntities(prefab->entities);
  m_loadedOverrides.clear();
  if (prefab->root >= 0 && static_cast<size_t>(prefab->root) < created.size()) {
    const Entity root = created[prefab->root];
    std::unordered_set<Entity> references;
    for (Entity entity : created)
      if (m_componentManager.has<PrefabInstance>(entity))
        references.insert(entity); // the nested instances the file holds
    linkEntities(root, EngineConfig::relativePath(EngineConfig::resolveAsset(path)), created, references);
    if (!references.empty()) {
      // The entities of the nested prefabs come in with the rest of the file's parts
      const PrefabFile *file = cachedPrefabFile(EngineConfig::resolveAsset(path));
      const std::unordered_set<Entity> own(created.begin(), created.end());
      syncInstance(root, file->parts, file->opaque, false);
      for (const auto &[key, entity] : collectParts(root))
        if (!own.count(entity))
          created.push_back(entity);
    }
  }
  return created;
}

std::vector<std::string> SceneSerializer::modelPaths(const std::string &path) const {
  std::vector<std::string> paths;
  std::ifstream file(EngineConfig::resolveAsset(path));
  json scene = json::parse(file, nullptr, false);
  if (scene.is_discarded() || !scene.contains("entities") || !scene["entities"].is_array())
    return paths;
  for (const json &entry : scene["entities"]) {
    const json *model = entry.contains("components") && entry["components"].contains("Model")
                            ? &entry["components"]["Model"]
                            : nullptr;
    if (model && model->contains("path") && (*model)["path"].is_string()) {
      std::string resolved = EngineConfig::resolveAsset((*model)["path"].get<std::string>());
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
