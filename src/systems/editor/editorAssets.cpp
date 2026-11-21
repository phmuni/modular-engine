// The editor's asset panel and what its assets do (drag into the view, double-click, context menu), and the
// Properties section of linked prefab instances. Part of EditorUISystem, kept apart from editorUISystem.cpp.

#include "systems/editorUISystem.h"

#include "components/audioSource.h"
#include "components/camera.h"
#include "components/hidden.h"
#include "components/model.h"
#include "components/name.h"
#include "components/parent.h"
#include "components/prefabInstance.h"
#include "components/script.h"
#include "components/transform.h"
#include "foundation/core/config.h"
#include "foundation/core/engine.h"
#include "foundation/core/logger.h"
#include "rendering/resources/material.h"
#include "rendering/resources/mesh.h"
#include "systems/modelQueries.h"
#include "systems/resourceSystem.h"
#include "systems/sceneSerializer.h"
#include "systems/transformSystem.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <imgui/imgui.h>
#include <limits>

namespace {
const ImVec4 kLinkColor(0.45f, 0.7f, 1.0f, 1.0f);

// "../assets/prefabs/car.json" as the browser knows it, "prefabs/car.json"; empty for a file outside assets/
std::string assetRelative(const std::string &path) {
  const std::string prefix(EngineConfig::ASSETS_PREFIX);
  return path.compare(0, prefix.size(), prefix) == 0 ? path.substr(prefix.size()) : std::string();
}

std::string nameOfAsset(const std::string &asset) { return std::filesystem::path(asset).stem().string(); }
} // namespace

void EditorUISystem::renderAssetPanel(Engine &engine, SystemManager &systemManager, ComponentManager &componentManager) {
  // Docked along the bottom of the view in the default layout; a request to open it brings its tab forward
  if (m_assetsOpenRequest) {
    ImGui::SetNextWindowFocus();
    m_assetsOpenRequest = false;
  }
  const bool open = ImGui::Begin("Assets");
  if (open) {
    const bool focused = (SDL_GetWindowFlags(m_window) & SDL_WINDOW_INPUT_FOCUS) != 0;
    const AssetBrowser::Event event = m_assets.draw(focused);
    if (event.type != AssetBrowser::Event::Type::None)
      handleAssetEvent(engine, systemManager, componentManager, event);
  }
  ImGui::End();
}

void EditorUISystem::requestSceneLoad(Engine &engine, const std::string &asset) {
  // The path as scene lists write it, relative to the executable
  const std::string path = std::string(EngineConfig::ASSETS_PREFIX) + asset;
  if (engine.isSceneDirty())
    m_pendingScene = path; // asked about first
  else
    engine.loadScene(path);
}

