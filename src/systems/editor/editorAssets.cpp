// The editor's asset panel and what its assets do (drag into the view, double-click, context menu), and the
// Properties section of linked prefab instances. Part of EditorUISystem, kept apart from editorUISystem.cpp.

#include "systems/editorUISystem.h"

#include "components/audioSource.h"
#include "components/camera.h"
#include "components/hidden.h"
#include "components/model.h"
#include "components/name.h"
#include "components/parent.h"
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
std::string nameOfAsset(const std::string &asset) { return std::filesystem::path(asset).stem().string(); }
} // namespace

void EditorUISystem::renderAssetPanel(Engine &engine, SystemManager &systemManager, ComponentManager &componentManager) {
  const ImGuiIO &io = ImGui::GetIO();
  // A strip along the bottom of the view, between the side panels. Collapsed it is only its title bar, so the view
  // stays free until it is wanted; it opens from its arrow.
  const float left = m_explorerWidth;
  const float width = std::max(io.DisplaySize.x - left - m_propertiesWidth, ui(200.0f));
  const float expanded = std::clamp(io.DisplaySize.y * 0.3f, ui(170.0f), io.DisplaySize.y * 0.5f);
  const float height = m_assetsCollapsed ? ImGui::GetFrameHeight() : expanded;
  m_assetsHeight = height;
  ImGui::SetNextWindowPos(ImVec2(left, io.DisplaySize.y - height));
  ImGui::SetNextWindowSize(ImVec2(width, height));
  if (m_assetsOpenRequest) {
    ImGui::SetNextWindowCollapsed(false, ImGuiCond_Always);
    m_assetsOpenRequest = false;
  } else {
    ImGui::SetNextWindowCollapsed(true, ImGuiCond_FirstUseEver);
  }
  const bool open = ImGui::Begin("Assets", nullptr,
                                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings);
  m_assetsCollapsed = ImGui::IsWindowCollapsed();
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
  if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow))
    return; // over a panel: its own drop targets take it
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
