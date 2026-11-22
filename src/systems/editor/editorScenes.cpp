// The editor's scene menu: File in the menu bar (New, Open, Recent, Save, Save As), its shortcuts, and the dialogs
// that ask about unsaved changes and about the file name.

#include "foundation/core/config.h"
#include "foundation/core/engine.h"
#include "foundation/core/logger.h"
#include "systems/cameraSystem.h"
#include "systems/editor/sceneFiles.h"
#include "systems/editorUISystem.h"
#include "systems/sceneSerializer.h"
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h> // BeginDragDropTargetCustom

namespace {
// Scene paths come in several spellings; this is the one the lists and the recent scenes use
std::string normalized(const std::string &path) {
  return path.empty() ? path : EngineConfig::relativePath(EngineConfig::resolvePath(path));
}

bool fileExists(const std::string &path) {
  std::error_code error;
  return std::filesystem::exists(EngineConfig::resolvePath(path), error);
}

std::string displayName(const std::string &path) { return std::filesystem::path(path).filename().string(); }

void setName(char *buffer, size_t size, const std::string &text) {
  std::strncpy(buffer, text.c_str(), size - 1);
  buffer[size - 1] = '\0';
}

constexpr const char *kTestingTip = "Not while a test is running: Stop first (the scene is restored then)";
} // namespace

void EditorUISystem::rememberScene(const std::string &path) {
  if (!path.empty())
    sceneFiles::pushRecent(m_recentScenes, normalized(path));
}

void EditorUISystem::requestScene(Engine &engine, SceneAction::Kind kind, const std::string &path) {
  if (engine.isTesting())
    return;
  const SceneAction action{kind, path};
  // Adding loses nothing; replacing loses every scene's changes, unloading the one scene's
  const bool unsaved = kind == SceneAction::Kind::Add      ? false
                       : kind == SceneAction::Kind::Unload ? engine.isSceneDirty(path)
                                                           : engine.isSceneDirty();
  if (unsaved)
    m_pendingScene = action; // asked about first
  else
    performScene(engine, action);
}

bool EditorUISystem::saveDirtyScenes(Engine &engine) {
  bool ok = true;
  for (const std::string &scene : engine.dirtyScenes()) {
    if (scene.empty()) {
      ok = false; // no file yet: only Save As can name it
    } else if (engine.saveOpenScene(scene)) {
      rememberScene(scene);
    } else {
      ok = false;
    }
  }
  return ok;
}

void EditorUISystem::performScene(Engine &engine, const SceneAction &action) {
  if (action.kind == SceneAction::Kind::New) {
    engine.newScene();
  } else if (action.kind == SceneAction::Kind::Open) {
    engine.loadScene(action.path);
    if (fileExists(action.path))
      rememberScene(action.path);
  } else if (action.kind == SceneAction::Kind::Add) {
    engine.loadSceneAdditive(action.path);
    if (fileExists(action.path))
      rememberScene(action.path);
  } else if (action.kind == SceneAction::Kind::Unload) {
    engine.unloadScene(action.path);
  }
}

void EditorUISystem::requestSave(Engine &engine) {
  if (engine.isTesting())
    return;
  const std::string active = engine.getScenePath();
  // The other scenes with changes first; the active one is always written
  for (const std::string &scene : engine.dirtyScenes())
    if (!scene.empty() && scene != active && engine.saveOpenScene(scene))
      rememberScene(scene);
  if (active.empty()) {
    openSaveAs();
    return;
  }
  if (engine.saveOpenScene(active))
    rememberScene(active);
}

bool EditorUISystem::saveSceneAs(Engine &engine, const std::string &name, std::string *error) {
  if (engine.isTesting()) {
    if (error)
      *error = kTestingTip;
    return false;
  }
  const std::string file = sceneFiles::fileName(name, error);
  if (file.empty())
    return false;
  const std::string path = std::string(EngineConfig::SCENES_DIR) + "/" + file;
  std::error_code ignored;
  std::filesystem::create_directories(EngineConfig::resolvePath(EngineConfig::SCENES_DIR), ignored);
  if (!engine.saveScene(path)) {
    if (error)
      *error = "Could not write " + path;
    return false;
  }
  rememberScene(path);
  return true;
}