void EditorUISystem::renderSceneLoadDialog(Engine &engine) {
  if (m_pendingScene.empty())
    return;
  if (!ImGui::IsPopupOpen("Load scene?"))
    ImGui::OpenPopup("Load scene?");
  if (!ImGui::BeginPopupModal("Load scene?", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    return;
  ImGui::Text("The scene has changes that are not saved.");
  ImGui::TextDisabled("Load %s anyway?", m_pendingScene.c_str());
  const bool canSave = !engine.getScenePath().empty();
  ImGui::BeginDisabled(!canSave);
  if (ImGui::Button("Save and load")) {
    engine.saveScene(engine.getScenePath());
    engine.loadScene(m_pendingScene);
    m_pendingScene.clear();
    ImGui::CloseCurrentPopup();
  }
  ImGui::EndDisabled();
  if (!canSave && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    ImGui::SetTooltip("This scene has no file yet: save it from Settings > Scene");
  ImGui::SameLine();
  if (ImGui::Button("Load without saving")) {
    engine.loadScene(m_pendingScene);
    m_pendingScene.clear();
    ImGui::CloseCurrentPopup();
  }
  ImGui::SameLine();
  if (ImGui::Button("Cancel")) {
    m_pendingScene.clear();
    ImGui::CloseCurrentPopup();
  }
  ImGui::EndPopup();
}

void EditorUISystem::handleAssetEvent(Engine &engine, SystemManager &systemManager, ComponentManager &componentManager,
                                      const AssetBrowser::Event &event) {
  using Kind = AssetBrowser::Kind;
  if (event.type == AssetBrowser::Event::Type::SelectInstances) {
    const auto roots =
        systemManager.getSystem<SceneSerializer>().instancesOf(EngineConfig::resolveAsset(event.path));
    setSelection(roots);
    m_selectionAnchor = selectedEntity;
    m_scrollToSelection = true;
    if (roots.empty())
      LOG_I("[Editor] No instance of %s in the scene", event.path.c_str());
    return;
  }
  if (event.type != AssetBrowser::Event::Type::Open)
    return;
  switch (event.kind) {
  case Kind::Scene:
    requestSceneLoad(engine, event.path);
    break;
  case Kind::Model:
  case Kind::Prefab: {
    // In front of the camera, as Add Entity > Prefab does
    glm::vec3 position(0.0f);
    if (const Camera *camera = engine.getActiveCamera())
      position = camera->position + glm::normalize(camera->front) * 5.0f;
    placeAsset(engine, systemManager, componentManager, event.path, position, false);
    break;
  }
  default:
    openInEditor(EngineConfig::resolveAsset(event.path)); // a script opens in the editor, anything else in its program
    break;
  }
}

bool EditorUISystem::assignAssetToEntity(Engine &engine, ComponentManager &componentManager, Entity entity,
                                         const std::string &asset) {
  if (engine.getPlayMode() == Engine::PlayMode::Testing)
    return false;
  switch (AssetBrowser::classify(asset)) {
  case AssetBrowser::Kind::Script:
    if (Script *script = componentManager.tryGet<Script>(entity))
      script->path = asset; // replaces the script, keeping the entity's own property values
    else
      componentManager.addInPlace<Script>(entity, asset);
    return true;
  case AssetBrowser::Kind::Sound:
    if (AudioSource *source = componentManager.tryGet<AudioSource>(entity))
      source->path = asset;
    else
      componentManager.addInPlace<AudioSource>(entity).path = asset;
    return true;
  case AssetBrowser::Kind::Animator:
    engine.setAnimator(entity, asset); // an animation state machine dropped on a character
    return true;
  default:
    return false;
  }
}

Entity EditorUISystem::placeAsset(Engine &engine, SystemManager &systemManager, ComponentManager &componentManager,
                                  const std::string &asset, const glm::vec3 &point, bool onSurface) {
  if (engine.getPlayMode() == Engine::PlayMode::Testing)
    return -1;
  auto &resourceSystem = systemManager.getSystem<ResourceSystem>();
  auto &transformSystem = systemManager.getSystem<TransformSystem>();
  // One undo step: the click that began the drag opened it already, or it opens here
  engine.beginCheckpoint();
  Entity placed = -1;
  if (AssetBrowser::classify(asset) == AssetBrowser::Kind::Prefab) {
    placed = engine.instantiate(asset, point);
    if (placed != -1 && onSurface) {
      // Its lowest model point rests on the surface
      float lowest = std::numeric_limits<float>::max();
      std::vector<Entity> family{placed};
      for (Entity descendant : transformSystem.getDescendants(componentManager, placed))
        family.push_back(descendant);
      for (Entity entity : family) {
        const Model *model = componentManager.tryGet<Model>(entity);
        const Transform *transform = componentManager.tryGet<Transform>(entity);
        if (!model || !transform)
          continue;
        const Mesh &mesh = resourceSystem.getMesh(model->meshHandle);
        const glm::mat4 matrix = transformSystem.calculateModelMatrix(*transform);
        for (int i = 0; i < 8; ++i) {
          const glm::vec3 low = mesh.getBoundsMin(), high = mesh.getBoundsMax();
          const glm::vec3 corner(i & 1 ? high.x : low.x, i & 2 ? high.y : low.y, i & 4 ? high.z : low.z);
          lowest = std::min(lowest, glm::vec3(matrix * glm::vec4(corner, 1.0f)).y);
        }
      }
      if (lowest != std::numeric_limits<float>::max())
        if (Transform *transform = componentManager.tryGet<Transform>(placed))
          transform->position.y += point.y - lowest;
    }
  } else {
    const uint32_t shader = resourceSystem.getMaterial(0).getShaderHandle();
    placed = engine.entities().create(nameOfAsset(asset)).withModel(asset, point, glm::vec3(0.0f), glm::vec3(1.0f), shader).build();
    if (onSurface) { // standing on it
      const Model *model = componentManager.tryGet<Model>(placed);
      if (model)
        if (Transform *transform = componentManager.tryGet<Transform>(placed))
          transform->position.y -= resourceSystem.getMesh(model->meshHandle).getBoundsMin().y * transform->scale.y;
    }
  }
  if (placed != -1) {
    selectOnly(placed);
    m_scrollToSelection = true;
  }
  return placed;
}

void EditorUISystem::handleAssetDrop(Engine &engine, SystemManager &systemManager, ComponentManager &componentManager) {
  using Kind = AssetBrowser::Kind;
  const ImGuiPayload *payload = ImGui::GetDragDropPayload();
  if (!payload || !payload->IsDataType(AssetBrowser::kPayload) || !payload->Data || !ImGui::IsMousePosValid())
    return;
  if (!m_gameHovered && ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow))
    return; // over a panel: its own drop targets take it (the Game panel is the view: the drop lands there)
  const ImGuiIO &io = ImGui::GetIO();
  const std::string asset = static_cast<const char *>(payload->Data);
  const Kind kind = AssetBrowser::classify(asset);
  const bool released = ImGui::IsMouseReleased(ImGuiMouseButton_Left);
  const glm::vec2 mouse(io.MousePos.x, io.MousePos.y);
  ImDrawList *drawList = ImGui::GetForegroundDrawList();

  if (kind == Kind::Model || kind == Kind::Prefab) {
    // The surface under the cursor; off any, the ground plane, or a few units into the view
    const auto hit = engine.pickModel(mouse);
    glm::vec3 point(0.0f);
    bool onSurface = false;
    if (hit) {
      point = hit->point;
      onSurface = true;
    } else if (const auto ray = engine.screenRay(mouse)) {
      if (ray->direction.y < -1e-3f && -ray->origin.y / ray->direction.y < 200.0f) {
        point = ray->origin + ray->direction * (-ray->origin.y / ray->direction.y);
        onSurface = true;
      } else {
        point = ray->origin + ray->direction * 8.0f;
      }
    }
    const ImU32 color = onSurface ? IM_COL32(110, 230, 120, 230) : IM_COL32(255, 200, 60, 230);
    drawList->AddCircle(ImVec2(mouse.x, mouse.y), 9.0f, color, 0, 2.0f);
    drawList->AddCircleFilled(ImVec2(mouse.x, mouse.y), 2.5f, color);
    if (released)
      placeAsset(engine, systemManager, componentManager, asset, point, onSurface);
  } else if (kind == Kind::Script || kind == Kind::Sound) {
    // The entity under the cursor, outlined
    const auto hit = engine.pickModel(mouse);
    if (hit) {
      drawBox(systemManager, componentManager, hit->entity, IM_COL32(90, 200, 255, 240), 2.0f);
      if (released) {
        engine.beginCheckpoint();
        if (assignAssetToEntity(engine, componentManager, hit->entity, asset))
          selectOnly(hit->entity);
      }
    }
  } else if (kind == Kind::Scene && released) {
    requestSceneLoad(engine, asset);
  }
}

void EditorUISystem::renderPrefabLink(Engine &engine, SystemManager &systemManager, ComponentManager &componentManager) {
  auto &serializer = systemManager.getSystem<SceneSerializer>();
  const Entity root = serializer.instanceRoot(selectedEntity);
  if (root == -1)
    return;
  const PrefabInstance *instance = componentManager.tryGet<PrefabInstance>(root);
  if (!instance)
    return;
  const std::string assetPath = assetRelative(instance->path);

  ImGui::Separator();
  ImGui::TextColored(kLinkColor, "Prefab instance");
  ImGui::SameLine();
  ImGui::TextDisabled("%s", assetPath.empty() ? instance->path.c_str() : assetPath.c_str());
  if (root != selectedEntity) {
    const Name *name = componentManager.tryGet<Name>(root);
    ImGui::TextDisabled("Part of %s", name && !name->name.empty() ? name->name.c_str() : "its instance");
    ImGui::SameLine();
    if (ImGui::SmallButton("Select root"))
      selectOnly(root);
  }
  // A nested instance (a door inside a house): its own file, and the prefab that holds it
  const Entity outer = serializer.enclosingInstance(root);
  const PrefabInstance *outerInstance = outer != -1 ? componentManager.tryGet<PrefabInstance>(outer) : nullptr;
  const auto stemOf = [](const std::string &path) { return std::filesystem::path(path).filename().string(); };
  if (outerInstance) {
    const Name *outerName = componentManager.tryGet<Name>(outer);
    ImGui::TextDisabled("Nested in %s (%s)", outerName && !outerName->name.empty() ? outerName->name.c_str() : "an instance",
                        stemOf(outerInstance->path).c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton("Select outer"))
      selectOnly(outer);
  }
  if (!serializer.prefabFileExists(root))
    ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "The prefab file is missing; the entities keep what was saved");

  // What the instance changed from the file; found twice a second, not every frame
  const double now = ImGui::GetTime();
  if (m_overrideRoot != root || now - m_overrideTime > 0.5) {
    m_overrideList = serializer.instanceOverrides(root);
    m_overrideRoot = root;
    m_overrideTime = now;
  }
  if (m_overrideList.empty()) {
    ImGui::TextDisabled("Matches the prefab");
  } else {
    ImGui::Text("%zu override(s)", m_overrideList.size());
    if (ImGui::IsItemHovered()) {
      ImGui::BeginTooltip();
      for (size_t i = 0; i < m_overrideList.size() && i < 20; ++i)
        ImGui::TextUnformatted(m_overrideList[i].c_str());
      if (m_overrideList.size() > 20)
        ImGui::TextDisabled("... and %zu more", m_overrideList.size() - 20);
      ImGui::EndTooltip();
    }
  }

  // Each is one edit (a click on the panel opened the undo step); Apply's step also holds the prefab file (see Engine::beginCheckpoint)
  const std::string applyLabel = outerInstance ? "Apply to " + stemOf(instance->path) : std::string("Apply");
  if (ImGui::SmallButton(applyLabel.c_str())) {
    serializer.applyInstance(root);
    m_overrideTime = -1000.0;
  }
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip(outerInstance ? "Writes this nested instance into its own prefab file (the outer prefab is not\n"
                                      "touched); every instance of that prefab follows"
                                    : "Writes this instance into the prefab file; every instance of it follows");
  if (outerInstance) {
    ImGui::SameLine();
    const std::string outerLabel = "Apply to " + stemOf(outerInstance->path);
    if (ImGui::SmallButton(outerLabel.c_str())) {
      serializer.applyInstance(outer);
      m_overrideTime = -1000.0;
    }
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Writes the outer instance into its prefab file; what differs here from %s becomes the outer\n"
                        "prefab's own override of it, and %s itself is not touched",
                        stemOf(instance->path).c_str(), stemOf(instance->path).c_str());
  }
  ImGui::SameLine();
  ImGui::BeginDisabled(m_overrideList.empty());
  if (ImGui::SmallButton("Revert")) {
    serializer.revertInstance(root);
    m_overrideTime = -1000.0;
  }
  ImGui::EndDisabled();
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    ImGui::SetTooltip(outerInstance ? "Drops what this nested instance changed from what the outer prefab makes of it\n"
                                        "(its place stays)"
                                    : "Drops what this instance changed from the prefab (its place stays)");
  ImGui::SameLine();
  if (ImGui::SmallButton("Unpack")) {
    serializer.unpackInstance(root);
    m_overrideTime = -1000.0;
  }
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip(outerInstance ? "Breaks this link: plain entities from now on; the outer instance counts them as removed"
                                    : "Breaks the link: plain entities from now on (instances nested inside stay linked)");
  ImGui::SameLine();
  ImGui::BeginDisabled(assetPath.empty());
  if (ImGui::SmallButton("Select prefab file")) {
    m_assets.reveal(assetPath);
    m_assetsOpenRequest = true;
  }
  ImGui::EndDisabled();
  (void)engine;
}
