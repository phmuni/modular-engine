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
  if (engine.isSceneDirty())
    m_pendingScene = action; // asked about first
  else
    performScene(engine, action);
}

void EditorUISystem::performScene(Engine &engine, const SceneAction &action) {
  if (action.kind == SceneAction::Kind::New) {
    engine.newScene();
  } else if (action.kind == SceneAction::Kind::Open) {
    engine.loadScene(action.path);
    if (fileExists(action.path))
      rememberScene(action.path);
  }
}

void EditorUISystem::requestSave(Engine &engine) {
  if (engine.isTesting())
    return;
  if (engine.getScenePath().empty()) {
    openSaveAs();
    return;
  }
  if (engine.saveScene(engine.getScenePath()))
    rememberScene(engine.getScenePath());
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
    tip();
    if (ImGui::MenuItem("Save As...", "Ctrl+Shift+S", false, !testing))
      openSaveAs();
    tip();
    ImGui::EndMenu();
  }

  // The scene's file at the right, with a * for changes not saved
  const bool dirty = engine.isSceneDirty();
  const std::string name = engine.getScenePath().empty() ? "untitled" : displayName(engine.getScenePath());
  const std::string label = name + (dirty ? " *" : "");
  const ImGuiStyle &style = ImGui::GetStyle();
  const float width = ImGui::CalcTextSize(label.c_str()).x;
  ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX() + style.ItemSpacing.x,
                                ImGui::GetWindowWidth() - width - style.WindowPadding.x * 2.0f));
  ImGui::TextUnformatted(label.c_str());
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("%s", engine.getScenePath().empty() ? "No file yet: Save asks for a name"
                                                          : (dirty ? "Has changes that are not saved" : "Saved"));
  ImGui::EndMainMenuBar();
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
  ImGui::Text("The scene has changes that are not saved.");
  if (isNew)
    ImGui::TextDisabled("Start a new scene anyway?");
  else
    ImGui::TextDisabled("Open %s anyway?", m_pendingScene.path.c_str());
  const bool hasFile = !engine.getScenePath().empty();
  if (ImGui::Button(isNew ? "Save and start new" : "Save and open")) {
    const SceneAction action = m_pendingScene;
    m_pendingScene = SceneAction();
    ImGui::CloseCurrentPopup();
    if (hasFile) {
      if (engine.saveScene(engine.getScenePath()))
        performScene(engine, action);
    } else {
      m_afterSaveAs = action; // no file yet: Save As asks for a name, then goes on
      openSaveAs();
    }
  }
  if (!hasFile && ImGui::IsItemHovered())
    ImGui::SetTooltip("This scene has no file yet: Save As asks for a name first");
  ImGui::SameLine();
  if (ImGui::Button(isNew ? "Start new without saving" : "Open without saving")) {
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
      if (after.kind != SceneAction::Kind::None)
        performScene(engine, after);
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