void EditorUISystem::handleSceneShortcuts(Engine &engine, SystemManager &systemManager) {
  // Like Ctrl+P: not while typing, and not while flying the camera (Ctrl is a key for going down there). Nor with a
  // dialog open: it has its own buttons.
  const ImGuiIO &io = ImGui::GetIO();
  if (!io.KeyCtrl || io.KeyAlt || io.WantTextInput || systemManager.getSystem<CameraSystem>().isNavigating() ||
      ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel))
    return;
  if (ImGui::IsKeyPressed(ImGuiKey_N, false) && !io.KeyShift) {
    requestScene(engine, SceneAction::Kind::New);
  } else if (ImGui::IsKeyPressed(ImGuiKey_S, false)) {
    if (!io.KeyShift)
      requestSave(engine);
    else if (!engine.isTesting())
      openSaveAs();
  }
}

void EditorUISystem::renderMainMenuBar(Engine &engine) {
  if (!ImGui::BeginMainMenuBar())
    return;
  const bool testing = engine.isTesting();
  const std::string current = normalized(engine.getScenePath());
  auto tip = [&] {
    if (testing && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
      ImGui::SetTooltip("%s", kTestingTip);
  };

  if (ImGui::BeginMenu("File")) {
    if (ImGui::MenuItem("New Scene", "Ctrl+N", false, !testing))
      requestScene(engine, SceneAction::Kind::New);
    tip();
    if (ImGui::BeginMenu("Open Scene", !testing)) {
      const auto scenes = engine.getSceneSerializer().listScenes(EngineConfig::SCENES_DIR);
      if (scenes.empty())
        ImGui::TextDisabled("No scenes in %s", EngineConfig::SCENES_DIR);
      for (const std::string &scene : scenes)
        if (ImGui::MenuItem(displayName(scene).c_str(), nullptr, scene == current))
          requestScene(engine, SceneAction::Kind::Open, scene);
      ImGui::EndMenu();
    }
    tip();
    if (ImGui::BeginMenu("Add Scene...", !testing)) {
      std::vector<std::string> open;
      for (const std::string &path : engine.loadedScenes())
        open.push_back(SceneSerializer::sceneKey(path));
      bool any = false;
      for (const std::string &scene : engine.getSceneSerializer().listScenes(EngineConfig::SCENES_DIR)) {
        if (std::find(open.begin(), open.end(), SceneSerializer::sceneKey(scene)) != open.end())
          continue; // already open
        any = true;
        if (ImGui::MenuItem(displayName(scene).c_str()))
          requestScene(engine, SceneAction::Kind::Add, scene);
      }
      if (!any)
        ImGui::TextDisabled("No scene left to add");
      ImGui::EndMenu();
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
      ImGui::SetTooltip("%s", testing ? kTestingTip : "Opens another scene next to the ones open (several at once)");
    if (ImGui::BeginMenu("Recent", !testing)) {
      bool any = false;
      for (const std::string &scene : m_recentScenes) {
        if (!fileExists(scene))
          continue; // deleted or moved since
        any = true;
        if (ImGui::MenuItem(scene.c_str(), nullptr, scene == current))
          requestScene(engine, SceneAction::Kind::Open, scene);
      }
      if (!any)
        ImGui::TextDisabled("No recent scenes");
      ImGui::EndMenu();
    }
    tip();
    ImGui::Separator();
    if (ImGui::MenuItem("Save", "Ctrl+S", false, !testing))
      requestSave(engine);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
      ImGui::SetTooltip("%s", testing ? kTestingTip : "Saves every scene with unsaved changes");
    if (ImGui::MenuItem("Save As...", "Ctrl+Shift+S", false, !testing))
      openSaveAs();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
      ImGui::SetTooltip("%s", testing ? kTestingTip : "Saves the active scene under another name");
    ImGui::EndMenu();
  }

  // The active scene's file at the right, with +N for the other scenes open and a * for changes not saved
  const bool dirty = engine.isSceneDirty();
  const std::vector<std::string> open = engine.loadedScenes();
  std::string name = engine.getScenePath().empty() ? "untitled" : displayName(engine.getScenePath());
  if (open.size() > 1)
    name += " +" + std::to_string(open.size() - 1);
  const std::string label = name + (dirty ? " *" : "");
  const ImGuiStyle &style = ImGui::GetStyle();
  const float width = ImGui::CalcTextSize(label.c_str()).x;
  ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX() + style.ItemSpacing.x,
                                ImGui::GetWindowWidth() - width - style.WindowPadding.x * 2.0f));
  ImGui::TextUnformatted(label.c_str());
  if (ImGui::IsItemHovered()) {
    if (open.size() > 1) {
      ImGui::BeginTooltip();
      for (const std::string &scene : open) {
        const bool active = scene == engine.getScenePath();
        ImGui::Text("%s%s%s", scene.empty() ? "untitled" : displayName(scene).c_str(), active ? " (active)" : "",
                    engine.isSceneDirty(scene) ? " *" : "");
      }
      ImGui::EndTooltip();
    } else {
      ImGui::SetTooltip("%s", engine.getScenePath().empty() ? "No file yet: Save asks for a name"
                                                            : (dirty ? "Has changes that are not saved" : "Saved"));
    }
  }
  ImGui::EndMainMenuBar();
}

bool EditorUISystem::renderSceneHeader(Engine &engine, const EntityManager::Scene &scene, bool dirty) {
  const bool testing = engine.isTesting();
  const bool active = engine.getEntityManager().activeScene() == scene.id;
  const std::string name = scene.path.empty() ? "untitled" : displayName(scene.path);
  const std::string label = name + (dirty ? " *" : "") + (active ? "  (active)" : "") + "##scene" + std::to_string(scene.id);
  const ImVec4 accent(1.0f, 0.82f, 0.4f, 1.0f);
  if (active)
    ImGui::PushStyleColor(ImGuiCol_Text, accent);
  const bool open = ImGui::TreeNodeEx(label.c_str(), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_OpenOnArrow |
                                                         ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_Framed);
  if (active)
    ImGui::PopStyleColor();
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("%s%s\nDouble-click makes it the active scene (new entities go there). Right-click for more.\n"
                      "Drop an entity here to move it (and what is below it) to this scene.",
                      scene.path.empty() ? "No file yet" : scene.path.c_str(), dirty ? "  (unsaved changes)" : "");
    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
      engine.getEntityManager().setActiveScene(scene.id);
  }

  if (ImGui::BeginPopupContextItem("##sceneMenu")) {
    if (ImGui::MenuItem("Set Active", nullptr, false, !active))
      engine.getEntityManager().setActiveScene(scene.id);
    if (scene.path.empty()) {
      if (ImGui::MenuItem("Save As...", nullptr, false, !testing)) {
        engine.getEntityManager().setActiveScene(scene.id); // Save As names the active scene
        openSaveAs();
      }
    } else if (ImGui::MenuItem("Save", nullptr, false, !testing)) {
      if (engine.saveOpenScene(scene.path))
        rememberScene(scene.path);
    }
    const bool onlyOne = engine.getEntityManager().scenes().size() <= 1;
    if (ImGui::MenuItem("Unload", nullptr, false, !testing && !onlyOne))
      requestScene(engine, SceneAction::Kind::Unload, scene.path);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
      ImGui::SetTooltip("%s", testing ? kTestingTip : onlyOne ? "The only open scene stays (Open Scene replaces it)"
                                                               : "Removes the scene's entities from the editor");
    const std::string prefix(EngineConfig::ASSETS_PREFIX);
    const bool inAssets = scene.path.compare(0, prefix.size(), prefix) == 0;
    if (ImGui::MenuItem("Show in Assets", nullptr, false, inAssets)) {
      m_assets.reveal(scene.path.substr(prefix.size()));
      m_assetsOpenRequest = true;
    }
    ImGui::EndPopup();
  }

  // An entity dropped on the header moves, with its children, into this scene
  if (ImGui::BeginDragDropTarget()) {
    if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("ENTITY"))
      if (!testing)
        engine.moveToScene(*static_cast<const Entity *>(payload->Data), scene.path);
    ImGui::EndDragDropTarget();
  }
  return open;
}

void EditorUISystem::acceptSceneDrop(Engine &engine, ImVec2 min, ImVec2 max) {
  const ImGuiPayload *asset = ImGui::GetDragDropPayload();
  if (!asset || !asset->IsDataType(AssetBrowser::kPayload) || !asset->Data ||
      AssetBrowser::classify(static_cast<const char *>(asset->Data)) != AssetBrowser::Kind::Scene)
    return;
  if (ImGui::BeginDragDropTargetCustom(ImRect(min, max), ImGui::GetID("##sceneAddDrop"))) {
    if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(AssetBrowser::kPayload))
      requestScene(engine, SceneAction::Kind::Add,
                   std::string(EngineConfig::ASSETS_PREFIX) + static_cast<const char *>(payload->Data));
    ImGui::EndDragDropTarget();
  }
}

void EditorUISystem::renderSceneLoadDialog(Engine &engine) {
  if (!hasPendingSceneAction())
    return;
  if (engine.isTesting()) { // Play was pressed with the question open
    m_pendingScene = SceneAction();
    return;
  }
  constexpr const char *kTitle = "Unsaved changes";
  if (!ImGui::IsPopupOpen(kTitle))
    ImGui::OpenPopup(kTitle);
  if (!ImGui::BeginPopupModal(kTitle, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    return;
  const bool isNew = m_pendingScene.kind == SceneAction::Kind::New;
  const bool isUnload = m_pendingScene.kind == SceneAction::Kind::Unload;
  const size_t open = engine.loadedScenes().size();
  if (isUnload)
    ImGui::Text("%s has changes that are not saved.", displayName(m_pendingScene.path).c_str());
  else
    ImGui::Text(open > 1 ? "The scenes have changes that are not saved." : "The scene has changes that are not saved.");
  if (isNew)
    ImGui::TextDisabled("Start a new scene anyway?");
  else if (isUnload)
    ImGui::TextDisabled("Unload it anyway?");
  else
    ImGui::TextDisabled("Open %s anyway?", m_pendingScene.path.c_str());
  // The active scene with no file yet is named by Save As; an unload of one has no file to save to
  const bool activeUntitled = !isUnload && engine.getScenePath().empty() && engine.isSceneDirty("");
  const char *saveLabel = isNew ? "Save and start new" : isUnload ? "Save and unload" : "Save and open";
  ImGui::BeginDisabled(isUnload && m_pendingScene.path.empty());
  if (ImGui::Button(saveLabel)) {
    const SceneAction action = m_pendingScene;
    m_pendingScene = SceneAction();
    ImGui::CloseCurrentPopup();
    if (isUnload) {
      if (engine.saveOpenScene(action.path))
        performScene(engine, action);
    } else if (activeUntitled) {
      saveDirtyScenes(engine); // the others; Save As names the active one, then goes on
      m_afterSaveAs = action;
      openSaveAs();
    } else if (saveDirtyScenes(engine)) {
      performScene(engine, action);
    }
  }
  ImGui::EndDisabled();
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
    if (isUnload && m_pendingScene.path.empty())
      ImGui::SetTooltip("This scene has no file yet: make it the active one and use Save As");
    else if (activeUntitled)
      ImGui::SetTooltip("The active scene has no file yet: Save As asks for a name first");
  }
  ImGui::SameLine();
  if (ImGui::Button(isNew ? "Start new without saving" : isUnload ? "Unload without saving" : "Open without saving")) {
    const SceneAction action = m_pendingScene;
    m_pendingScene = SceneAction();
    ImGui::CloseCurrentPopup();
    performScene(engine, action);
  }
  ImGui::SameLine();
  if (ImGui::Button("Cancel")) {
    m_pendingScene = SceneAction();
    ImGui::CloseCurrentPopup();
  }
  ImGui::EndPopup();
}

void EditorUISystem::renderSaveAsDialog(Engine &engine) {
  constexpr const char *kTitle = "Save Scene As";
  if (m_saveAsRequest) {
    m_saveAsRequest = false;
    if (!engine.isTesting()) {
      // Starts from the current name, so Save As on a saved scene makes a copy under another one
      const std::string stem =
          engine.getScenePath().empty() ? std::string() : std::filesystem::path(engine.getScenePath()).stem().string();
      setName(m_saveAsName, sizeof(m_saveAsName), stem);
      m_saveAsOverwrite.clear();
      m_saveAsError.clear();
      ImGui::OpenPopup(kTitle);
    }
  }
  if (!ImGui::BeginPopupModal(kTitle, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    return;

  auto finish = [&](const std::string &name) {
    if (saveSceneAs(engine, name, &m_saveAsError)) {
      const SceneAction after = m_afterSaveAs;
      m_afterSaveAs = SceneAction();
      ImGui::CloseCurrentPopup();
      if (after.kind != SceneAction::Kind::None) {
        saveDirtyScenes(engine);
        performScene(engine, after);
      }
    }
  };

  if (!m_saveAsOverwrite.empty()) {
    ImGui::Text("%s already exists. Replace it?", m_saveAsOverwrite.c_str());
    if (ImGui::Button("Replace")) {
      const std::string name = m_saveAsOverwrite;
      m_saveAsOverwrite.clear();
      finish(name);
    }
    ImGui::SameLine();
    if (ImGui::Button("Back"))
      m_saveAsOverwrite.clear();
    ImGui::EndPopup();
    return;
  }

  ImGui::TextDisabled("Saved in %s", EngineConfig::SCENES_DIR);
  if (ImGui::IsWindowAppearing())
    ImGui::SetKeyboardFocusHere();
  const bool entered =
      ImGui::InputText("Name", m_saveAsName, sizeof(m_saveAsName), ImGuiInputTextFlags_EnterReturnsTrue);
  std::string reason;
  const std::string file = sceneFiles::fileName(m_saveAsName, &reason);
  const ImVec4 red(0.9f, 0.4f, 0.3f, 1.0f);
  if (file.empty() && m_saveAsName[0] != '\0')
    ImGui::TextColored(red, "%s", reason.c_str());
  if (!m_saveAsError.empty())
    ImGui::TextColored(red, "%s", m_saveAsError.c_str());

  const auto scenes = engine.getSceneSerializer().listScenes(EngineConfig::SCENES_DIR);
  ImGui::Text("Existing scenes");
  ImGui::BeginChild("##saveAsList", ImVec2(ImGui::GetFontSize() * 22.0f, ImGui::GetFontSize() * 8.0f), true);
  for (const std::string &scene : scenes) {
    const std::string label = displayName(scene);
    if (ImGui::Selectable(label.c_str(), label == file))
      setName(m_saveAsName, sizeof(m_saveAsName), std::filesystem::path(label).stem().string());
  }
  if (scenes.empty())
    ImGui::TextDisabled("None yet");
  ImGui::EndChild();

  const bool exists = !file.empty() && std::any_of(scenes.begin(), scenes.end(), [&](const std::string &scene) {
                        return displayName(scene) == file;
                      });
  ImGui::BeginDisabled(file.empty());
  const bool save = ImGui::Button("Save");
  ImGui::EndDisabled();
  if ((save || entered) && !file.empty()) {
    m_saveAsError.clear();
    if (exists)
      m_saveAsOverwrite = file; // asked about first
    else
      finish(file);
  }
  ImGui::SameLine();
  if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
    m_afterSaveAs = SceneAction();
    ImGui::CloseCurrentPopup();
  }
  ImGui::EndPopup();
}
