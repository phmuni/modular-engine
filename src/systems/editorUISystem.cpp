// Editor UI implementation: the ImGui scene explorer, properties panel and settings panel (F2), apart from the game
// UI that games build with RmlUi (GameUISystem).

#include "systems/editorUISystem.h"
#include <RmlUi/Core/Context.h>
#include "components/animationStateMachine.h"
#include "components/animator.h"
#include "components/audioSource.h"
#include "components/cameraRig.h"
#include "components/collision.h"
#include "components/hidden.h"
#include "components/joint.h"
#include "components/light.h"
#include "components/model.h"
#include "components/name.h"
#include "components/parent.h"
#include "components/prefabInstance.h"
#include "components/particleEmitter.h"
#include "components/reflectionProbe.h"
#include "components/skybox.h"
#include "components/characterController.h"
#include "components/rigidBody.h"
#include "components/script.h"
#include "components/tags.h"
#include "components/transform.h"
#include "foundation/core/engine.h"
#include "foundation/core/logger.h"
#include "systems/animationGraph.h"
#include "systems/animationSystem.h"
#include "systems/editor/sceneFiles.h"
#include "systems/editor/sceneFilter.h"
#include "systems/editor/surfacePlacement.h"
#include "systems/modelQueries.h"
#include "systems/navigationSystem.h"
#include "systems/scriptSystem.h"
#include "rendering/resources/material.h"
#include "rendering/resources/mesh.h"
#include "rendering/resources/textureUploader.h"
#include "systems/audioSystem.h"
#include "systems/sceneSerializer.h"
#include "systems/cameraSystem.h"
#include "systems/inputSystem.h"
#include "systems/lightSystem.h"
#include "systems/renderSystem.h"
#include "systems/resourceSystem.h"
#include "systems/sceneSystem.h"
#include "systems/stateSystem.h"
#include "systems/timeSystem.h"
#include "systems/transformSystem.h"
#include "systems/windowSystem.h"

#include <SDL3/SDL_clipboard.h>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <filesystem>
#include <functional>
#include <iterator>
#include <limits>
#include <array>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/euler_angles.hpp>
#include <glm/gtx/quaternion.hpp>
#include <imgui/imgui.h>
#include <imgui/imgui_internal.h> // the settings handler that keeps the interface scale in imgui.ini
#include <imgui/imgui_impl_opengl3.h>
#include <imgui/imgui_impl_sdl3.h>
#include <ImGuizmo.h> // after imgui.h, which it does not include itself
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// Opens a file with the program the system uses for its type. A native path is not a URL (C:\x\y.lua on
// Windows), so it becomes file:///C:/x/y.lua first (AssetBrowser::fileUrl). When no program handles .lua files
// (common on Windows), it tries VS Code's `code` command, then shows the folder.
void openInEditor(const std::string &path) {
  std::error_code error;
  std::filesystem::path absolute = std::filesystem::absolute(path, error);
  if (error)
    absolute = path;
  if (SDL_OpenURL(AssetBrowser::fileUrl(absolute.generic_string()).c_str()))
    return;
  LOG_W("[Editor] No program opens %s (%s); trying VS Code", absolute.string().c_str(), SDL_GetError());
#ifdef _WIN32
  const std::string command = "start \"\" code \"" + absolute.string() + "\"";
#else
  const std::string command = "code \"" + absolute.string() + "\" &";
#endif
  if (std::system(command.c_str()) != 0)
    SDL_OpenURL(AssetBrowser::fileUrl(absolute.parent_path().generic_string()).c_str());
}

namespace {
// The style before any scaling: each scale starts from it, so scales do not compound
ImGuiStyle g_baseStyle;
constexpr float kBaseFontSize = 13.0f; // ImGui's built-in font
} // namespace

EditorUISystem::EditorUISystem(SDL_Window *window, SDL_GLContext glContext) : m_window(window) {
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable; // the panels dock around the Game view
  ImGui::StyleColorsDark();
  g_baseStyle = ImGui::GetStyle();

  ImGui_ImplSDL3_InitForOpenGL(window, glContext);
  ImGui_ImplOpenGL3_Init("#version 330");
  registerSettings();
  applyUiScale();
}

// This ImGui has no FontScaleMain: the font atlas is rebuilt at the scale (and at the framebuffer's density, drawn
// back at 1/density), and the style's sizes scaled with it. Before NewFrame, never inside a frame.
void EditorUISystem::applyUiScale() {
  const float density = std::max(SDL_GetWindowPixelDensity(m_window), 1.0f);
  const float displayScale = SDL_GetWindowDisplayScale(m_window);
  const float automatic = displayScale > 0.0f ? displayScale / density : 1.0f;
  m_uiScale = std::clamp(m_uiScaleUser > 0.0f ? m_uiScaleUser : automatic, kMinUiScale, kMaxUiScale);
  if (m_uiScale == m_appliedScale && density == m_appliedDensity)
    return;
  m_appliedScale = m_uiScale;
  m_appliedDensity = density;

  ImGuiIO &io = ImGui::GetIO();
  io.Fonts->Clear();
  ImFontConfig font;
  font.SizePixels = kBaseFontSize * m_uiScale * density;
  io.Fonts->AddFontDefault(&font);
  io.FontGlobalScale = 1.0f / density;
  ImGui_ImplOpenGL3_DestroyFontsTexture(); // NewFrame builds it again
  ImGuiStyle &style = ImGui::GetStyle();
  style = g_baseStyle;
  style.ScaleAllSizes(m_uiScale);
}

// "[ModularEditor][UI]" with the scale the user chose, next to the panels' places in imgui.ini
void EditorUISystem::registerSettings() {
  ImGuiSettingsHandler handler;
  handler.TypeName = "ModularEditor";
  handler.TypeHash = ImHashStr("ModularEditor");
  handler.UserData = this;
  handler.ReadOpenFn = [](ImGuiContext *, ImGuiSettingsHandler *handler, const char *name) -> void * {
    static int entry;
    if (std::strcmp(name, "UI") != 0)
      return nullptr;
    static_cast<EditorUISystem *>(handler->UserData)->m_recentScenes.clear(); // the file's list replaces it
    return &entry;
  };
  handler.ReadLineFn = [](ImGuiContext *, ImGuiSettingsHandler *handler, void *, const char *line) {
    float scale = 0.0f;
    int maximizeOnPlay = 0;
    auto *editor = static_cast<EditorUISystem *>(handler->UserData);
    if (std::sscanf(line, "Scale=%f", &scale) == 1)
      editor->m_uiScaleUser = scale > 0.0f ? std::clamp(scale, kMinUiScale, kMaxUiScale) : 0.0f;
    else if (std::sscanf(line, "MaximizeOnPlay=%d", &maximizeOnPlay) == 1)
      editor->m_maximizeOnPlay = maximizeOnPlay != 0;
    else if (std::strncmp(line, "Recent=", 7) == 0 && line[7] != '\0' && editor->m_recentScenes.size() < sceneFiles::kMaxRecent)
      editor->m_recentScenes.emplace_back(line + 7); // written newest first
  };
  handler.WriteAllFn = [](ImGuiContext *, ImGuiSettingsHandler *handler, ImGuiTextBuffer *out) {
    const auto *editor = static_cast<EditorUISystem *>(handler->UserData);
    out->appendf("[%s][UI]\nScale=%.3f\nMaximizeOnPlay=%d\n", handler->TypeName, editor->m_uiScaleUser,
                 editor->m_maximizeOnPlay ? 1 : 0);
    for (const std::string &scene : editor->m_recentScenes)
      out->appendf("Recent=%s\n", scene.c_str());
    out->append("\n");
  };
  ImGui::AddSettingsHandler(&handler);
}

float EditorUISystem::panelWidth(float base) const {
  // Never more than a bit over a quarter of the window, so the view keeps its middle
  return std::min(ui(base), ImGui::GetIO().DisplaySize.x * 0.27f);
}

void EditorUISystem::fileDialogCallback(void *userdata, const char *const *filelist, int /*filter*/) {
  if (!userdata)
    return;
  auto *buf = static_cast<char *>(userdata);
  if (filelist && filelist[0]) {
    std::strncpy(buf, filelist[0], 255);
    buf[255] = '\0';

    for (int i = 0; buf[i] != '\0'; ++i) {
      if (buf[i] == '\\')
        buf[i] = '/';
    }
  }
}

void EditorUISystem::pickFileButton(const char *id, char *buf, int /*bufSize*/, SDL_Window *window,
                                    const SDL_DialogFileFilter *filters, int nfilters) {
  ImGui::SameLine();
  std::string label = std::string("...") + "##" + id;
  if (ImGui::Button(label.c_str())) {
    SDL_ShowOpenFileDialog(fileDialogCallback, buf, window, filters, nfilters, nullptr, true);
  }
}

void EditorUISystem::beginFrame(Engine &engine) {
  applyUiScale();
  ImGui_ImplOpenGL3_NewFrame();
  ImGui_ImplSDL3_NewFrame();
  ImGui::NewFrame();
  ImGuizmo::BeginFrame();
  beginDockSpace(engine);
}

namespace {
constexpr const char *kGamePanel = "Game";
constexpr float kExplorerWidth = 260.0f, kPropertiesWidth = 340.0f; // the default layout's columns, at scale 1
// Fixed, so the layout kept in imgui.ini finds its dock space again
ImGuiID dockSpaceId() { return ImHashStr("ModularEditorDockSpace"); }
} // namespace

// The default layout, like the fixed panels it replaces: the Scene Explorer and Settings in a column at the left, the
// Properties at the right, the Assets along the bottom, and the Game in the middle
void EditorUISystem::buildDefaultLayout(ImGuiID dockSpace, float width, float height) {
  width = std::max(width, 1.0f);
  const float left = panelWidth(kExplorerWidth) / width, right = panelWidth(kPropertiesWidth) / width;
  ImGui::DockBuilderRemoveNode(dockSpace);
  ImGui::DockBuilderAddNode(dockSpace, ImGuiDockNodeFlags_DockSpace);
  ImGui::DockBuilderSetNodeSize(dockSpace, ImVec2(width, std::max(height, 1.0f)));
  ImGuiID center = dockSpace, leftColumn = 0, rightColumn = 0, bottom = 0, settings = 0;
  leftColumn = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, left, nullptr, &center);
  rightColumn = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, right / (1.0f - left), nullptr, &center);
  bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.28f, nullptr, &center);
  settings = ImGui::DockBuilderSplitNode(leftColumn, ImGuiDir_Down, 0.45f, nullptr, &leftColumn);
  ImGui::DockBuilderDockWindow("Scene Explorer", leftColumn);
  ImGui::DockBuilderDockWindow("Settings", settings);
  ImGui::DockBuilderDockWindow("Properties", rightColumn);
  ImGui::DockBuilderDockWindow("Assets", bottom);
  ImGui::DockBuilderDockWindow(kGamePanel, center);
  ImGui::DockBuilderFinish(dockSpace);
}

// The dock space and the Game panel are submitted first in the frame, so the Game panel's rectangle (the game view)
// is known before the scene is drawn. While the editor is closed or maximized the dock space is only kept alive
// (never shown), so the layout is still there, as it was, when the panels come back.
void EditorUISystem::beginDockSpace(Engine &engine) {
  const ImGuiIO &io = ImGui::GetIO();
  const ImGuiViewport *viewport = ImGui::GetMainViewport();
  const ImGuiID dockSpace = dockSpaceId();
  const bool previousMouse = m_gameMouse;

  m_docked = false;
  m_gameMouse = m_gameFocused = m_gameHovered = false;
  m_viewRect = glm::vec4(0.0f, 0.0f, io.DisplaySize.x, io.DisplaySize.y);
  m_viewPixels = ViewRect();

  if (!(m_renderUI && !m_panelsHidden)) {
    if (ImGui::DockBuilderGetNode(dockSpace))
      ImGui::DockSpaceOverViewport(dockSpace, viewport,
                                   ImGuiDockNodeFlags_KeepAliveOnly | ImGuiDockNodeFlags_PassthruCentralNode);
    return;
  }

  // The menu bar first: it takes the top of the work area, which the dock space then fills. Maximized, it is hidden
  // with the panels: the game gets the whole window (the shortcuts still work).
  renderMainMenuBar(engine);

  // No layout yet (first run, or an imgui.ini from before docking, which has only window positions): the default one
  if (m_resetLayout || !ImGui::DockBuilderGetNode(dockSpace))
    // The bar's height is taken off by hand: in the bar's first frame the work area does not have it yet
    buildDefaultLayout(dockSpace, viewport->Size.x, viewport->Size.y - ImGui::GetFrameHeight());
  m_resetLayout = false;
  // A passthru dock space leaves a hole in its background only while the central node is empty; with the Game panel
  // docked there it would paint WindowBg over the whole picture. The panels draw their own backgrounds, so the dock
  // space's is not needed at all.
  ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
  ImGui::DockSpaceOverViewport(dockSpace, viewport, ImGuiDockNodeFlags_PassthruCentralNode);
  ImGui::PopStyleColor();
  m_docked = true;

  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
  constexpr ImGuiWindowFlags kFlags = ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar |
                                      ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoNavInputs;
  const bool visible = ImGui::Begin(kGamePanel, nullptr, kFlags);
  ImGui::PopStyleVar();
  if (visible) {
    const ImVec2 position = ImGui::GetCursorScreenPos(), size = ImGui::GetContentRegionAvail();
    m_gameHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem); // also mid drag and drop
    m_gameFocused = ImGui::IsWindowFocused();
    // The panel's own mouse state: over it, or still holding a press that began there
    m_gameMouse = ImGui::IsAnyMouseDown() ? previousMouse : m_gameHovered;
    if (size.x >= 8.0f && size.y >= 8.0f)
      m_lastGameRect = glm::vec4(position.x, position.y, size.x, size.y);
  }
  ImGui::End();

  if (m_lastGameRect.z > 0.0f) {
    // Whole framebuffer pixels, so the picture and the mouse agree; y from the top
    const ImVec2 scale = io.DisplayFramebufferScale;
    const float fx = scale.x > 0.0f ? scale.x : 1.0f, fy = scale.y > 0.0f ? scale.y : 1.0f;
    const int x0 = static_cast<int>(std::lround(m_lastGameRect.x * fx)), y0 = static_cast<int>(std::lround(m_lastGameRect.y * fy));
    const int x1 = static_cast<int>(std::lround((m_lastGameRect.x + m_lastGameRect.z) * fx));
    const int y1 = static_cast<int>(std::lround((m_lastGameRect.y + m_lastGameRect.w) * fy));
    m_viewPixels = ViewRect{x0, y0, std::max(x1 - x0, 1), std::max(y1 - y0, 1), true};
    m_viewRect = glm::vec4(x0 / fx, y0 / fy, m_viewPixels.width / fx, m_viewPixels.height / fy);
    // What is drawn over the view (selection boxes, the gizmo) stays out of the panels
    ImGui::GetBackgroundDrawList()->PushClipRect(ImVec2(m_viewRect.x, m_viewRect.y),
                                                 ImVec2(m_viewRect.x + m_viewRect.z, m_viewRect.y + m_viewRect.w), false);
  }
}

bool EditorUISystem::mouseOnPanels() const { return ImGui::GetIO().WantCaptureMouse && !m_gameMouse; }

void EditorUISystem::endFrame() {
  ImGui::Render();
  ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void EditorUISystem::update(SystemManager &systemManager) {
  auto &state = systemManager.getSystem<StateSystem>();
  auto &windowSystem = systemManager.getSystem<WindowSystem>();

  m_renderUI = state.isToggled(Toggle::ShowUI);
  m_showStats = state.isToggled(Toggle::ShowStats);

  // Opening the editor gives the cursor back (a game that locks it for aiming would leave the panels unreachable),
  // and closing it gives the game the lock it had
  if (m_renderUI && !m_wasShown) {
    m_lockBeforeShown = state.isToggled(Toggle::CursorLock);
    state.setToggle(Toggle::CursorLock, false);
  } else if (!m_renderUI && m_wasShown && !state.isToggled(Toggle::CursorLock)) {
    state.setToggle(Toggle::CursorLock, m_lockBeforeShown);
  }
  m_wasShown = m_renderUI;

  // Looking around with the right button holds the mouse like the lock does
  windowSystem.setCursor(state.isToggled(Toggle::CursorLock) ||
                         systemManager.getSystem<InputSystem>().isEditorFlying());
}

namespace {
const ImVec4 kPrefabLinkColor(0.45f, 0.7f, 1.0f, 1.0f); // linked prefab instances in the lists

// Name of the key bound to a toggle in the default keybinds, or nullptr
const char *toggleKeyName(Toggle toggle) {
  for (const auto &[scancode, bound] : EngineConfig::DEFAULT_TOGGLE_KEYBINDS)
    if (bound == toggle)
      return SDL_GetScancodeName(scancode);
  return nullptr;
}
} // namespace

void EditorUISystem::recordFrame(float frameSeconds, float workSeconds) {
  m_statsElapsed += frameSeconds;
  m_statsWork += workSeconds;
  m_statsMaxFrame = std::max(m_statsMaxFrame, frameSeconds);
  ++m_statsFrames;

  if (m_statsElapsed < kStatsWindow)
    return;

  m_shownFps = static_cast<float>(m_statsFrames) / m_statsElapsed;
  m_shownFrameMs = m_statsElapsed * 1000.0f / static_cast<float>(m_statsFrames);
  m_shownWorkMs = m_statsWork * 1000.0f / static_cast<float>(m_statsFrames);
  m_shownMaxMs = m_statsMaxFrame * 1000.0f;

  m_statsElapsed = m_statsWork = m_statsMaxFrame = 0.0f;
  m_statsFrames = 0;
}

void EditorUISystem::renderViewportHint(SystemManager &systemManager) {
  auto &cameraSystem = systemManager.getSystem<CameraSystem>();
  if (!cameraSystem.canNavigate(systemManager.getSystem<StateSystem>()))
    return;
  ImGui::SetNextWindowPos(ImVec2(m_viewRect.x + m_viewRect.z * 0.5f, m_viewRect.y + m_viewRect.w - ui(10.0f)),
                          ImGuiCond_Always, ImVec2(0.5f, 1.0f));
  ImGui::SetNextWindowBgAlpha(0.45f);
  constexpr ImGuiWindowFlags kFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                                      ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                                      ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs;
  if (ImGui::Begin("##viewportHint", nullptr, kFlags)) {
    if (cameraSystem.isNavigating()) {
      ImGui::Text("Flying   speed x%.2f   Shift: faster   Q/E: down/up", cameraSystem.getFlySpeedScale());
    } else {
      ImGui::TextDisabled("Right-drag: look  |  WASD: fly  |  Wheel: speed  |  Drag an object: move it (Align on: onto surfaces)");
    }
  }
  ImGui::End();
}

void EditorUISystem::renderStatsOverlay() {
  constexpr float kMargin = 10.0f;
  ImGui::SetNextWindowPos(ImVec2(m_viewRect.x + m_viewRect.z - kMargin, m_viewRect.y + kMargin), ImGuiCond_Always,
                          ImVec2(1.0f, 0.0f));
  ImGui::SetNextWindowBgAlpha(0.6f);

  constexpr ImGuiWindowFlags kFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                                      ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                                      ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs;
  if (ImGui::Begin("##stats", nullptr, kFlags)) {
    ImGui::Text("%.0f FPS", m_shownFps);
    ImGui::Text("frame  %6.2f ms", m_shownFrameMs);
    ImGui::Text("work   %6.2f ms", m_shownWorkMs);
    ImGui::Text("max    %6.2f ms", m_shownMaxMs);
  }
  ImGui::End();
}

namespace {
// A dropdown of the collision layers; true when another was picked. A name no layer has (a scene made for other
// layers) is shown as it is, and counts as "default" until changed.
bool layerCombo(const char *label, std::string &layer, const CollisionLayers &layers) {
  bool changed = false;
  const std::string shown = layers.indexOf(layer) < 0 ? layer + " (no such layer)" : layer;
  if (ImGui::BeginCombo(label, shown.c_str())) {
    for (const std::string &name : layers.names())
      if (ImGui::Selectable(name.c_str(), name == layer)) {
        layer = name;
        changed = true;
      }
    ImGui::EndCombo();
  }
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Which layers it collides with is set in Settings, Collision layers");
  return changed;
}
} // namespace

void EditorUISystem::renderSettingsPanel(Engine &engine, EntityManager &entityManager, SystemManager &systemManager,
                                         ComponentManager &componentManager) {
  auto &state = systemManager.getSystem<StateSystem>();
  auto &timeSystem = systemManager.getSystem<TimeSystem>();
  auto &renderSystem = systemManager.getSystem<RenderSystem>();
  auto &cameraSystem = systemManager.getSystem<CameraSystem>();

  // Docked under the Scene Explorer in the default layout
  if (!ImGui::Begin("Settings")) {
    ImGui::End();
    return;
  }
  // Leave room for slider labels
  ImGui::PushItemWidth(-ImGui::GetFontSize() * 6.0f);

  auto toggleCheckbox = [&](const char *label, Toggle toggle, const char *tooltip = nullptr) {
    bool value = state.isToggled(toggle);
    if (ImGui::Checkbox(label, &value))
      state.setToggle(toggle, value);
    if (tooltip && ImGui::IsItemHovered())
      ImGui::SetTooltip("%s", tooltip);
    if (const char *key = toggleKeyName(toggle)) {
      ImGui::SameLine();
      ImGui::TextDisabled("[%s]", key);
    }
  };

  if (ImGui::CollapsingHeader("Performance")) {
    const RenderStats &stats = renderSystem.getRenderer().getStats();
    ImGui::Text("%.0f FPS  |  %.2f ms frame", m_shownFps, m_shownFrameMs);
    ImGui::Text("Work before swap: %.2f ms", m_shownWorkMs);
    ImGui::Text("Draw calls: %d", stats.drawCalls);
    ImGui::Text("Meshes drawn: %d  (culled: %d)", stats.meshesDrawn, renderSystem.getCulledCount());
    if (int pointShadows = renderSystem.getPointShadowCount())
      ImGui::Text("Point shadows: %d  (re-rendered this frame: %d)", pointShadows,
                  renderSystem.getPointShadowUpdates());
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Includes the shadow pass; culled meshes were outside the camera");
    {
      const ClusteredLights::Stats &lights = renderSystem.lightStats();
      ImGui::Text("Lights: %d point/spot, %d directional (cluster build %.2f ms)", lights.lights, lights.directional,
                  lights.buildMs);
    }
    ImGui::Text("Particles: %d", stats.particles);
    ImGui::Text("Entities: %zu", entityManager.count());
    auto &resourceSystem = systemManager.getSystem<ResourceSystem>();
    ImGui::Text("Materials: %zu  |  Cached textures: %zu", resourceSystem.getMaterialCount(),
                resourceSystem.getTextureCount());
    if (size_t streaming = TextureUploader::pendingCount())
      ImGui::Text("Textures still uploading: %zu", streaming);
    toggleCheckbox("Performance overlay", Toggle::ShowStats);
    toggleCheckbox("VSync", Toggle::VSync, "Off shows how fast the engine can actually run");
  }

  if (ImGui::CollapsingHeader("Rendering")) {
    toggleCheckbox("Wireframe", Toggle::Wireframe);
    toggleCheckbox("Shadows", Toggle::Shadows);
    toggleCheckbox("Gamma-correct lighting", Toggle::GammaCorrect, "Lights in linear space; off is the old look");
    toggleCheckbox("Bloom", Toggle::Bloom, "Glow around light brighter than white (emission, highlights)");
    toggleCheckbox("Game UI debugger", Toggle::UIDebugger, "RmlUi's element inspector and log for the game UI");
    ImGui::Checkbox("Show navigation", &m_showNavigation);
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("The walkable cells of the navigation map (nav.build) and the last path found");
  }

  if (ImGui::CollapsingHeader("Simulation")) {
    toggleCheckbox("Paused", Toggle::Paused, "Freezes particles and game logic; the camera keeps moving");
    float timeScale = timeSystem.getTimeScale();
    if (ImGui::SliderFloat("Time scale", &timeScale, 0.0f, 4.0f, "%.2fx"))
      timeSystem.setTimeScale(timeScale);
    if (ImGui::SmallButton("Reset to 1x"))
      timeSystem.setTimeScale(1.0f);
  }

  if (ImGui::CollapsingHeader("Interface", ImGuiTreeNodeFlags_DefaultOpen)) {
    int percent = static_cast<int>(std::lround(m_uiScale * 100.0f));
    if (ImGui::SliderInt("UI scale", &percent, static_cast<int>(kMinUiScale * 100.0f),
                         static_cast<int>(kMaxUiScale * 100.0f), "%d%%")) {
      m_uiScaleUser = static_cast<float>(percent) / 100.0f;
      ImGui::MarkIniSettingsDirty();
    }
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Size of the panels and text. Starts from the display's scale (%.0f%% here); kept between runs",
                        systemManager.getSystem<WindowSystem>().getAutoUiScale() * 100.0f);
    // The 3D scene only: these panels stay at the screen's own resolution
    int renderPercent = static_cast<int>(std::lround(engine.renderScale() * 100.0f));
    if (ImGui::SliderInt("Render scale", &renderPercent, static_cast<int>(Renderer::kMinRenderScale * 100.0f),
                         static_cast<int>(Renderer::kMaxRenderScale * 100.0f), "%d%%"))
      engine.setRenderScale(static_cast<float>(renderPercent) / 100.0f);
    if (ImGui::IsItemHovered()) {
      const glm::ivec2 scene = engine.renderResolution();
      ImGui::SetTooltip("Resolution the scene is drawn at, as a share of the screen's pixels (now %d x %d): below "
                        "100%% is faster, above sharper. The panels are not affected.",
                        scene.x, scene.y);
    }
    // Antialiasing and upscaling (graphicsSettings.h)
    {
      static const char *const kMsaaNames[] = {"Off", "MSAA 2x", "MSAA 4x", "MSAA 8x"};
      const int samples = engine.msaa();
      int msaaIndex = samples >= 8 ? 3 : samples >= 4 ? 2 : samples >= 2 ? 1 : 0;
      if (ImGui::Combo("Antialiasing", &msaaIndex, kMsaaNames, 4))
        engine.setMsaa(msaaIndex == 0 ? 0 : 1 << msaaIndex);
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Multisampling of the 3D scene (the GPU's maximum caps it). Also smooths the edges of "
                          "cut-out materials.");
      bool fxaa = engine.fxaa();
      if (ImGui::Checkbox("FXAA", &fxaa))
        engine.setFxaa(fxaa);
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Post filter on the tone mapped image; softens remaining jagged edges");
      bool fsr = engine.fsr();
      if (ImGui::Checkbox("FSR upscaling", &fsr))
        engine.setFsr(fsr);
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("AMD FidelityFX Super Resolution 1: upscales the scene when Render scale is below 100%%");
      float sharpness = engine.fsrSharpness();
      if (ImGui::SliderFloat("FSR sharpness", &sharpness, kMinFsrSharpness, kMaxFsrSharpness, "%.2f stops"))
        engine.setFsrSharpness(sharpness);
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("RCAS sharpening: 0 is the sharpest, each stop halves it (AMD's default is 0.2)");
    }
    // Ambient occlusion (the scene's radius and intensity are the Skybox's: this edits those when there is one)
    bool ssaoOn = engine.ssao();
    if (ImGui::Checkbox("Ambient occlusion", &ssaoOn))
      engine.setSsao(ssaoOn);
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Contact shadows in the ambient light, where objects meet and in creases. Direct lights and "
                        "emission are not affected.");
    if (ssaoOn) {
      SsaoSettings ssao = engine.ssaoSettings();
      bool changed = false;
      int quality = static_cast<int>(ssao.quality);
      if (ImGui::Combo("Quality##ssao", &quality, "Low\0Medium\0High\0")) {
        ssao.quality = static_cast<SsaoQuality>(quality);
        changed = true;
      }
      changed |= ImGui::Checkbox("Half resolution##ssao", &ssao.halfResolution);
      changed |= ImGui::DragFloat("Radius##ssao", &ssao.radius, 0.01f, SsaoSettings::kMinRadius,
                                  SsaoSettings::kMaxRadius, "%.2f");
      changed |= ImGui::DragFloat("Intensity##ssao", &ssao.intensity, 0.01f, 0.0f, SsaoSettings::kMaxIntensity, "%.2f");
      if (changed)
        engine.setSsaoSettings(ssao);
      bool debugView = engine.ssaoDebugView();
      if (ImGui::Checkbox("Show AO only##ssao", &debugView))
        engine.setSsaoDebugView(debugView);
    }
    {
      bool heatMap = engine.lightHeatMap();
      if (ImGui::Checkbox("Light heat map", &heatMap))
        engine.setLightHeatMap(heatMap);
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Colors the scene by how many lights reach each pixel (blue: 1, red: 16 or more)");
    }
    if (ImGui::SmallButton("Reset scale")) {
      m_uiScaleUser = 0.0f;
      ImGui::MarkIniSettingsDirty();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Reset layout"))
      m_resetLayout = true;
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Puts every panel back where it starts");
    if (ImGui::Button("Maximize view"))
      toggleMaximized();
    ImGui::SameLine();
    ImGui::TextDisabled("[Shift+Space, Tab]");
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Hides every panel and keeps the editor going: click to select, the gizmo, flying the camera");
    ImGui::Checkbox("Maximize on Play", &m_maximizeOnPlay);
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("The game view fills the window while the game plays, and the panels come back at Stop");
    if (ImGui::IsItemDeactivatedAfterEdit() || ImGui::IsItemEdited())
      ImGui::MarkIniSettingsDirty();
  }

  if (ImGui::CollapsingHeader("Collision layers")) {
    CollisionLayers &layers = engine.layers();
    ImGui::TextDisabled("Checked pairs collide. An entity picks its layer in its own panel.");
    std::string removed;
    if (ImGui::BeginTable("##layerMatrix", layers.count() + 1,
                          ImGuiTableFlags_ScrollX | ImGuiTableFlags_Borders | ImGuiTableFlags_SizingFixedFit |
                              ImGuiTableFlags_RowBg)) {
      ImGui::TableSetupScrollFreeze(1, 0);
      ImGui::TableSetupColumn("");
      for (int j = 0; j < layers.count(); ++j)
        ImGui::TableSetupColumn(layers.name(j).c_str());
      ImGui::TableHeadersRow();
      for (int i = 0; i < layers.count(); ++i) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::PushID(i);
        ImGui::TextUnformatted(layers.name(i).c_str());
        if (i >= CollisionLayers::kBuiltInLayers) {
          ImGui::SameLine();
          if (ImGui::SmallButton("x") )
            removed = layers.name(i);
          if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Remove this layer; what is on it goes back to \"default\"");
        }
        // The matrix is symmetric: one checkbox per pair, on and below the diagonal
        for (int j = 0; j <= i; ++j) {
          ImGui::TableSetColumnIndex(j + 1);
          ImGui::PushID(j);
          bool collide = layers.collide(i, j);
          if (ImGui::Checkbox("##pair", &collide))
            layers.setCollide(layers.name(i), layers.name(j), collide);
          ImGui::PopID();
        }
        ImGui::PopID();
      }
      ImGui::EndTable();
    }
    if (!removed.empty())
      layers.remove(removed);
    static char newLayer[32] = "";
    ImGui::SetNextItemWidth(140.0f);
    const bool enter = ImGui::InputText("##newLayer", newLayer, sizeof(newLayer), ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    if ((ImGui::Button("Add layer") || enter) && newLayer[0] != '\0') {
      layers.add(newLayer);
      newLayer[0] = '\0';
    }
    // Lua projects keep their layers in project.json
    const std::string projectPath = EngineConfig::resolvePath("../project.json");
    if (std::filesystem::exists(projectPath)) {
      if (ImGui::Button("Save to project.json") && !layers.saveToProjectFile(projectPath))
        LOG_W("[Editor] Could not write the layers to %s", projectPath.c_str());
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Layers are not part of the scene: they load with the project");
    } else {
      ImGui::TextDisabled("Not saved: a C++ game sets its layers in code (engine.layers())");
    }
  }

  if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen)) {
    toggleCheckbox("Free camera", Toggle::FreeCamera,
                   "Fly the camera yourself in any mode, instead of the game or its CameraRig: hold the right mouse\n"
                   "button in the view and use W A S D, or lock the cursor. Off by default; a game drives its camera.\n"
                   "While frozen (editing or paused), the right button flies the camera without this.");
    if (engine.isFrozen())
      ImGui::TextDisabled("  Frozen: right-drag in the view to fly");
    else if (!state.isToggled(Toggle::FreeCamera))
      ImGui::TextDisabled("  The game drives the camera");
    else if (!state.isToggled(Toggle::CursorLock))
      ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "  Right-drag in the view to fly (or lock: %s)",
                         toggleKeyName(Toggle::CursorLock) ? toggleKeyName(Toggle::CursorLock) : "Cursor lock");
    // Only a setting of the free camera: greyed out while it is off, so a ticked box does not read as flying being on
    ImGui::BeginDisabled(!state.isToggled(Toggle::FreeCamera));
    ImGui::Indent();
    toggleCheckbox("Up / down", Toggle::FreeCameraVertical,
                   "The free camera also moves up and down: Space / Shift with the cursor locked, E / Q or Space /\n"
                   "Ctrl while right-dragging. Off: it moves on the ground plane, at its height. Key: F4");
    ImGui::Unindent();
    ImGui::EndDisabled();
    toggleCheckbox("Cursor lock", Toggle::CursorLock, "Holds the mouse for looking around (with the free camera on or while editing)");

    Entity cameraEntity = cameraSystem.getActiveCamera();
    if (auto *camera = cameraEntity != -1 ? componentManager.tryGet<Camera>(cameraEntity) : nullptr) {
      ImGui::SliderFloat("FOV", &camera->fov, 20.0f, 120.0f, "%.0f deg");
      ImGui::SliderFloat("Move speed", &camera->moveSpeed, 0.5f, 50.0f, "%.1f", ImGuiSliderFlags_Logarithmic);
      ImGui::SliderFloat("Sensitivity", &camera->mouseSensitivity, 0.1f, 5.0f, "%.2f");
    } else {
      ImGui::TextDisabled("No active camera");
    }
  }

  if (ImGui::CollapsingHeader("Audio")) {
    auto &audio = systemManager.getSystem<AudioSystem>();
    if (!audio.isAvailable()) {
      ImGui::TextDisabled("No audio device");
    } else {
      float master = audio.getMasterVolume();
      if (ImGui::SliderFloat("Master", &master, 0.0f, 1.0f, "%.2f"))
        audio.setMasterVolume(master);
      float effects = audio.getBusVolume(AudioSystem::Bus::Effects);
      if (ImGui::SliderFloat("Effects", &effects, 0.0f, 1.0f, "%.2f"))
        audio.setBusVolume(AudioSystem::Bus::Effects, effects);
      float music = audio.getBusVolume(AudioSystem::Bus::Music);
      if (ImGui::SliderFloat("Music", &music, 0.0f, 1.0f, "%.2f"))
        audio.setBusVolume(AudioSystem::Bus::Music, music);
      ImGui::Text("Voices: %zu", audio.getVoiceCount());
    }
  }

  if (ImGui::CollapsingHeader("Capture")) {
    if (ImGui::Button("Take screenshot"))
      engine.captureScreenshot(EngineConfig::resolvePath(EngineConfig::SCREENSHOT_PATH));
    ImGui::SameLine();
    ImGui::TextDisabled("[%s]", SDL_GetScancodeName(EngineConfig::SCREENSHOT_KEY));
    ImGui::TextDisabled("Saved to bin/%s", EngineConfig::SCREENSHOT_PATH);
  }

  ImGui::Separator();
  toggleCheckbox("Show debug UI", Toggle::ShowUI, "Hides these panels");

  ImGui::PopItemWidth();
  ImGui::End();
}

void EditorUISystem::render(Engine &engine, EntityManager &entityManager, SystemManager &systemManager,
                            ComponentManager &componentManager) {
  // Script errors show on screen, editor open or not, until the script loads and runs cleanly again
  const std::string &scriptError = engine.scripts().getLastError();
  if (!scriptError.empty()) {
    ImGui::SetNextWindowPos(ImVec2(m_viewRect.x + m_viewRect.z * 0.5f, m_viewRect.y + m_viewRect.w - 12.0f),
                            ImGuiCond_Always, ImVec2(0.5f, 1.0f));
    ImGui::SetNextWindowBgAlpha(0.9f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.45f, 0.08f, 0.08f, 1.0f));
    ImGui::Begin("##ScriptError", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                     ImGuiWindowFlags_NoInputs);
    ImGui::TextUnformatted("Script error");
    ImGui::PushTextWrapPos(m_viewRect.z * 0.7f);
    ImGui::TextUnformatted(scriptError.c_str());
    ImGui::PopTextWrapPos();
    ImGui::End();
    ImGui::PopStyleColor();
  }

  const bool panels = m_renderUI && !m_panelsHidden;
  if (m_showStats)
    renderStatsOverlay();

  if (!m_renderUI) {
    engine.endCheckpoint(); // hiding the panels (F2) mid-edit ends the edit
    return;
  }

  // Tab and Shift+Space maximize the game view (hide the panels) and bring them back (not while typing)
  const ImGuiIO &io = ImGui::GetIO();
  const bool quiet = !io.WantTextInput && !io.KeyCtrl && !io.KeyAlt && !systemManager.getSystem<CameraSystem>().isNavigating();
  if (quiet && ((ImGui::IsKeyPressed(ImGuiKey_Tab, false) && !io.KeyShift) ||
                (ImGui::IsKeyPressed(ImGuiKey_Space, false) && io.KeyShift)))
    toggleMaximized();
  followPlayMode(engine.isTesting());
  const bool playing = !engine.isFrozen() && !engine.time().isPaused();
  m_altPicking = playing && io.KeyAlt;
  m_viewForEditor = !playing || io.KeyAlt;

  handleSceneShortcuts(engine, systemManager);
  handleHistoryShortcuts(engine, systemManager);
  handleEditShortcuts(engine, systemManager, componentManager);
  beginEditTracking(engine);
  if (panels)
    renderSettingsPanel(engine, entityManager, systemManager, componentManager);
  if (m_showNavigation)
    drawNavigation(engine, systemManager, componentManager);
  if (panels)
    renderSceneExplorer(engine, systemManager, componentManager);
  if (panels)
    renderAssetPanel(engine, systemManager, componentManager);
  renderPlayToolbar(engine, systemManager);
  m_gizmoShown = false;
  updateBodyGrab(engine);
  if (panels)
    renderPropertiesPanel(engine, systemManager, componentManager); // always there, so its dock slot is not empty
  if (selectedEntity != -1) {
    drawSelectionBoxes(systemManager, componentManager);
    renderGizmo(systemManager, componentManager);
  }
  renderViewportHint(systemManager);
  handleAssetDrop(engine, systemManager, componentManager);
  renderSceneLoadDialog(engine);
  renderSaveAsDialog(engine);
  handleClickSelection(engine, systemManager, componentManager);
  endEditTracking(engine);
}

void EditorUISystem::toggleMaximized() {
  m_panelsHidden = !m_panelsHidden;
  m_maximizedByPlay = false; // the user's own choice from here on
}

// Maximize on Play: Play hides the panels, Stop brings them back, unless the user changed that meanwhile
void EditorUISystem::followPlayMode(bool playing) {
  if (m_wasPlaying != playing) {
    if (playing && m_maximizeOnPlay && !m_panelsHidden) {
      m_panelsHidden = true;
      m_maximizedByPlay = true;
    } else if (!playing && m_maximizedByPlay) {
      m_panelsHidden = false;
      m_maximizedByPlay = false;
    }
  }
  m_wasPlaying = playing;
}

void EditorUISystem::handleHistoryShortcuts(Engine &engine, SystemManager &systemManager) {
  // Not while a text field has the keyboard: it undoes its own typing (and InputSystem keeps these keys from the game).
  // Nor while flying the camera: Ctrl is a key for going down there.
  const ImGuiIO &io = ImGui::GetIO();
  if (!io.KeyCtrl || io.KeyAlt || io.WantTextInput || systemManager.getSystem<CameraSystem>().isNavigating())
    return;
  if (ImGui::IsKeyPressed(ImGuiKey_Z))
    io.KeyShift ? engine.redo() : engine.undo();
  else if (ImGui::IsKeyPressed(ImGuiKey_Y))
    engine.redo();
}

void EditorUISystem::handleEditShortcuts(Engine &engine, SystemManager &systemManager,
                                         ComponentManager &componentManager) {
  const ImGuiIO &io = ImGui::GetIO();
  if (m_deleteRequested) {
    m_deleteRequested = false;
    deleteSelected(engine, systemManager, componentManager);
  }
  if (io.WantTextInput || io.KeyAlt || systemManager.getSystem<CameraSystem>().isNavigating())
    return;
  if (io.KeyCtrl && !io.KeyShift) {
    if (ImGui::IsKeyPressed(ImGuiKey_D, false))
      duplicateSelected(engine, systemManager, componentManager);
    else if (ImGui::IsKeyPressed(ImGuiKey_C, false))
      copySelected(systemManager, componentManager);
    else if (ImGui::IsKeyPressed(ImGuiKey_V, false))
      pasteClipboard(engine, systemManager, componentManager);
  } else if (!io.KeyCtrl && (ImGui::IsKeyPressed(ImGuiKey_Delete, false) || ImGui::IsKeyPressed(ImGuiKey_Backspace, false))) {
    deleteSelected(engine, systemManager, componentManager);
  }
}

bool EditorUISystem::isSelected(Entity entity) const {
  return std::find(m_selection.begin(), m_selection.end(), entity) != m_selection.end();
}

void EditorUISystem::setSelection(std::vector<Entity> entities) {
  m_selection.clear();
  for (Entity entity : entities)
    if (entity != -1 && !isSelected(entity))
      m_selection.push_back(entity);
  selectedEntity = m_selection.empty() ? -1 : m_selection.back();
}

void EditorUISystem::selectOnly(Entity entity) {
  setSelection(entity == -1 ? std::vector<Entity>{} : std::vector<Entity>{entity});
  m_selectionAnchor = entity;
}

void EditorUISystem::toggleSelected(Entity entity) {
  std::vector<Entity> entities = m_selection;
  auto it = std::find(entities.begin(), entities.end(), entity);
  if (it != entities.end())
    entities.erase(it);
  else
    entities.push_back(entity); // the one just added is the primary
  setSelection(std::move(entities));
  m_selectionAnchor = entity;
}

void EditorUISystem::pruneSelection(const EntityManager &entityManager) {
  m_selection.erase(std::remove_if(m_selection.begin(), m_selection.end(),
                                   [&](Entity entity) { return !entityManager.isAlive(entity); }),
                    m_selection.end());
  selectedEntity = m_selection.empty() ? -1 : m_selection.back();
  if (m_selectionAnchor != -1 && !entityManager.isAlive(m_selectionAnchor))
    m_selectionAnchor = -1;
}

std::vector<Entity> EditorUISystem::selectionRoots(ComponentManager &componentManager) const {
  std::vector<Entity> roots;
  for (Entity entity : m_selection) {
    bool covered = false;
    Entity at = entity;
    for (int guard = 0; guard < 256 && !covered; ++guard) {
      const Parent *parent = componentManager.tryGet<Parent>(at);
      if (!parent)
        break;
      at = parent->entity;
      covered = isSelected(at);
    }
    if (!covered)
      roots.push_back(entity);
  }
  return roots;
}

void EditorUISystem::clickInList(Entity entity, const std::vector<Entity> &order, bool toggleOff) {
  const ImGuiIO &io = ImGui::GetIO();
  if (io.KeyCtrl) {
    toggleSelected(entity);
    return;
  }
  if (io.KeyShift && m_selectionAnchor != -1) {
    const auto from = std::find(order.begin(), order.end(), m_selectionAnchor);
    const auto to = std::find(order.begin(), order.end(), entity);
    if (from != order.end() && to != order.end()) {
      std::vector<Entity> range = from <= to ? std::vector<Entity>(from, to + 1)
                                             : std::vector<Entity>(std::make_reverse_iterator(from + 1),
                                                                   std::make_reverse_iterator(to));
      // the one clicked is the primary, the last
      range.erase(std::remove(range.begin(), range.end(), entity), range.end());
      range.push_back(entity);
      setSelection(std::move(range)); // the anchor stays, so the range can be changed
      return;
    }
  }
  if (toggleOff && m_selection.size() == 1 && m_selection.front() == entity)
    selectOnly(-1);
  else
    selectOnly(entity);
}

// The scene is not edited while testing: the snapshot taken at Play brings it back on Stop, and no undo step is kept
void EditorUISystem::duplicateSelected(Engine &engine, SystemManager &systemManager,
                                       ComponentManager &componentManager) {
  if (engine.isTesting())
    return;
  const std::vector<Entity> roots = selectionRoots(componentManager);
  if (roots.empty())
    return;
  auto &serializer = systemManager.getSystem<SceneSerializer>();
  auto &transformSystem = systemManager.getSystem<TransformSystem>();
  const std::string text = serializer.copyToJson(roots);
  if (text.empty())
    return;
  std::vector<Entity> parents;
  for (Entity root : roots)
    parents.push_back(transformSystem.getParent(componentManager, root));
  engine.beginCheckpoint();
  const std::vector<Entity> created = serializer.pasteFromJson(text);
  // The copies of the roots are the created entities with no parent, in the order of the roots
  std::vector<Entity> copies;
  for (Entity entity : created)
    if (!componentManager.has<Parent>(entity))
      copies.push_back(entity);
  for (size_t i = 0; i < copies.size() && i < roots.size(); ++i) {
    if (parents[i] != -1)
      transformSystem.setParent(componentManager, copies[i], parents[i]);
    if (Name *name = componentManager.tryGet<Name>(copies[i]))
      name->name += " (1)";
  }
  if (!copies.empty()) {
    setSelection(copies);
    m_scrollToSelection = true;
  }
  engine.endCheckpoint();
}

void EditorUISystem::copySelected(SystemManager &systemManager, ComponentManager &componentManager) {
  const std::vector<Entity> roots = selectionRoots(componentManager);
  if (roots.empty())
    return;
  const std::string text = systemManager.getSystem<SceneSerializer>().copyToJson(roots);
  if (!text.empty() && !SDL_SetClipboardText(text.c_str()))
    LOG_W("[Editor] Could not copy to the clipboard: %s", SDL_GetError());
}

void EditorUISystem::pasteClipboard(Engine &engine, SystemManager &systemManager, ComponentManager &componentManager) {
  if (engine.isTesting() || !SDL_HasClipboardText())
    return;
  char *clipboard = SDL_GetClipboardText();
  if (!clipboard)
    return;
  const std::string text = clipboard;
  SDL_free(clipboard);
  // Anything else on the clipboard is not for the editor
  if (text.find("modular-engine-scene") == std::string::npos)
    return;
  engine.beginCheckpoint();
  const std::vector<Entity> created = systemManager.getSystem<SceneSerializer>().pasteFromJson(text);
  if (created.empty()) {
    LOG_W("[Editor] The clipboard does not hold entities to paste");
  } else {
    std::vector<Entity> pasted;
    for (Entity entity : created)
      if (!componentManager.has<Parent>(entity))
        pasted.push_back(entity);
    setSelection(pasted);
    m_scrollToSelection = true;
  }
  engine.endCheckpoint();
}

void EditorUISystem::deleteSelected(Engine &engine, SystemManager &systemManager, ComponentManager &componentManager) {
  if (m_selection.empty() || engine.isTesting())
    return;
  const std::vector<Entity> roots = selectionRoots(componentManager); // their descendants go with them
  engine.beginCheckpoint();
  for (Entity root : roots)
    systemManager.getSystem<SceneSystem>().destroyEntity(root);
  selectOnly(-1);
  engine.endCheckpoint();
}

void EditorUISystem::entityContextMenu(Engine &engine, SystemManager &systemManager,
                                       ComponentManager &componentManager, Entity entity) {
  if (!ImGui::BeginPopupContextItem())
    return;
  if (!isSelected(entity)) // right-clicking one of several keeps them all
    selectOnly(entity);
  if (ImGui::MenuItem("Duplicate", "Ctrl+D"))
    duplicateSelected(engine, systemManager, componentManager);
  if (ImGui::MenuItem("Copy", "Ctrl+C"))
    copySelected(systemManager, componentManager);
  if (ImGui::MenuItem("Paste", "Ctrl+V"))
    pasteClipboard(engine, systemManager, componentManager);
  ImGui::Separator();
  if (ImGui::MenuItem("Delete", "Del"))
    m_deleteRequested = true;
  ImGui::EndPopup();
}

// An edit is whatever happens between a click on the editor (a panel, the gizmo or a resize handle) and the moment
// the mouse is up and no widget is active. The scene is copied when the click lands, before the widget acts on it
// (a slider moves on the very click), and the copy is kept as an undo step only if the scene differs when the edit
// ends. A click on empty space (selecting) starts nothing.
void EditorUISystem::beginEditTracking(Engine &engine) {
  const ImGuiIO &io = ImGui::GetIO();
  const bool onEditor = mouseOnPanels() || (m_gizmoShown && ImGuizmo::IsOver()) || m_resizeHovered;
  if (!ImGui::IsMouseClicked(ImGuiMouseButton_Left) || !onEditor)
    return;
  // A click while an edit is still open (a text field keeps it open until something else is clicked) closes it first,
  // so what the click does is a step of its own
  engine.endCheckpoint();
  engine.beginCheckpoint();
}

void EditorUISystem::endEditTracking(Engine &engine) {
  const bool busy = ImGui::IsMouseDown(ImGuiMouseButton_Left) || ImGui::IsAnyItemActive() || ImGuizmo::IsUsing();
  if (busy || m_resize.axis >= 0)
    return;
  engine.endCheckpoint();
}

void EditorUISystem::handleClickSelection(Engine &engine, SystemManager &systemManager,
                                          ComponentManager &componentManager) {
  const ImGuiIO &io = ImGui::GetIO();
  if (!m_viewForEditor) { // the game is playing: its clicks are its own
    m_marquee.active = false;
    return;
  }
  updateMarquee(engine, systemManager, componentManager);
  if (!ImGui::IsMouseClicked(ImGuiMouseButton_Left) || mouseOnPanels() ||
      systemManager.getSystem<StateSystem>().isToggled(Toggle::CursorLock))
    return;
  if (m_gizmoShown && (ImGuizmo::IsOver() || ImGuizmo::IsUsing()))
    return; // the click grabs the gizmo
  if (m_resizeHovered || m_resize.axis >= 0)
    return; // the click grabs a resize handle
  if (const Rml::Context *gameUI = engine.getUIContext(); gameUI && gameUI->IsMouseInteracting())
    return; // a button of the game's UI (a pause menu over the scene) is not a pick of what is behind it
  const glm::vec2 mouse(io.MousePos.x, io.MousePos.y);
  const auto hit = engine.pickModel(viewMouse());
  if (hit) {
    m_marquee.active = false;
    // With Move, a press on a model may be the start of a drag by its body (see updateBodyGrab). Pressing
    // one of several selected models keeps the selection until the button is up without a drag, so the drag moves all.
    const bool grabbing = m_gizmoOperation == GizmoOperation::Move;
    Entity collapseTo = -1;
    if (io.KeyCtrl)
      toggleSelected(hit->entity); // Ctrl/Cmd adds or removes
    else if (grabbing && isSelected(hit->entity) && m_selection.size() > 1)
      collapseTo = hit->entity;
    else
      selectOnly(hit->entity);
    m_scrollToSelection = isSelected(hit->entity);
    if (grabbing && isSelected(hit->entity))
      m_bodyGrab = {true, false, false, collapseTo, mouse, hit->point, glm::vec3(0.0f)};
  } else {
    // Empty space: clears (unless adding), and may start a box
    if (!io.KeyCtrl)
      selectOnly(-1);
    m_marquee = {true, false, mouse};
  }
}

// Dragging by the body (Move): a press on a model followed by a drag of a few pixels grabs the model at the spot
// pressed and moves it like the gizmo's center does (alignToSurface), the grabbed spot under the cursor. Without the
// drag it is the plain click it was (the press already selected). One undo step for the drag.
void EditorUISystem::updateBodyGrab(Engine &engine) {
  if (!m_bodyGrab.pending)
    return;
  const ImGuiIO &io = ImGui::GetIO();
  const bool allowed = m_viewForEditor && m_gizmoOperation == GizmoOperation::Move;
  if (!allowed || !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
    if (allowed && !m_bodyGrab.active && m_bodyGrab.collapseTo != -1 && isSelected(m_bodyGrab.collapseTo))
      selectOnly(m_bodyGrab.collapseTo);
    m_bodyGrab = {};
    return;
  }
  if (!m_bodyGrab.active && glm::distance(glm::vec2(io.MousePos.x, io.MousePos.y), m_bodyGrab.start) > 6.0f) {
    m_bodyGrab.active = m_bodyGrab.fresh = true;
    m_bodyGrab.collapseTo = -1;
    engine.endCheckpoint(); // the scene as it is before the drag is the undo step's start
    engine.beginCheckpoint();
  }
}

// A drag that began on empty space draws a box and, when released, selects the models whose center is inside it
void EditorUISystem::updateMarquee(Engine &engine, SystemManager &systemManager, ComponentManager &componentManager) {
  if (!m_marquee.active)
    return;
  const ImGuiIO &io = ImGui::GetIO();
  const glm::vec2 mouse(io.MousePos.x, io.MousePos.y);
  const glm::vec2 low = glm::min(m_marquee.start, mouse), high = glm::max(m_marquee.start, mouse);
  if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
    if (!m_marquee.dragging && glm::distance(mouse, m_marquee.start) > 6.0f)
      m_marquee.dragging = true;
    if (m_marquee.dragging) {
      ImDrawList *drawList = ImGui::GetForegroundDrawList();
      drawList->AddRectFilled(ImVec2(low.x, low.y), ImVec2(high.x, high.y), IM_COL32(255, 200, 60, 40));
      drawList->AddRect(ImVec2(low.x, low.y), ImVec2(high.x, high.y), IM_COL32(255, 200, 60, 200));
    }
    return;
  }
  const bool dragged = m_marquee.dragging;
  m_marquee.active = m_marquee.dragging = false;
  if (!dragged)
    return;

  auto &cameraSystem = systemManager.getSystem<CameraSystem>();
  const Entity cameraEntity = cameraSystem.getActiveCamera();
  const Camera *camera = cameraEntity != -1 ? componentManager.tryGet<Camera>(cameraEntity) : nullptr;
  if (!camera || m_viewRect.z <= 0.0f || m_viewRect.w <= 0.0f)
    return;
  const glm::mat4 toClip = cameraSystem.getProjectionMatrix(*camera) * cameraSystem.getViewMatrix(*camera);
  auto &resourceSystem = systemManager.getSystem<ResourceSystem>();
  auto &transformSystem = systemManager.getSystem<TransformSystem>();
  std::vector<Entity> inside;
  componentManager.forEachComponent<Model>([&](Entity entity, Model &model) {
    const Transform *transform = componentManager.tryGet<Transform>(entity);
    if (!transform || componentManager.has<Hidden>(entity))
      return;
    const Mesh &mesh = resourceSystem.getMesh(model.meshHandle);
    const glm::vec3 center = (mesh.getBoundsMin() + mesh.getBoundsMax()) * 0.5f;
    const glm::vec4 clip = toClip * transformSystem.calculateModelMatrix(*transform) * glm::vec4(center, 1.0f);
    if (clip.w <= 0.01f)
      return; // behind the camera
    const ImVec2 point = fromClip(clip);
    const glm::vec2 screen(point.x, point.y);
    if (screen.x >= low.x && screen.x <= high.x && screen.y >= low.y && screen.y <= high.y)
      inside.push_back(entity);
  });
  std::sort(inside.begin(), inside.end());
  if (io.KeyCtrl)
    for (Entity entity : m_selection)
      if (std::find(inside.begin(), inside.end(), entity) == inside.end())
        inside.insert(inside.begin(), entity); // what was selected stays
  setSelection(inside);
  m_selectionAnchor = selectedEntity;
}

void EditorUISystem::drawSelectionBoxes(SystemManager &systemManager, ComponentManager &componentManager) {
  for (Entity entity : m_selection) {
    const bool primary = entity == selectedEntity;
    drawBox(systemManager, componentManager, entity, primary ? IM_COL32(255, 200, 60, 230) : IM_COL32(255, 140, 40, 170),
            primary ? 1.8f : 1.2f);
    drawProbeBox(systemManager, componentManager, entity);
    drawJointGizmo(systemManager, componentManager, entity);
    drawLightGizmo(systemManager, componentManager, entity);
  }
}

void EditorUISystem::drawLightGizmo(SystemManager &systemManager, ComponentManager &componentManager, Entity entity) {
  const auto *light = componentManager.tryGet<Light>(entity);
  const auto *transform = componentManager.tryGet<Transform>(entity);
  auto &cameraSystem = systemManager.getSystem<CameraSystem>();
  const Entity cameraEntity = cameraSystem.getActiveCamera();
  const Camera *camera = cameraEntity != -1 ? componentManager.tryGet<Camera>(cameraEntity) : nullptr;
  if (!light || !camera)
    return;
  const glm::mat4 toClip = cameraSystem.getProjectionMatrix(*camera) * cameraSystem.getViewMatrix(*camera);
  ImDrawList *drawList = ImGui::GetBackgroundDrawList();
  auto line = [&](const glm::vec3 &from, const glm::vec3 &to, unsigned color, float thickness) {
    const glm::vec4 a = toClip * glm::vec4(from, 1.0f), b = toClip * glm::vec4(to, 1.0f);
    if (a.w > 0.01f && b.w > 0.01f)
      drawList->AddLine(fromClip(a), fromClip(b), color, thickness);
  };
  const unsigned color = IM_COL32(255, 235, 90, 240);
  const glm::vec3 origin = LightSystem::worldPosition(*light, transform);
  const glm::vec4 clip = toClip * glm::vec4(origin, 1.0f);
  if (clip.w > 0.01f)
    drawList->AddCircleFilled(fromClip(clip), 5.0f, color);
  if (light->type == LightType::Point)
    return;
  glm::vec3 direction = LightSystem::worldDirection(*light, transform);
  if (glm::length(direction) < 1e-6f)
    return;
  direction = glm::normalize(direction);
  const float length = light->type == LightType::Spot ? std::clamp(light->range * 0.25f, 1.5f, 6.0f) : 3.0f;
  const glm::vec3 tip = origin + direction * length;
  line(origin, tip, color, 2.0f);
  // The arrow head
  const glm::vec3 helper = std::abs(direction.y) > 0.9f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
  const glm::vec3 side = glm::normalize(glm::cross(direction, helper));
  const glm::vec3 up = glm::cross(side, direction);
  for (const glm::vec3 &offset : {side, -side, up, -up})
    line(tip, tip - direction * 0.3f + offset * 0.12f, color, 2.0f);
  if (light->type == LightType::Spot) {
    // The cone where the light ends (outerCutOff is its cosine)
    const float outerCos = std::clamp(light->outerCutOff, 0.05f, 0.9999f);
    const float radius = length * std::sqrt(1.0f - outerCos * outerCos) / outerCos;
    glm::vec3 previous = tip + side * radius;
    for (int i = 1; i <= 24; ++i) {
      const float angle = glm::two_pi<float>() * static_cast<float>(i) / 24.0f;
      const glm::vec3 point = tip + (side * std::cos(angle) + up * std::sin(angle)) * radius;
      line(previous, point, IM_COL32(255, 235, 90, 150), 1.2f);
      previous = point;
    }
    for (const glm::vec3 &offset : {side, -side, up, -up})
      line(origin, tip + offset * radius, IM_COL32(255, 235, 90, 150), 1.2f);
  }
}

// A selected reflection probe's influence box (the world-aligned one the lit shader uses) in cyan, and inside it the
// box where the probe is at full strength (the blend distance in from each face) in a dimmer one
void EditorUISystem::drawProbeBox(SystemManager &systemManager, ComponentManager &componentManager, Entity entity) {
  const auto *probe = componentManager.tryGet<ReflectionProbe>(entity);
  const auto *transform = componentManager.tryGet<Transform>(entity);
  auto &cameraSystem = systemManager.getSystem<CameraSystem>();
  const Entity cameraEntity = cameraSystem.getActiveCamera();
  const Camera *camera = cameraEntity != -1 ? componentManager.tryGet<Camera>(cameraEntity) : nullptr;
  if (!probe || !transform || !camera)
    return;
  const glm::mat4 toClip = cameraSystem.getProjectionMatrix(*camera) * cameraSystem.getViewMatrix(*camera);
  ImDrawList *drawList = ImGui::GetBackgroundDrawList();
  auto drawWire = [&](const glm::vec3 &half, unsigned color, float thickness) {
    glm::vec4 corners[8];
    for (int i = 0; i < 8; ++i)
      corners[i] = toClip * glm::vec4(transform->position + glm::vec3(i & 1 ? half.x : -half.x, i & 2 ? half.y : -half.y,
                                                                      i & 4 ? half.z : -half.z),
                                      1.0f);
    for (int a = 0; a < 8; ++a)
      for (int bit = 1; bit < 8; bit <<= 1) {
        const int b = a | bit;
        if (b == a || corners[a].w <= 0.01f || corners[b].w <= 0.01f)
          continue;
        drawList->AddLine(fromClip(corners[a]), fromClip(corners[b]), color, thickness);
      }
  };
  const glm::vec3 half = glm::max(probe->size, glm::vec3(0.0f)) * 0.5f;
  drawWire(half, IM_COL32(80, 220, 255, 230), 1.6f);
  const glm::vec3 inner = glm::max(half - glm::vec3(std::max(probe->blendDistance, 0.0f)), glm::vec3(0.0f));
  if (probe->blendDistance > 0.0f && inner != half)
    drawWire(inner, IM_COL32(80, 220, 255, 110), 1.0f);
}

// The Reflection Probe section of the Properties panel
void EditorUISystem::drawReflectionProbe(Engine &engine, Entity entity, ReflectionProbe &probe) {
  const bool testing = engine.isTesting();
  ImGui::DragFloat3("Size##probe", &probe.size.x, 0.1f, 0.1f, 1000.0f, "%.1f");
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("The influence box, centered on the entity and aligned to the world axes. Things inside it are lit\n"
                      "by this probe instead of the sky: make it reach a little past the walls of the room.");
  ImGui::DragFloat("Blend distance##probe", &probe.blendDistance, 0.05f, 0.0f, 100.0f, "%.2f");
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("How far in from the box's faces the probe fades from nothing to full strength (into the sky or\n"
                      "the next probe)");
  ImGui::DragFloat("Intensity##probe", &probe.intensity, 0.01f, 0.0f, 10.0f, "%.2f");
  ImGui::Checkbox("Box projection##probe", &probe.boxProjection);
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Reflections are corrected for the box, so a wall appears where it is. Right for a room that\n"
                      "fills its box; off for open spaces.");
  int choice = probe.resolution <= 64 ? 0 : (probe.resolution >= 256 ? 2 : 1);
  if (ImGui::Combo("Resolution##probe", &choice, "64\0" "128\0" "256\0"))
    probe.resolution = 64 << choice;
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Size of a face of the capture; the stored reflections are 128 across whatever this is");

  ImGui::Separator();
  const bool stale = engine.isReflectionProbeStale(entity);
  if (probe.bakedPath.empty())
    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.4f, 1.0f), "Not baked: captured when the scene loads");
  else if (stale)
    ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.3f, 1.0f), "Stale: moved or resized since it was baked");
  else
    ImGui::TextColored(ImVec4(0.5f, 0.9f, 0.5f, 1.0f), "Baked");
  if (!probe.bakedPath.empty())
    ImGui::TextDisabled("%s", probe.bakedPath.c_str());
  if (testing)
    ImGui::BeginDisabled();
  if (ImGui::Button("Bake##probe"))
    engine.bakeReflectionProbe(entity);
  if (testing)
    ImGui::EndDisabled();
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    ImGui::SetTooltip("%s", testing ? "Stop the game to bake"
                                    : "Captures the room from here and writes assets/probes/<scene>/<name>.probe; "
                                      "save the scene to keep the path");
  ImGui::SameLine();
  if (ImGui::Button("Capture (no file)##probe"))
    engine.refreshReflectionProbe(entity);
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Captures it again into memory only, to preview; the baked file is not touched");
}

// A selected joint: the anchor as a dot, the axis through it, the limits (an arc around a hinge's axis, a segment along
// a slider's) and a line to the other end (the target's origin, or the distance joint's far point). Where the limits
// are drawn assumes the joint was built where the bodies are now, which is so in the editor (moving a body builds it again).
void EditorUISystem::drawJointGizmo(SystemManager &systemManager, ComponentManager &componentManager, Entity entity) {
  const auto *joint = componentManager.tryGet<Joint>(entity);
  const auto *transform = componentManager.tryGet<Transform>(entity);
  auto &cameraSystem = systemManager.getSystem<CameraSystem>();
  const Entity cameraEntity = cameraSystem.getActiveCamera();
  const Camera *camera = cameraEntity != -1 ? componentManager.tryGet<Camera>(cameraEntity) : nullptr;
  if (!joint || !transform || !camera)
    return;
  auto &transformSystem = systemManager.getSystem<TransformSystem>();
  const glm::mat4 toClip = cameraSystem.getProjectionMatrix(*camera) * cameraSystem.getViewMatrix(*camera);
  ImDrawList *drawList = ImGui::GetBackgroundDrawList();
  auto line = [&](const glm::vec3 &from, const glm::vec3 &to, unsigned color, float thickness) {
    const glm::vec4 a = toClip * glm::vec4(from, 1.0f), b = toClip * glm::vec4(to, 1.0f);
    if (a.w > 0.01f && b.w > 0.01f)
      drawList->AddLine(fromClip(a), fromClip(b), color, thickness);
  };
  const glm::mat4 model = transformSystem.calculateModelMatrix(*transform);
  const glm::vec3 scale = glm::max(glm::abs(transform->scale), glm::vec3(1e-4f));
  const glm::mat3 rotation(glm::vec3(model[0]) / scale.x, glm::vec3(model[1]) / scale.y, glm::vec3(model[2]) / scale.z);
  const glm::vec3 anchor = glm::vec3(model * glm::vec4(joint->anchor, 1.0f));
  glm::vec3 axis = rotation * joint->axis;
  axis = glm::length(axis) > 1e-6f ? glm::normalize(axis) : glm::vec3(0, 1, 0);

  // The dot
  const glm::vec4 clip = toClip * glm::vec4(anchor, 1.0f);
  if (clip.w > 0.01f)
    drawList->AddCircleFilled(fromClip(clip), 5.0f, IM_COL32(255, 160, 40, 240));

  // What it holds on to
  glm::vec3 other = anchor; // the world
  bool hasOther = false;
  if (!joint->target.empty()) {
    Entity found = -1;
    componentManager.forEachComponent<Name>([&](Entity candidate, Name &name) {
      if (name.name == joint->target && (found == -1 || candidate < found) && componentManager.has<Transform>(candidate))
        found = candidate;
    });
    if (found != -1) {
      const Transform &target = componentManager.get<Transform>(found);
      other = joint->type == JointType::Distance
                  ? glm::vec3(transformSystem.calculateModelMatrix(target) * glm::vec4(joint->targetAnchor, 1.0f))
                  : target.position;
      hasOther = true;
    }
  } else if (joint->type == JointType::Distance) {
    other = joint->targetAnchor;
    hasOther = true;
  }
  if (hasOther)
    line(anchor, other, IM_COL32(255, 160, 40, 200), 1.5f);

  if (joint->type != JointType::Hinge && joint->type != JointType::Slider)
    return;
  const float length = 0.8f;
  line(anchor - axis * length, anchor + axis * length, IM_COL32(80, 220, 255, 230), 2.0f);
  if (!joint->limits)
    return;
  if (joint->type == JointType::Slider) {
    line(anchor + axis * std::min(joint->min, 0.0f), anchor + axis * std::max(joint->max, 0.0f),
         IM_COL32(120, 255, 120, 230), 3.0f);
    return;
  }
  // The same normal axis as the physics uses, so the arc starts where the hinge's 0 is
  const glm::vec3 zero = std::fabs(axis.x) > std::fabs(axis.y) ? glm::normalize(glm::vec3(-axis.z, 0.0f, axis.x))
                                                               : glm::normalize(glm::vec3(0.0f, axis.z, -axis.y));
  const float low = glm::radians(std::clamp(joint->min, -180.0f, 0.0f));
  const float high = glm::radians(std::clamp(joint->max, 0.0f, 180.0f));
  const int segments = 24;
  glm::vec3 previous = anchor + glm::angleAxis(low, axis) * zero * length;
  line(anchor, previous, IM_COL32(120, 255, 120, 200), 1.2f);
  for (int i = 1; i <= segments; ++i) {
    const float angle = low + (high - low) * static_cast<float>(i) / segments;
    const glm::vec3 point = anchor + glm::angleAxis(angle, axis) * zero * length;
    line(previous, point, IM_COL32(120, 255, 120, 230), 2.0f);
    previous = point;
  }
  line(anchor, previous, IM_COL32(120, 255, 120, 200), 1.2f);
  line(anchor, anchor + zero * length, IM_COL32(120, 255, 120, 90), 1.0f); // where it was built: 0
}

// The Joint section of the Properties panel
void EditorUISystem::drawJoint(Engine &engine, Entity entity, Joint &joint) {
  auto &componentManager = engine.getComponentManager();
  int type = static_cast<int>(joint.type);
  ImGui::Combo("Type##joint", &type, "Hinge\0Fixed\0Distance\0Slider\0");
  joint.type = static_cast<JointType>(type);
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Hinge: turns about an axis (door, wheel). Slider: moves along an axis (drawer, elevator).\n"
                      "Fixed: welded in the current pose. Distance: keeps two points apart (rod, rope, bungee).");

  // The target: the world, or an entity with a body, picked by name
  const std::string shown = joint.target.empty() ? "World" : joint.target;
  if (ImGui::BeginCombo("Target##joint", shown.c_str())) {
    if (ImGui::Selectable("World", joint.target.empty()))
      joint.target.clear();
    componentManager.forEachComponent<RigidBody>([&](Entity candidate, RigidBody &) {
      const Name *name = componentManager.tryGet<Name>(candidate);
      if (candidate != entity && name && !name->name.empty() &&
          ImGui::Selectable((name->name + "##" + std::to_string(candidate)).c_str(), name->name == joint.target))
        joint.target = name->name;
    });
    ImGui::EndCombo();
  }
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("What it is joined to: the world (a fixed point) or another entity with a Rigid Body, by name.\n"
                      "The joint holds this entity's body, so it is normally a dynamic one.");

  ImGui::DragFloat3("Anchor##joint", &joint.anchor.x, 0.02f, -100.0f, 100.0f, "%.2f");
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("The pivot (or attach point), in this entity's own space: a unit box has its faces at +-0.5.\n"
                      "Scaled with the entity.");
  if (joint.type == JointType::Distance) {
    ImGui::DragFloat3("Target anchor##joint", &joint.targetAnchor.x, 0.02f, -1000.0f, 1000.0f, "%.2f");
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("The other end: in the target's own space, or a point in the world when the target is World");
    ImGui::DragFloat("Min distance##joint", &joint.minDistance, 0.02f, 0.0f, 1000.0f, "%.2f");
    ImGui::DragFloat("Max distance##joint", &joint.maxDistance, 0.02f, 0.0f, 1000.0f, "%.2f");
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Both 0: the distance the points are at when the joint is built. Equal: a rod; min 0: a rope.");
  }
  const bool moves = joint.type == JointType::Hinge || joint.type == JointType::Slider;
  if (moves) {
    ImGui::DragFloat3("Axis##joint", &joint.axis.x, 0.01f, -1.0f, 1.0f, "%.2f");
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("The hinge's turning axis or the slider's direction, in this entity's own space");
    const bool hinge = joint.type == JointType::Hinge;
    ImGui::Checkbox("Limits##joint", &joint.limits);
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Stops at min and max, measured from the pose the joint is built in (when the scene loads or\n"
                        "the joint changes)");
    if (joint.limits) {
      ImGui::DragFloat("Min##joint", &joint.min, hinge ? 1.0f : 0.02f, hinge ? -180.0f : -1000.0f, 0.0f,
                       hinge ? "%.0f deg" : "%.2f");
      ImGui::DragFloat("Max##joint", &joint.max, hinge ? 1.0f : 0.02f, 0.0f, hinge ? 180.0f : 1000.0f,
                       hinge ? "%.0f deg" : "%.2f");
    }
    int motor = static_cast<int>(joint.motor);
    ImGui::Combo("Motor##joint", &motor, "Off\0Velocity\0Position\0");
    joint.motor = static_cast<JointMotor>(motor);
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Velocity: keeps a speed up. Position: goes to a target and holds it like a spring\n"
                        "(a door that swings back shut: Position, target 0, a low frequency).");
    if (joint.motor == JointMotor::Velocity)
      ImGui::DragFloat("Speed##joint", &joint.motorSpeed, hinge ? 1.0f : 0.05f, -10000.0f, 10000.0f,
                       hinge ? "%.1f deg/s" : "%.2f /s");
    if (joint.motor == JointMotor::Position)
      ImGui::DragFloat("Target pose##joint", &joint.motorTarget, hinge ? 1.0f : 0.02f, -10000.0f, 10000.0f,
                       hinge ? "%.1f deg" : "%.2f");
    if (joint.motor != JointMotor::Off) {
      ImGui::DragFloat("Max force##joint", &joint.maxMotorForce, 100.0f, 0.0f, 1.0e9f, hinge ? "%.0f N*m" : "%.0f N");
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("The most the motor pushes. Finite so that a motor blocked by something stalls instead of\n"
                          "forcing its way through (a wall turned into a pillar stops there).");
    }
  }
  if ((moves && joint.motor == JointMotor::Position) || joint.type == JointType::Distance) {
    ImGui::DragFloat("Spring frequency##joint", &joint.springFrequency, 0.05f, 0.0f, 30.0f,
                     joint.springFrequency > 0.0f ? "%.2f Hz" : (moves ? "stiff (10 Hz)" : "hard"));
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip(moves ? "How stiffly the position motor pulls to its target; low values make a soft door that\n"
                                "gives way to pushes"
                              : "0: hard limits. Above 0 the limits are a spring: a rope that stretches, a bungee.");
    ImGui::DragFloat("Spring damping##joint", &joint.springDamping, 0.01f, 0.0f, 2.0f, "%.2f");
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("0 keeps oscillating, 1 settles without overshoot");
  }
  ImGui::Checkbox("Collide connected##joint", &joint.collideConnected);
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Whether the two joined bodies collide with each other (off: a door and its frame ignore each other)");

  ImGui::Separator();
  if (!componentManager.has<RigidBody>(entity)) {
    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.4f, 1.0f), "Needs a Rigid Body on this entity");
  } else if (engine.physics().isJointBuilt(entity)) {
    if (joint.type == JointType::Hinge)
      ImGui::TextDisabled("Angle: %.1f deg", engine.physics().getJointAngle(entity));
    else if (joint.type == JointType::Slider)
      ImGui::TextDisabled("Position: %.2f", engine.physics().getJointPosition(entity));
    else
      ImGui::TextDisabled("Built");
  } else {
    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.4f, 1.0f), "Not built: %s",
                       joint.target.empty() ? "waiting for the bodies" : "the target needs to exist and have a Rigid Body");
  }
}

void EditorUISystem::drawBox(SystemManager &systemManager, ComponentManager &componentManager, Entity entity,
                             unsigned color, float thickness) {
  const auto *model = componentManager.tryGet<Model>(entity);
  const auto *transform = componentManager.tryGet<Transform>(entity);
  auto &cameraSystem = systemManager.getSystem<CameraSystem>();
  const Entity cameraEntity = cameraSystem.getActiveCamera();
  const Camera *camera = cameraEntity != -1 ? componentManager.tryGet<Camera>(cameraEntity) : nullptr;
  if (!model || !transform || !camera)
    return;

  const Mesh &mesh = systemManager.getSystem<ResourceSystem>().getMesh(model->meshHandle);
  const glm::mat4 toClip = cameraSystem.getProjectionMatrix(*camera) * cameraSystem.getViewMatrix(*camera) *
                           systemManager.getSystem<TransformSystem>().calculateModelMatrix(*transform);
  const glm::vec3 low = mesh.getBoundsMin(), high = mesh.getBoundsMax();
  glm::vec4 corners[8];
  for (int i = 0; i < 8; ++i)
    corners[i] = toClip * glm::vec4(i & 1 ? high.x : low.x, i & 2 ? high.y : low.y, i & 4 ? high.z : low.z, 1.0f);

  auto toScreen = [&](const glm::vec4 &clip) { return fromClip(clip); };
  ImDrawList *drawList = ImGui::GetBackgroundDrawList();
  // The 12 edges: corners that differ in one bit
  for (int a = 0; a < 8; ++a)
    for (int bit = 1; bit < 8; bit <<= 1) {
      const int b = a | bit;
      if (b == a || corners[a].w <= 0.01f || corners[b].w <= 0.01f) // skip edges behind the camera
        continue;
      drawList->AddLine(toScreen(corners[a]), toScreen(corners[b]), color, thickness);
    }
}

void EditorUISystem::drawNavigation(Engine &engine, SystemManager &systemManager, ComponentManager &componentManager) {
  const NavigationSystem &navigation = engine.navigation();
  auto &cameraSystem = systemManager.getSystem<CameraSystem>();
  const Entity cameraEntity = cameraSystem.getActiveCamera();
  const Camera *camera = cameraEntity != -1 ? componentManager.tryGet<Camera>(cameraEntity) : nullptr;
  if (!navigation.isBuilt() || !camera)
    return;

  const glm::mat4 toClip = cameraSystem.getProjectionMatrix(*camera) * cameraSystem.getViewMatrix(*camera);
  // False for a point behind the camera
  auto project = [&](const glm::vec3 &point, ImVec2 &screen) {
    const glm::vec4 clip = toClip * glm::vec4(point, 1.0f);
    if (clip.w <= 0.01f)
      return false;
    screen = fromClip(clip);
    return true;
  };

  ImDrawList *drawList = ImGui::GetBackgroundDrawList();
  constexpr float kDrawDistance = 40.0f; // far polygons are small and many: leave them out to keep this cheap
  const glm::vec3 lift(0.0f, 0.03f, 0.0f);
  navigation.forEachPolygon(camera->position, kDrawDistance, [&](const glm::vec3 *corners, int count) {
    ImVec2 screen[6];
    for (int i = 0; i < count; ++i)
      if (!project(corners[i] + lift, screen[i]))
        return;
    drawList->AddConvexPolyFilled(screen, count, IM_COL32(60, 200, 120, 70));
    drawList->AddPolyline(screen, count, IM_COL32(60, 220, 130, 160), ImDrawFlags_Closed, 1.0f);
  });

  const std::vector<glm::vec3> &path = navigation.getLastPath();
  for (size_t i = 0; i < path.size(); ++i) {
    ImVec2 point;
    if (!project(path[i] + glm::vec3(0.0f, 0.08f, 0.0f), point))
      continue;
    ImVec2 previous;
    if (i > 0 && project(path[i - 1] + glm::vec3(0.0f, 0.08f, 0.0f), previous))
      drawList->AddLine(previous, point, IM_COL32(255, 220, 60, 255), 2.5f);
    drawList->AddCircleFilled(point, 4.0f, IM_COL32(255, 120, 40, 255));
  }
}

void EditorUISystem::renderGizmoControls() {
  auto option = [&](const char *label, GizmoOperation operation) {
    if (ImGui::RadioButton(label, m_gizmoOperation == operation))
      m_gizmoOperation = operation;
  };
  option("Move##gizmo", GizmoOperation::Move);
  ImGui::SameLine();
  option("Rotate##gizmo", GizmoOperation::Rotate);
  ImGui::SameLine();
  option("Scale##gizmo", GizmoOperation::Scale);
  ImGui::SameLine();
  option("Resize##gizmo", GizmoOperation::Resize);
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("A handle on each face: dragging one moves that face alone, the opposite face stays put");
  ImGui::SameLine();
  ImGui::TextDisabled("[WERT]");
  ImGui::Checkbox("Local axes##gizmo", &m_gizmoLocal);
  ImGui::SameLine();
  ImGui::Checkbox("Snap##gizmo", &m_gizmoSnap);
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("0.5 units, 15 degrees, 0.1 scale; holding Ctrl snaps too");
  ImGui::SameLine();
  ImGui::Checkbox("Solid##gizmo", &m_gizmoSolid);
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Moving stops where it touches another model (by their triangles) and slides along it;\n"
                      "a rotation or scale that would cut into one is not applied. It can still leave or slide\n"
                      "along a model it already cuts into, but not sink deeper.");
  ImGui::Checkbox("Align to surface##gizmo", &m_gizmoAlign);
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Dragging the object itself (by the spot you grab, with Move) slides it over the surfaces, the\n"
                      "grabbed spot under the cursor: grab a tall object near its base to keep it on the floor by a wall.\n"
                      "Off, dragging the object moves it like the gizmo's center: on the screen's plane, keeping its\n"
                      "rotation.\n"
                      "Dragging the move gizmo's center (the white circle) slides the entity over the surfaces under the\n"
                      "cursor: its bottom on the one you point at, the circle on the cursor (held back at the surface's\n"
                      "edge; on a small top it follows the spot you point at). The arrows and squares still move it\n"
                      "along their axis or plane. With Snap on, its place goes to the 0.5 grid of the surface's model\n"
                      "and its heading to 15 degrees; the rotate gizmo then turns it around the surface's normal.\n"
                      "While dragging: R turns it 90 degrees around the surface's normal; T (without Tilt) tips it 90\n"
                      "degrees towards the camera's side.");
  ImGui::SameLine();
  // Only means something with Align on: greyed out (its choice kept) while Align is off
  ImGui::BeginDisabled(!m_gizmoAlign);
  ImGui::Checkbox("Tilt to surface##gizmo", &m_gizmoAlignTilt);
  ImGui::EndDisabled();
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    ImGui::SetTooltip("Needs Align to surface on. For dragging the object and the gizmo's center over surfaces.\n"
                      "On: its up axis follows the surface's slope, keeping its heading. Off: it keeps its rotation (a box\n"
                      "on its side stays on its side) and rests on whichever part of it points down.");
}

namespace {
constexpr float kSnapStep = 0.5f, kSnapAngle = 15.0f; // the Snap option: units and degrees

// TransformSystem rotates by Y, then X, then Z (qy * qx * qz), which eulerAngleYXZ builds and extractEulerAngleYXZ
// undoes
glm::quat rotationOf(const Transform &transform) {
  const glm::vec3 radians = glm::radians(transform.rotation);
  return glm::quat_cast(glm::eulerAngleYXZ(radians.y, radians.x, radians.z));
}
// The Transform's angles for a rotation: TransformSystem's, steady at +-90 degrees of pitch and without round-off
glm::vec3 eulerOf(const glm::quat &rotation) { return TransformSystem::eulerDegrees(rotation); }
// The turn around +Y left in a rotation once its tilt (the shortest turn taking +Y to where its up went) is taken
// out: the heading it keeps on a slope, in degrees. The twist of a swing-twist split.
float headingAround(const glm::quat &rotation) { return glm::degrees(2.0f * std::atan2(rotation.y, rotation.w)); }
} // namespace

void EditorUISystem::renderGizmo(SystemManager &systemManager, ComponentManager &componentManager) {
  // The gizmo sits on the primary; when an ancestor of it is selected too, on the topmost such ancestor, which
  // carries the primary along
  m_gizmoEntity = selectedEntity;
  Entity at = selectedEntity;
  for (int guard = 0; guard < 256; ++guard) {
    const Parent *parent = componentManager.tryGet<Parent>(at);
    if (!parent)
      break;
    at = parent->entity;
    if (isSelected(at))
      m_gizmoEntity = at;
  }
  const Transform *transform = componentManager.tryGet<Transform>(m_gizmoEntity);
  const Transform before = transform ? *transform : Transform();
  renderGizmoOn(systemManager, componentManager);
  // What it did to that entity is done to the others, except resizing a face: that is for one entity
  transform = componentManager.tryGet<Transform>(m_gizmoEntity);
  if (m_selection.size() > 1 && transform && *transform != before && m_gizmoOperation != GizmoOperation::Resize)
    followGizmo(systemManager, componentManager, before, *transform);
}

// The roots of the selection other than the gizmo's entity move, turn and scale as it did, around its place: the
// whole selection acts as one body with the pivot at the primary's (the gizmo)
void EditorUISystem::followGizmo(SystemManager &systemManager, ComponentManager &componentManager,
                                 const Transform &before, const Transform &after) {
  const glm::quat turnBefore = rotationOf(before);
  const bool turned = after.rotation != before.rotation, scaled = after.scale != before.scale;
  const glm::quat turn = turned ? rotationOf(after) * glm::inverse(turnBefore) : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
  const glm::vec3 ratio = after.scale / glm::max(before.scale, glm::vec3(1e-6f));
  for (Entity entity : selectionRoots(componentManager)) {
    Transform *transform = componentManager.tryGet<Transform>(entity);
    if (entity == m_gizmoEntity || !transform || componentManager.has<Camera>(entity))
      continue;
    glm::vec3 offset = transform->position - before.position;
    if (scaled) // along the gizmo entity's own axes, where scaling happens
      offset = turnBefore * (ratio * (glm::inverse(turnBefore) * offset));
    if (turned) {
      offset = turn * offset;
      transform->rotation = eulerOf(turn * rotationOf(*transform));
    }
    if (scaled)
      transform->scale *= ratio;
    transform->position = after.position + offset;
  }
  (void)systemManager;
}

void EditorUISystem::renderGizmoOn(SystemManager &systemManager, ComponentManager &componentManager) {
  // Cameras place themselves from their Camera component, not the Transform the gizmo would move
  auto *transform = componentManager.tryGet<Transform>(m_gizmoEntity);
  if (!transform || componentManager.has<Camera>(m_gizmoEntity))
    return;
  auto &cameraSystem = systemManager.getSystem<CameraSystem>();
  const Entity cameraEntity = cameraSystem.getActiveCamera();
  const Camera *camera = cameraEntity != -1 ? componentManager.tryGet<Camera>(cameraEntity) : nullptr;
  if (!camera)
    return;

  ImGuiIO &io = ImGui::GetIO();
  // Shortcuts, unless a text field has the keyboard, or the camera is being flown: W is forward there, not Move
  if (!io.WantTextInput && !ImGuizmo::IsUsing() && m_resize.axis < 0 &&
      !systemManager.getSystem<CameraSystem>().isNavigating()) {
    if (ImGui::IsKeyPressed(ImGuiKey_W, false))
      m_gizmoOperation = GizmoOperation::Move;
    if (ImGui::IsKeyPressed(ImGuiKey_E, false))
      m_gizmoOperation = GizmoOperation::Rotate;
    if (ImGui::IsKeyPressed(ImGuiKey_R, false))
      m_gizmoOperation = GizmoOperation::Scale;
    if (ImGui::IsKeyPressed(ImGuiKey_T, false))
      m_gizmoOperation = GizmoOperation::Resize;
  }

  const glm::mat4 view = cameraSystem.getViewMatrix(*camera);
  const glm::mat4 projection = cameraSystem.getProjectionMatrix(*camera);
  if (m_gizmoOperation == GizmoOperation::Resize) {
    if (m_viewForEditor) {
      renderResizeHandles(systemManager, componentManager, *transform, view, projection);
    } else { // nothing grabbed while the game has the clicks
      m_resizeHovered = false;
      m_resize.axis = -1;
    }
    return;
  }
  m_resize.axis = -1;
  m_resizeHovered = false;
  glm::mat4 model = systemManager.getSystem<TransformSystem>().calculateModelMatrix(*transform);

  const ImGuizmo::OPERATION operation = m_gizmoOperation == GizmoOperation::Move     ? ImGuizmo::TRANSLATE
                                        : m_gizmoOperation == GizmoOperation::Rotate ? ImGuizmo::ROTATE
                                                                                     : ImGuizmo::SCALE;
  // Scaling is always along the entity's own axes; so is rotating while aligning to surfaces, where its up axis is
  // the surface's normal and the ring around it turns the heading
  const ImGuizmo::MODE mode = m_gizmoLocal || operation == ImGuizmo::SCALE || (operation == ImGuizmo::ROTATE && m_gizmoAlign)
                                  ? ImGuizmo::LOCAL
                                  : ImGuizmo::WORLD;
  const float snapStep = operation == ImGuizmo::TRANSLATE ? kSnapStep : operation == ImGuizmo::ROTATE ? kSnapAngle : 0.1f;
  const float snap[3] = {snapStep, snapStep, snapStep};

  // Drawn over the game view, behind the editor's windows
  ImGuizmo::SetOrthographic(false);
  ImGuizmo::SetDrawlist(ImGui::GetBackgroundDrawList());
  // It takes the mouse only over its draw list's window or this one: the Game panel is what lies under the cursor
  ImGuizmo::SetAlternativeWindow(ImGui::FindWindowByName(kGamePanel));
  ImGuizmo::SetRect(m_viewRect.x, m_viewRect.y, m_viewRect.z, m_viewRect.w);
  m_gizmoShown = true;
  // The heading kept while aligning to surfaces is the one the entity had when the drag started
  const bool bodyGrab = m_bodyGrab.active && operation == ImGuizmo::TRANSLATE;
  if (!ImGuizmo::IsUsing() && (!bodyGrab || m_bodyGrab.fresh)) {
    m_alignHeading = headingAround(rotationOf(*transform));
    m_alignSurface = -1;
    m_alignRotation = rotationOf(*transform);
    m_alignTurned = false;
    m_dragStartRotation = rotationOf(*transform);
    m_dragStartAngles = transform->rotation;
    m_dragRotation = m_dragStartRotation;
  }
  // The rotate gizmo turns the matrix it is given a little every frame. Given the snapped entity, it would be turned
  // back to the step it was on every frame and never get to the next one: it is given the unsnapped rotation of the
  // drag instead, and only what is applied to the entity is snapped.
  if (operation == ImGuizmo::ROTATE)
    model = glm::translate(glm::mat4(1.0f), transform->position) * glm::mat4_cast(m_dragRotation) *
            glm::scale(glm::mat4(1.0f), transform->scale);
  const bool snapping = m_gizmoSnap || io.KeyCtrl;
  if (ImGuizmo::IsUsing())
    cameraSystem.editorTookCamera();
  // Rotations are snapped here, not by the gizmo (which only steps from where the drag started): see below
  ImGuizmo::Enable(m_viewForEditor); // shown while the game plays, but its clicks are the game's (Alt: the gizmo's)
  const bool gizmoMoved =
      ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(projection), operation, mode, glm::value_ptr(model),
                           nullptr, snapping && operation != ImGuizmo::ROTATE ? snap : nullptr);
  // Align to surface: dragging the gizmo's center (its free move) follows the cursor over surfaces (off any surface,
  // the gizmo moves it as usual). Only the center: an arrow or a plane square is a request to keep to that axis or
  // plane, and following the surface there would carry the entity somewhere the handle does not point (the cursor's
  // ray passes through the entity, onto whatever is behind it). Every frame of the drag, not only those the gizmo reports a change: a snapped gizmo
  // moves in steps, and the entity would stand still between them while the cursor slid along the surface.
  // The same by the body: the grabbed spot (not the center) stays under the cursor; off any surface, it moves on the
  // plane facing the camera
  if (bodyGrab && !ImGuizmo::IsUsing()) {
    if (m_bodyGrab.fresh) {
      m_bodyGrab.local = glm::inverse(rotationOf(*transform)) * (m_bodyGrab.point - transform->position);
      m_bodyGrab.fresh = false;
    }
    cameraSystem.editorTookCamera();
    // Align on: it rests on the surfaces under the cursor. Off: it moves like the gizmo's center does, the grabbed spot
    // on the plane through it facing the camera, keeping its rotation, stopped by other models when Solid is on
    if (!m_gizmoAlign) {
      Transform wanted = *transform;
      moveGrabFreely(systemManager, componentManager, wanted, m_bodyGrab.point, m_bodyGrab.local, false, snapping);
      applyGizmoChange(systemManager, componentManager, *transform, wanted, true);
    } else if (!alignToSurface(systemManager, componentManager, *transform, snapping, m_bodyGrab.local)) {
      moveGrabFreely(systemManager, componentManager, *transform, m_bodyGrab.point, m_bodyGrab.local);
    }
    m_bodyGrab.point = transform->position + rotationOf(*transform) * m_bodyGrab.local;
    return;
  }
  if (operation == ImGuizmo::TRANSLATE && m_gizmoAlign && ImGuizmo::IsUsing() &&
      ImGuizmo::GetActiveHandleType() == ImGuizmo::MT_MOVE_SCREEN &&
      alignToSurface(systemManager, componentManager, *transform, snapping))
    return;
  if (!gizmoMoved)
    return;

  // Back to position, rotation and scale. TransformSystem rotates by Y, then X, then Z (qy * qx * qz), which is the
  // order extractEulerAngleYXZ undoes.
  const glm::vec3 scale(glm::length(glm::vec3(model[0])), glm::length(glm::vec3(model[1])),
                        glm::length(glm::vec3(model[2])));
  if (scale.x <= 0.0f || scale.y <= 0.0f || scale.z <= 0.0f)
    return;
  glm::mat4 rotation(1.0f);
  rotation[0] = glm::vec4(glm::vec3(model[0]) / scale.x, 0.0f);
  rotation[1] = glm::vec4(glm::vec3(model[1]) / scale.y, 0.0f);
  rotation[2] = glm::vec4(glm::vec3(model[2]) / scale.z, 0.0f);

  // Only what this operation changes, so moving never nudges the rotation through float round-off
  Transform wanted = *transform;
  if (operation == ImGuizmo::TRANSLATE)
    wanted.position = glm::vec3(model[3]);
  else if (operation == ImGuizmo::ROTATE) {
    m_dragRotation = glm::normalize(glm::quat_cast(rotation));
    wanted.rotation = snapping ? snappedRotation(m_dragRotation)
                               : eulerOf(m_dragRotation); // steady at 90 degrees of pitch, no round-off
  }
  else
    wanted.scale = scale;
  // The gizmo snaps how far it moved, from where the drag started: an entity at 1.3 went to 1.8 and 2.3, never onto
  // the grid. The axes that changed go onto it instead (world axes: along local ones the grid has no meaning), and a
  // scale that changed to the 0.1 steps (angles: snappedRotation).
  if (snapping) {
    const auto onto = [](float value, float step) {
      const float snapped = std::round(value / step) * step;
      return snapped == 0.0f ? 0.0f : snapped; // not -0
    };
    for (int axis = 0; axis < 3; ++axis) {
      if (operation == ImGuizmo::TRANSLATE && mode == ImGuizmo::WORLD &&
          std::abs(wanted.position[axis] - transform->position[axis]) > 1e-5f)
        wanted.position[axis] = onto(wanted.position[axis], kSnapStep);
      if (operation == ImGuizmo::SCALE && std::abs(wanted.scale[axis] - transform->scale[axis]) > 1e-5f)
        wanted.scale[axis] = std::max(onto(wanted.scale[axis], snapStep), snapStep);
    }
  }
  applyGizmoChange(systemManager, componentManager, *transform, wanted, operation == ImGuizmo::TRANSLATE);
}

namespace {
// The world box of a model as placed (its bounds, rotated and scaled, boxed again along the world axes)
struct WorldBox {
  glm::vec3 min, max;
};
WorldBox worldBox(const Mesh &mesh, const glm::mat4 &matrix) {
  const glm::vec3 low = mesh.getBoundsMin(), high = mesh.getBoundsMax();
  WorldBox box{glm::vec3(std::numeric_limits<float>::max()), glm::vec3(-std::numeric_limits<float>::max())};
  for (int i = 0; i < 8; ++i) {
    const glm::vec3 corner(i & 1 ? high.x : low.x, i & 2 ? high.y : low.y, i & 4 ? high.z : low.z);
    const glm::vec3 world = glm::vec3(matrix * glm::vec4(corner, 1.0f));
    box.min = glm::min(box.min, world);
    box.max = glm::max(box.max, world);
  }
  return box;
}
bool boxesOverlap(const WorldBox &a, const WorldBox &b) {
  return glm::all(glm::lessThan(a.min, b.max)) && glm::all(glm::lessThan(b.min, a.max));
}
} // namespace

bool EditorUISystem::applyGizmoChange(SystemManager &systemManager, ComponentManager &componentManager,
                                      Transform &transform, const Transform &wanted, bool translating) {
  const auto *model = componentManager.tryGet<Model>(m_gizmoEntity);
  if (!m_gizmoSolid || !model) {
    transform = wanted;
    return true;
  }
  auto &resourceSystem = systemManager.getSystem<ResourceSystem>();
  auto &transformSystem = systemManager.getSystem<TransformSystem>();
  const ModelQueries queries(componentManager, resourceSystem, transformSystem);
  const Mesh &mesh = resourceSystem.getMesh(model->meshHandle);

  // Models it already cuts into (a floor it is sunk into a little) cannot block every move, or it would be stuck:
  // it may leave them or slide along them, just not sink deeper. Their boxes judge "deeper".
  std::vector<Entity> alreadyIn;
  std::vector<WorldBox> alreadyInBoxes;
  const WorldBox startBox = worldBox(mesh, transformSystem.calculateModelMatrix(transform));
  componentManager.forEachComponent<Model>([&](Entity entity, Model &other) {
    const auto *otherTransform = componentManager.tryGet<Transform>(entity);
    if (entity == m_gizmoEntity || !otherTransform || componentManager.has<Hidden>(entity))
      return;
    const WorldBox box =
        worldBox(resourceSystem.getMesh(other.meshHandle), transformSystem.calculateModelMatrix(*otherTransform));
    if (boxesOverlap(startBox, box) && queries.intersects(m_gizmoEntity, transform, entity)) {
      alreadyIn.push_back(entity);
      alreadyInBoxes.push_back(box);
    }
  });
  const auto blocks = [&](Entity entity) { return std::find(alreadyIn.begin(), alreadyIn.end(), entity) == alreadyIn.end(); };

  if (!translating) {
    // Whether a pose cuts into another model, or deeper into one it already cuts into (by their boxes: sticking out
    // past one side of it at the start, it may not reach further in on that side)
    const auto collidesAt = [&](const Transform &pose) {
      bool collides = false;
      componentManager.forEachComponent<Model>([&](Entity entity, Model &) {
        if (!collides && entity != m_gizmoEntity && blocks(entity) && !componentManager.has<Hidden>(entity))
          collides = queries.intersects(m_gizmoEntity, pose, entity);
      });
      if (collides)
        return true;
      const WorldBox now = worldBox(mesh, transformSystem.calculateModelMatrix(pose));
      for (const WorldBox &box : alreadyInBoxes)
        for (int axis = 0; axis < 3; ++axis) {
          const bool fromAbove = startBox.max[axis] > box.max[axis] && startBox.min[axis] > box.min[axis];
          const bool fromBelow = startBox.min[axis] < box.min[axis] && startBox.max[axis] < box.max[axis];
          if ((fromAbove && now.min[axis] < startBox.min[axis] - 1e-4f) ||
              (fromBelow && now.max[axis] > startBox.max[axis] + 1e-4f))
            return true;
        }
      return false;
    };

    // Rotation and scale: all or nothing
    if (collidesAt(wanted))
      return false;
    transform = wanted;
    return true;
  }

  // Moving: one axis at a time, each stopped where its triangles first touch another model's, so the rest of the
  // move still happens and the entity slides along what it hits. The whole path is swept: a big (snapped) step
  // cannot jump through a thin wall.
  Transform pose = transform;
  for (int axis = 0; axis < 3; ++axis) {
    glm::vec3 delta(0.0f);
    delta[axis] = wanted.position[axis] - pose.position[axis];
    if (delta[axis] == 0.0f)
      continue;
    float fraction = queries.sweep(m_gizmoEntity, pose, delta, blocks).fraction;

    // Not deeper into what it already cuts into: sticking out past its top (resting on a floor), no further down,
    // and so on for each side. Within it (a room) or across it on this axis there is nothing to sink into.
    const WorldBox here = worldBox(mesh, transformSystem.calculateModelMatrix(pose));
    for (const WorldBox &box : alreadyInBoxes) {
      const bool inside = here.min[axis] >= box.min[axis] && here.max[axis] <= box.max[axis];
      const bool around = here.min[axis] <= box.min[axis] && here.max[axis] >= box.max[axis];
      if (inside || around)
        continue;
      if ((here.max[axis] > box.max[axis] && delta[axis] < 0.0f) || (here.min[axis] < box.min[axis] && delta[axis] > 0.0f))
        fraction = 0.0f;
    }
    pose.position[axis] += delta[axis] * fraction;
  }
  transform.position = pose.position;
  return true;
}

glm::vec3 EditorUISystem::oneSidedScalePosition(SystemManager &systemManager, ComponentManager &componentManager,
                                                const Transform &transform, const glm::vec3 &newScale,
                                                bool keepLowSide) const {
  const auto *model = componentManager.tryGet<Model>(m_gizmoEntity);
  if (!model)
    return transform.position;
  const Mesh &mesh = systemManager.getSystem<ResourceSystem>().getMesh(model->meshHandle);
  const glm::vec3 fixedSide = keepLowSide ? mesh.getBoundsMin() : mesh.getBoundsMax();
  // The entity's own axes (unit length): its rotation alone
  Transform rotationOnly = transform;
  rotationOnly.position = glm::vec3(0.0f);
  rotationOnly.scale = glm::vec3(1.0f);
  const glm::mat3 axes(systemManager.getSystem<TransformSystem>().calculateModelMatrix(rotationOnly));
  // A point at the fixed side stays put: position + axes * (scale * side) is the same before and after
  glm::vec3 position = transform.position;
  for (int axis = 0; axis < 3; ++axis)
    position += axes[axis] * ((transform.scale[axis] - newScale[axis]) * fixedSide[axis]);
  return position;
}

Transform EditorUISystem::resizedTransform(SystemManager &systemManager, ComponentManager &componentManager,
                                          float offset) const {
  Transform result = m_resize.start;
  const auto *model = componentManager.tryGet<Model>(m_gizmoEntity);
  if (!model)
    return result;
  const Mesh &mesh = systemManager.getSystem<ResourceSystem>().getMesh(model->meshHandle);
  const int axis = m_resize.axis;
  const float localSize = mesh.getBoundsMax()[axis] - mesh.getBoundsMin()[axis];
  if (localSize <= 0.0f)
    return result;
  // The face moves `offset` outward: the size along the axis changes by that much, at least a sliver remaining
  const float startSize = localSize * m_resize.start.scale[axis];
  const float size = std::max(startSize + offset, 0.01f);
  glm::vec3 scale = m_resize.start.scale;
  scale[axis] = m_resize.start.scale[axis] * size / startSize;
  result.scale = scale;
  // The opposite face stays: the low one when dragging the high face, and the other way round
  result.position = oneSidedScalePosition(systemManager, componentManager, m_resize.start, scale, m_resize.side > 0.0f);
  return result;
}

void EditorUISystem::renderResizeHandles(SystemManager &systemManager, ComponentManager &componentManager,
                                         Transform &transform, const glm::mat4 &view, const glm::mat4 &projection) {
  const auto *model = componentManager.tryGet<Model>(m_gizmoEntity);
  ImGuiIO &io = ImGui::GetIO();
  if (!model || m_viewRect.z <= 0.0f || m_viewRect.w <= 0.0f) {
    m_resize.axis = -1;
    m_resizeHovered = false;
    return;
  }
  auto &transformSystem = systemManager.getSystem<TransformSystem>();
  const Mesh &mesh = systemManager.getSystem<ResourceSystem>().getMesh(model->meshHandle);
  const glm::mat4 toWorld = transformSystem.calculateModelMatrix(transform);
  const glm::mat4 toClip = projection * view;
  const glm::vec3 low = mesh.getBoundsMin(), high = mesh.getBoundsMax(), center = (low + high) * 0.5f;
  Transform rotationOnly = transform;
  rotationOnly.position = glm::vec3(0.0f);
  rotationOnly.scale = glm::vec3(1.0f);
  const glm::mat3 axes(transformSystem.calculateModelMatrix(rotationOnly));

  auto toScreen = [&](const glm::vec3 &world, ImVec2 &screen) {
    const glm::vec4 clip = toClip * glm::vec4(world, 1.0f);
    if (clip.w <= 0.01f)
      return false;
    screen = fromClip(clip);
    return true;
  };

  // The six handles: each face's center, the cursor's nearest one within reach
  constexpr float kRadius = 7.0f;
  struct Handle {
    int axis;
    float side;
    glm::vec3 world;
    ImVec2 screen;
  };
  std::vector<Handle> handles;
  int hovered = -1;
  float hoveredDistance = kRadius + 3.0f;
  for (int axis = 0; axis < 3; ++axis)
    for (float side : {1.0f, -1.0f}) {
      glm::vec3 local = center;
      local[axis] = side > 0.0f ? high[axis] : low[axis];
      Handle handle{axis, side, glm::vec3(toWorld * glm::vec4(local, 1.0f)), ImVec2()};
      if (!toScreen(handle.world, handle.screen))
        continue;
      const float distance = std::hypot(io.MousePos.x - handle.screen.x, io.MousePos.y - handle.screen.y);
      if (distance < hoveredDistance && !mouseOnPanels()) {
        hoveredDistance = distance;
        hovered = static_cast<int>(handles.size());
      }
      handles.push_back(handle);
    }
  m_resizeHovered = hovered >= 0;

  // Grab
  if (m_resize.axis < 0 && hovered >= 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
    m_resize.axis = handles[hovered].axis;
    m_resize.side = handles[hovered].side;
    m_resize.start = transform;
    m_resize.faceStart = handles[hovered].world;
    m_resize.direction = glm::normalize(axes[m_resize.axis] * m_resize.side);
    m_resize.applied = 0.0f;
    systemManager.getSystem<CameraSystem>().editorTookCamera();
  }

  // Drag: the face follows the point of its line nearest to the cursor's ray
  if (m_resize.axis >= 0) {
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
      m_resize.axis = -1;
    } else {
      const glm::vec2 ndc = toNdc(io.MousePos);
      const glm::mat4 inverse = glm::inverse(toClip);
      glm::vec4 nearPoint = inverse * glm::vec4(ndc, -1.0f, 1.0f), farPoint = inverse * glm::vec4(ndc, 1.0f, 1.0f);
      nearPoint /= nearPoint.w;
      farPoint /= farPoint.w;
      const glm::vec3 rayOrigin(nearPoint), rayDir = glm::normalize(glm::vec3(farPoint - nearPoint));
      // Closest points of the face's line (faceStart + direction * t) and the ray
      const glm::vec3 w0 = m_resize.faceStart - rayOrigin;
      const float b = glm::dot(m_resize.direction, rayDir), d = glm::dot(m_resize.direction, w0),
                  e = glm::dot(rayDir, w0);
      const float denominator = 1.0f - b * b;
      if (denominator > 1e-4f) { // not looking straight along the line
        float offset = (b * e - d) / denominator;
        if (m_gizmoSnap || io.KeyCtrl)
          offset = std::round(offset / 0.5f) * 0.5f;
        Transform wanted = resizedTransform(systemManager, componentManager, offset);
        Transform result = transform;
        if (applyGizmoChange(systemManager, componentManager, result, wanted, false)) {
          m_resize.applied = offset;
        } else {
          // Solid refused it: grow as far as it fits between where it is and where the cursor wants it
          float fits = m_resize.applied, blocked = offset;
          for (int step = 0; step < 12; ++step) {
            const float middle = (fits + blocked) * 0.5f;
            Transform candidate = transform;
            if (applyGizmoChange(systemManager, componentManager, candidate,
                                 resizedTransform(systemManager, componentManager, middle), false))
              fits = middle;
            else
              blocked = middle;
          }
          m_resize.applied = fits;
          result = resizedTransform(systemManager, componentManager, fits);
        }
        transform = result;
      }
    }
  }

  // Drawn last, at the faces as they are now
  ImDrawList *drawList = ImGui::GetBackgroundDrawList();
  const ImU32 colors[3] = {IM_COL32(230, 70, 70, 255), IM_COL32(80, 200, 90, 255), IM_COL32(70, 120, 240, 255)};
  for (size_t i = 0; i < handles.size(); ++i) {
    const Handle &handle = handles[i];
    const bool active = m_resize.axis == handle.axis && m_resize.side == handle.side;
    ImVec2 screen = handle.screen;
    if (active) { // the dragged face has moved since the handles were placed
      glm::vec3 local = center;
      local[handle.axis] = handle.side > 0.0f ? high[handle.axis] : low[handle.axis];
      toScreen(glm::vec3(transformSystem.calculateModelMatrix(transform) * glm::vec4(local, 1.0f)), screen);
    }
    drawList->AddCircleFilled(screen, kRadius, colors[handle.axis]);
    if (active || static_cast<int>(i) == hovered)
      drawList->AddCircle(screen, kRadius + 2.5f, IM_COL32(255, 255, 255, 230), 0, 2.0f);
  }
}

glm::vec3 EditorUISystem::snappedRotation(const glm::quat &dragged) const {
  // A turn that changed one angle only (about the world's up: the heading, whatever the entity's tilt; or one axis of
  // an entity that is not tilted otherwise) puts that angle on the 15 degree steps: a crate at 7 degrees goes to 0 or
  // 15, not 22. Wrapped, so 179 to -179 is a change of 2.
  const glm::vec3 angles = eulerOf(dragged);
  int changed = -1, count = 0;
  for (int axis = 0; axis < 3; ++axis) {
    const float difference = std::remainder(angles[axis] - m_dragStartAngles[axis], 360.0f);
    if (std::abs(difference) > 1e-3f) {
      changed = axis;
      ++count;
    }
  }
  if (count == 0)
    return m_dragStartAngles;
  if (count == 1) {
    glm::vec3 snapped = angles;
    snapped[changed] = std::round(angles[changed] / kSnapAngle) * kSnapAngle;
    if (snapped[changed] == 0.0f || std::abs(snapped[changed]) == 360.0f)
      snapped[changed] = 0.0f;
    return snapped;
  }
  // A turn that moved several angles at once (a ring tilted with the entity): rounding each would not be the turn the
  // ring made, so the turn itself goes to 15 degree steps, around its own axis, from where the drag started
  const glm::quat turn = glm::normalize(dragged * glm::inverse(m_dragStartRotation));
  const float angle = 2.0f * std::acos(std::clamp(std::abs(turn.w), 0.0f, 1.0f));
  if (angle < 1e-5f)
    return m_dragStartAngles;
  const glm::vec3 axis = glm::normalize(glm::vec3(turn.x, turn.y, turn.z) * (turn.w < 0.0f ? -1.0f : 1.0f));
  const float stepped = glm::radians(std::round(glm::degrees(angle) / kSnapAngle) * kSnapAngle);
  return eulerOf(glm::angleAxis(stepped, axis) * m_dragStartRotation);
}

bool EditorUISystem::alignToSurface(SystemManager &systemManager, ComponentManager &componentManager,
                                    Transform &transform, bool snapped, const glm::vec3 &grab) {
  const auto *model = componentManager.tryGet<Model>(m_gizmoEntity);
  auto &cameraSystem = systemManager.getSystem<CameraSystem>();
  const Entity cameraEntity = cameraSystem.getActiveCamera();
  const Camera *camera = cameraEntity != -1 ? componentManager.tryGet<Camera>(cameraEntity) : nullptr;
  const ImGuiIO &io = ImGui::GetIO();
  if (!model || !camera || m_viewRect.z <= 0.0f || m_viewRect.w <= 0.0f)
    return false;

  auto &resourceSystem = systemManager.getSystem<ResourceSystem>();
  const Mesh &mesh = resourceSystem.getMesh(model->meshHandle);
  const glm::vec3 boundsMin = mesh.getBoundsMin(), boundsMax = mesh.getBoundsMax();
  const glm::mat4 viewProjection = cameraSystem.getProjectionMatrix(*camera) * cameraSystem.getViewMatrix(*camera);
  const glm::vec2 mouse(io.MousePos.x, io.MousePos.y);

  // The cursor's ray
  const glm::vec2 ndc = toNdc(io.MousePos);
  const glm::mat4 inverse = glm::inverse(viewProjection);
  glm::vec4 nearPoint = inverse * glm::vec4(ndc, -1.0f, 1.0f), farPoint = inverse * glm::vec4(ndc, 1.0f, 1.0f);
  nearPoint /= nearPoint.w;
  farPoint /= farPoint.w;
  const glm::vec3 origin(nearPoint);
  const glm::vec3 direction = glm::normalize(glm::vec3(farPoint - nearPoint));

  // Surfaces of anything but what is being moved: the entity, the other selected ones, and their children (a
  // prefab's parts), which it would otherwise climb onto
  auto &transformSystem = systemManager.getSystem<TransformSystem>();
  const auto other = [&](Entity entity) {
    for (int guard = 0; guard < 256 && entity != -1; ++guard) {
      if (entity == m_gizmoEntity || isSelected(entity))
        return false;
      entity = transformSystem.getParent(componentManager, entity);
    }
    return true;
  };
  const ModelQueries queries(componentManager, resourceSystem, transformSystem);
  const auto hit = queries.raycast(origin, direction, std::numeric_limits<float>::max(), other);
  if (!hit)
    return false;
  const glm::vec3 normal = hit->normal;

  // R and T while dragging (not while a text field has the keyboard). R turns it 90 degrees around the surface's
  // normal; T, without Tilt, tips it 90 degrees around the surface axis nearest the camera's right.
  if (!io.WantTextInput) {
    if (ImGui::IsKeyPressed(ImGuiKey_R, false)) {
      if (m_gizmoAlignTilt)
        m_alignHeading += 90.0f;
      else
        m_alignRotation = glm::angleAxis(glm::radians(90.0f), normal) * m_alignRotation;
      m_alignTurned = true;
    }
    if (!m_gizmoAlignTilt && ImGui::IsKeyPressed(ImGuiKey_T, false)) {
      const glm::vec3 right(glm::inverse(cameraSystem.getViewMatrix(*camera))[0]);
      const glm::vec3 axis = right - normal * glm::dot(right, normal);
      if (glm::length(axis) > 1e-4f) {
        m_alignRotation = glm::angleAxis(glm::radians(90.0f), glm::normalize(axis)) * m_alignRotation;
        m_alignTurned = true;
      }
    }
  }

  // Tilt: its up (+Y) along the surface's normal, keeping the heading it had when the drag started (to 15 degrees
  // when snapping: the rotation around the normal). Otherwise the rotation it has, which only R and T change.
  glm::quat rotation = m_alignRotation;
  if (m_gizmoAlignTilt) {
    const glm::vec3 up(0.0f, 1.0f, 0.0f);
    const float headingDegrees = snapped ? std::round(m_alignHeading / kSnapAngle) * kSnapAngle : m_alignHeading;
    rotation = glm::rotation(up, normal) * glm::angleAxis(glm::radians(headingDegrees), up);
  }

  // The gizmo's center (the origin) stays under the cursor: on the cursor's ray, as high above the surface it points
  // at as the entity's base is below its origin, while that surface is under the center. Whether it can be is decided
  // when the cursor comes onto a surface (with a ray not along it). On one too small for it (a pillar's top or a head seen from low, where the
  // held center is in front of it), or with the ray running along the surface, the entity follows the spot pointed at
  // instead, its origin over it, so it moves freely there. Near the edge of a surface it holds on, it is slid back
  // towards that spot, the least that puts the center over the surface again: it always rests on the surface you
  // point at, without jumping.
  const float height = surfacePlacement::supportDistance(boundsMin, boundsMax, transform.scale, rotation, normal);
  // The grabbed spot as the entity will rest (its own frame turned by that rotation); zero for the gizmo's center
  const glm::vec3 grabbed = rotation * grab;
  const glm::vec3 grabbedAlong = normal * glm::dot(grabbed, normal);
  constexpr float kLift = 0.01f; // the ray down starts a little above the center, for an entity whose base is it
  const auto onSurface = [&](const glm::vec3 &at) {
    const auto below = queries.raycast(at + normal * kLift, -normal, std::numeric_limits<float>::max(), other);
    // The surface pointed at, or another in its plane (a floor of several tiles)
    return below && glm::dot(below->normal, normal) > 0.99f &&
           std::abs(below->distance - kLift - height) < 0.01f + 0.01f * height;
  };
  glm::vec3 center;
  const bool canHold = surfacePlacement::grabbedOrigin(origin, direction, hit->point, normal, height, grabbed, center);
  if (canHold && hit->entity != m_alignSurface) { // with the ray along the surface, decided once it is not
    m_alignSurface = hit->entity;
    m_alignHeld = onSurface(center);
  }
  const glm::vec3 overHit = hit->point + normal * height - (grabbed - grabbedAlong); // the grabbed spot over the hit
  glm::vec3 position =
      canHold && m_alignHeld ? surfacePlacement::slideToSupport(center, overHit, onSurface) : overHit;
  if (m_gizmoAlignTilt || m_alignTurned)
    transform.rotation = eulerOf(rotation);

  // When snapping, the spot under its origin goes to the grid of the model it rests on, and the origin follows
  if (snapped) {
    glm::quat frameRotation(1.0f, 0.0f, 0.0f, 0.0f);
    glm::vec3 frameOrigin(0.0f);
    if (const Transform *surface = componentManager.tryGet<Transform>(hit->entity)) {
      const glm::mat4 world = transformSystem.calculateModelMatrix(*surface);
      frameRotation = glm::quat_cast(glm::mat3(glm::normalize(glm::vec3(world[0])), glm::normalize(glm::vec3(world[1])),
                                               glm::normalize(glm::vec3(world[2]))));
      frameOrigin = glm::vec3(world[3]);
    }
    position = surfacePlacement::snapInFrame(position - normal * height, normal, frameRotation, frameOrigin, kSnapStep) +
               normal * height;
  }
  transform.position = position;
  return true;
}

// No surface under the cursor: the grabbed spot goes where the cursor's ray meets the plane facing the camera through
// where the spot was
void EditorUISystem::moveGrabFreely(SystemManager &systemManager, ComponentManager &componentManager,
                                    Transform &transform, const glm::vec3 &point, const glm::vec3 &local, bool level,
                                    bool snapped) {
  auto &cameraSystem = systemManager.getSystem<CameraSystem>();
  const Entity cameraEntity = cameraSystem.getActiveCamera();
  const Camera *camera = cameraEntity != -1 ? componentManager.tryGet<Camera>(cameraEntity) : nullptr;
  if (!camera || m_viewRect.z <= 0.0f || m_viewRect.w <= 0.0f)
    return;
  const glm::mat4 cameraToWorld = glm::inverse(cameraSystem.getViewMatrix(*camera));
  const glm::vec3 forward = -glm::normalize(glm::vec3(cameraToWorld[2]));
  const glm::mat4 inverse = glm::inverse(cameraSystem.getProjectionMatrix(*camera) * cameraSystem.getViewMatrix(*camera));
  const glm::vec2 ndc = toNdc(ImGui::GetIO().MousePos);
  glm::vec4 nearPoint = inverse * glm::vec4(ndc, -1.0f, 1.0f), farPoint = inverse * glm::vec4(ndc, 1.0f, 1.0f);
  nearPoint /= nearPoint.w;
  farPoint /= farPoint.w;
  const glm::vec3 origin(nearPoint), direction = glm::normalize(glm::vec3(farPoint - nearPoint));
  // The plane's normal: up for a level move, which never changes the height: past that plane's horizon (the ray never
  // meets it in front) or absurdly far along it, the entity just stays where it was
  const glm::vec3 normal = level ? glm::vec3(0.0f, 1.0f, 0.0f) : forward;
  const float facing = glm::dot(direction, normal);
  if (std::abs(facing) < 1e-4f)
    return;
  const float along = glm::dot(point - origin, normal) / facing;
  constexpr float kFarthest = 500.0f;
  if (along <= 0.0f || along > kFarthest)
    return;
  transform.position = origin + direction * along - rotationOf(transform) * local;
  if (snapped) // onto the grid, like the gizmo's snapped moves
    transform.position = glm::round(transform.position / kSnapStep) * kSnapStep;
}

namespace {
std::string displayName(ComponentManager &componentManager, Entity entity) {
  const Name *name = componentManager.tryGet<Name>(entity);
  return name && !name->name.empty() ? name->name : "Entity " + std::to_string(entity);
}
} // namespace

void EditorUISystem::dragToParent(Engine &engine, SystemManager &systemManager, ComponentManager &componentManager,
                                  Entity entity, const std::string &label) {
  // Drag an entity onto another to make it a child of it
  if (ImGui::BeginDragDropSource()) {
    ImGui::SetDragDropPayload("ENTITY", &entity, sizeof(Entity));
    ImGui::Text("Attach %s to...", displayName(componentManager, entity).c_str());
    ImGui::EndDragDropSource();
  }
  if (ImGui::BeginDragDropTarget()) {
    if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("ENTITY")) {
      const Entity dragged = *static_cast<const Entity *>(payload->Data);
      if (dragged != entity)
        systemManager.getSystem<TransformSystem>().setParent(componentManager, dragged, entity);
    }
    // A script or a sound from the asset browser goes onto the entity (other assets do not belong on an entity)
    const ImGuiPayload *asset = ImGui::GetDragDropPayload();
    if (asset && asset->IsDataType(AssetBrowser::kPayload) && asset->Data) {
      const std::string path = static_cast<const char *>(asset->Data);
      const AssetBrowser::Kind kind = AssetBrowser::classify(path);
      if ((kind == AssetBrowser::Kind::Script || kind == AssetBrowser::Kind::Sound) &&
          ImGui::AcceptDragDropPayload(AssetBrowser::kPayload)) {
        engine.beginCheckpoint();
        assignAssetToEntity(engine, componentManager, entity, path);
        selectOnly(entity);
      }
    }
    ImGui::EndDragDropTarget();
  }
  (void)label;
}

void EditorUISystem::setSceneFilter(const std::string &text) {
  std::snprintf(m_sceneFilter, sizeof(m_sceneFilter), "%s", text.c_str());
}

void EditorUISystem::renderHierarchy(Engine &engine, SystemManager &systemManager, ComponentManager &componentManager) {
  auto &transformSystem = systemManager.getSystem<TransformSystem>();
  auto &serializer = systemManager.getSystem<SceneSerializer>();

  ImGui::SetNextItemWidth(-1);
  ImGui::InputTextWithHint("##sceneSearch", "Search (name, t:Light, tag:enemy)", m_sceneFilter, sizeof(m_sceneFilter));
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Words match the name; t:Model, t:Light, t:Script... match a component; tag:enemy a tag.\n"
                      "Every term must match. A search shows a flat list.");
  const sceneFilter::Query query = sceneFilter::parse(m_sceneFilter);

  // The list fills the panel (it is all there is); the empty space at its bottom is where a drop detaches
  ImGui::BeginChild("Hierarchy", ImVec2(0, std::max(ImGui::GetContentRegionAvail().y, ui(120.0f))),
                    ImGuiChildFlags_Borders);

  // Every entity of the scene, in creation order. Nothing is left out: Hidden only turns the entity's drawing off, and
  // the editor keeps no entities of its own.
  std::vector<Entity> all(engine.getEntityManager().entities().begin(), engine.getEntityManager().entities().end());
  std::sort(all.begin(), all.end());

  // The rows as drawn, for Shift+click ranges; the range is made once they all are
  std::vector<Entity> visible;
  Entity rangeTarget = -1;

  // The tag in front of the name: what the entity is, as the old per-type lists said
  auto iconOf = [&](Entity entity) -> const char * {
    if (componentManager.has<Model>(entity))
      return "[M] ";
    if (componentManager.has<Light>(entity))
      return "[L] ";
    if (componentManager.has<Camera>(entity))
      return "[C] ";
    if (componentManager.has<ParticleEmitter>(entity))
      return "[FX] "; // [P] is a prefab's
    return "";
  };
  // "[P] " on a prefab instance's root, "[P door] " on a nested one (it names its own prefab)
  auto prefabMark = [&](Entity entity) -> std::string {
    const PrefabInstance *instance = componentManager.tryGet<PrefabInstance>(entity);
    if (!instance)
      return "";
    return serializer.enclosingInstance(entity) != -1
               ? "[P " + std::filesystem::path(instance->path).stem().string() + "] "
               : "[P] ";
  };

  const bool scroll = m_scrollToSelection;
  if (query.empty()) {
    // Children by parent, built once for the frame (asking the TransformSystem per row would be quadratic)
    std::unordered_map<Entity, std::vector<Entity>> children;
    std::vector<Entity> roots;
    for (Entity entity : all) {
      const Entity parent = transformSystem.getParent(componentManager, entity);
      if (parent == -1 || !engine.getEntityManager().isAlive(parent))
        roots.push_back(entity);
      else
        children[parent].push_back(entity);
    }
    // The way to the selected entity opens when a click elsewhere selected it
    std::unordered_set<Entity> openPath;
    if (scroll && selectedEntity != -1)
      for (Entity up = transformSystem.getParent(componentManager, selectedEntity); up != -1 && openPath.size() < 256;
           up = transformSystem.getParent(componentManager, up))
        openPath.insert(up);
    // In a big scene the roots start closed, or the list would open with thousands of rows
    const bool bigScene = all.size() > 200;

    std::function<void(Entity, int)> node = [&](Entity entity, int depth) {
      static const std::vector<Entity> none;
      const auto found = children.find(entity);
      const std::vector<Entity> &kids = found == children.end() ? none : found->second;
      ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
      if (!(bigScene && depth == 0))
        flags |= ImGuiTreeNodeFlags_DefaultOpen;
      if (kids.empty())
        flags |= ImGuiTreeNodeFlags_Leaf;
      if (isSelected(entity))
        flags |= ImGuiTreeNodeFlags_Selected;
      // Linked prefab instances: blue, the root marked
      const bool linked = serializer.instanceRoot(entity) != -1;
      const std::string label = std::string(iconOf(entity)) + prefabMark(entity) + displayName(componentManager, entity) +
                                "##tree" + std::to_string(entity);
      visible.push_back(entity);
      if (openPath.count(entity))
        ImGui::SetNextItemOpen(true);
      // What the game created is dimmed (and wins over the prefab blue): it is not saved and Stop discards it
      const bool runtime = engine.isRuntime(entity);
      if (runtime)
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
      else if (linked)
        ImGui::PushStyleColor(ImGuiCol_Text, kPrefabLinkColor);
      const bool open = ImGui::TreeNodeEx(label.c_str(), flags);
      if (runtime || linked)
        ImGui::PopStyleColor();
      if (runtime && ImGui::IsItemHovered())
        ImGui::SetTooltip("Created by the game: not saved, gone on Stop");
      if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
        if (ImGui::GetIO().KeyShift && !ImGui::GetIO().KeyCtrl)
          rangeTarget = entity;
        else
          clickInList(entity, visible, false);
      }
      if (scroll && selectedEntity == entity)
        ImGui::SetScrollHereY();
      dragToParent(engine, systemManager, componentManager, entity, label);
      entityContextMenu(engine, systemManager, componentManager, entity);
      if (open) {
        if (depth < 64)
          for (Entity child : kids)
            node(child, depth + 1);
        ImGui::TreePop();
      }
    };
    // One header per open scene, its entities below it; the game's own (in no scene) after them. With no scene open the
    // list is flat, as when only the game has made entities.
    const auto &scenes = engine.getEntityManager().scenes();
    for (const EntityManager::Scene &scene : scenes) {
      if (!renderSceneHeader(engine, scene, engine.isSceneDirty(scene.path)))
        continue;
      bool any = false;
      for (Entity root : roots)
        if (engine.getEntityManager().sceneOf(root) == scene.id) {
          any = true;
          node(root, 0);
        }
      if (!any)
        ImGui::TextDisabled("  (empty scene)");
      ImGui::TreePop();
    }
    std::vector<Entity> loose;
    for (Entity root : roots)
      if (engine.getEntityManager().sceneOf(root) == -1)
        loose.push_back(root);
    if (scenes.empty()) {
      for (Entity root : loose)
        node(root, 0);
      if (roots.empty())
        ImGui::TextDisabled("  (empty scene)");
    } else if (!loose.empty()) {
      ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
      const bool open = ImGui::TreeNodeEx("Created by the game##gameEntities",
                                          ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth);
      ImGui::PopStyleColor();
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Not part of any scene: not saved, gone on Stop");
      if (open) {
        for (Entity root : loose)
          node(root, 0);
        ImGui::TreePop();
      }
    }
    if (rangeTarget != -1)
      clickInList(rangeTarget, visible, false);
  } else {
    // A search: the matches as a flat list, each with the names of its parents in front, dimmed
    std::unordered_map<std::string, std::string> componentNames; // lowercase -> the name Engine::hasComponent knows
    for (const std::string &name : sceneFilter::builtInComponentNames())
      componentNames[sceneFilter::lower(name)] = name;
    for (const auto &reflected : engine.getReflectedComponents())
      componentNames[sceneFilter::lower(reflected.name)] = reflected.name;
    for (Entity entity : all) {
      const bool fits = sceneFilter::matches(
          query, displayName(componentManager, entity),
          [&](const std::string &term) {
            const auto name = componentNames.find(term);
            return name != componentNames.end() && engine.hasComponent(entity, name->second);
          },
          [&](const std::string &term) {
            const Tags *tags = componentManager.tryGet<Tags>(entity);
            if (tags)
              for (const std::string &tag : tags->tags)
                if (sceneFilter::lower(tag) == term)
                  return true;
            return false;
          });
      if (fits)
        visible.push_back(entity);
    }

    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(visible.size()));
    if (scroll) {
      const auto at = std::find(visible.begin(), visible.end(), selectedEntity);
      if (at != visible.end())
        clipper.IncludeItemByIndex(static_cast<int>(at - visible.begin())); // drawn even if off screen, to scroll to it
    }
    while (clipper.Step()) {
      for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
        const Entity entity = visible[row];
        std::string path;
        if (engine.getEntityManager().scenes().size() > 1)
          if (const EntityManager::Scene *scene =
                  engine.getEntityManager().findScene(engine.getEntityManager().sceneOf(entity)))
            path = (scene->path.empty() ? std::string("untitled") : std::filesystem::path(scene->path).stem().string()) +
                   " / ";
        Entity up = transformSystem.getParent(componentManager, entity);
        for (int guard = 0; up != -1 && guard < 64; ++guard, up = transformSystem.getParent(componentManager, up))
          path = displayName(componentManager, up) + " / " + path;
        const std::string head = std::string(iconOf(entity)) + prefabMark(entity);
        const std::string name = displayName(componentManager, entity);
        const bool linked = serializer.instanceRoot(entity) != -1;
        const std::string label = head + path + name + "##flat" + std::to_string(entity);

        // The row is one selectable; its text is drawn over it in two colors, the path dimmed
        const ImVec2 textPos = ImGui::GetCursorScreenPos();
        if (ImGui::Selectable(("##flat" + std::to_string(entity)).c_str(), isSelected(entity))) {
          if (ImGui::GetIO().KeyShift && !ImGui::GetIO().KeyCtrl)
            rangeTarget = entity;
          else
            clickInList(entity, visible, false);
        }
        ImDrawList *draw = ImGui::GetWindowDrawList();
        const ImU32 color = linked ? ImGui::GetColorU32(kPrefabLinkColor) : ImGui::GetColorU32(ImGuiCol_Text);
        const ImU32 dim = ImGui::GetColorU32(ImGuiCol_TextDisabled);
        float x = textPos.x;
        draw->AddText(ImVec2(x, textPos.y), color, head.c_str());
        x += ImGui::CalcTextSize(head.c_str()).x;
        draw->AddText(ImVec2(x, textPos.y), dim, path.c_str());
        x += ImGui::CalcTextSize(path.c_str()).x;
        draw->AddText(ImVec2(x, textPos.y), color, name.c_str());
        if (scroll && selectedEntity == entity)
          ImGui::SetScrollHereY();
        dragToParent(engine, systemManager, componentManager, entity, label);
        entityContextMenu(engine, systemManager, componentManager, entity);
      }
    }
    if (rangeTarget != -1)
      clickInList(rangeTarget, visible, false);
    if (visible.empty())
      ImGui::TextDisabled("  Nothing matches");
  }
  m_scrollToSelection = false;

  // Dropped on the empty space below: no parent any more
  ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x, std::max(ImGui::GetContentRegionAvail().y, 20.0f)));
  if (ImGui::BeginDragDropTarget()) {
    if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("ENTITY"))
      transformSystem.setParent(componentManager, *static_cast<const Entity *>(payload->Data), -1);
    ImGui::EndDragDropTarget();
  }
  if (ImGui::IsItemHovered() && ImGui::GetDragDropPayload())
    ImGui::SetTooltip("Drop here to detach it");
  ImGui::EndChild();
}

void EditorUISystem::renderParentAndPrefab(SystemManager &systemManager, ComponentManager &componentManager) {
  auto &transformSystem = systemManager.getSystem<TransformSystem>();
  if (!componentManager.has<Transform>(selectedEntity))
    return;

  // Parent: any entity with a Transform but this one and its descendants (which would make a loop)
  const Entity current = transformSystem.getParent(componentManager, selectedEntity);
  const std::string preview = current == -1 ? "(none)" : displayName(componentManager, current);
  if (ImGui::BeginCombo("Parent", preview.c_str())) {
    if (ImGui::Selectable("(none)", current == -1))
      transformSystem.setParent(componentManager, selectedEntity, -1);
    const std::vector<Entity> descendants = transformSystem.getDescendants(componentManager, selectedEntity);
    std::vector<Entity> candidates;
    componentManager.forEachComponent<Transform>([&](Entity entity, Transform &) {
      if (entity != selectedEntity && std::find(descendants.begin(), descendants.end(), entity) == descendants.end())
        candidates.push_back(entity);
    });
    std::sort(candidates.begin(), candidates.end());
    for (Entity candidate : candidates) {
      const std::string label = displayName(componentManager, candidate) + "##parent" + std::to_string(candidate);
      if (ImGui::Selectable(label.c_str(), candidate == current))
        transformSystem.setParent(componentManager, selectedEntity, candidate);
    }
    ImGui::EndCombo();
  }
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Follows the parent; keeps its place in the world when attached or detached.\n"
                      "Dragging one entity onto another in the Scene Explorer does the same.");

  // Prefab: this entity and its children, to instantiate as often as needed
  static char prefabPath[256] = "";
  static Entity prefabPathEntity = -1;
  if (prefabPathEntity != selectedEntity) {
    prefabPathEntity = selectedEntity;
    std::string name = displayName(componentManager, selectedEntity);
    std::replace(name.begin(), name.end(), ' ', '_');
    std::snprintf(prefabPath, sizeof(prefabPath), "../assets/prefabs/%s.json", name.c_str());
  }
  ImGui::SetNextItemWidth(200.0f);
  ImGui::InputText("##prefabPath", prefabPath, sizeof(prefabPath));
  ImGui::SameLine();
  if (ImGui::Button("Save as prefab")) {
    auto &serializer = systemManager.getSystem<SceneSerializer>();
    const std::string file = EngineConfig::resolvePath(prefabPath);
    const Entity owner = serializer.instanceRoot(selectedEntity);
    if (owner != -1 && (owner != selectedEntity || serializer.enclosingInstance(selectedEntity) != -1))
      serializer.savePrefab(selectedEntity, file); // a part of another instance: written, not linked
    else
      serializer.applyInstance(selectedEntity, file); // written, and this entity becomes an instance of it
  }
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Saves this entity and its children and links it to the file: editing the prefab updates\n"
                      "every instance. Add Entity > Prefab and the Assets panel place linked copies of it");
}

void EditorUISystem::renderPlayToolbar(Engine &engine, SystemManager &systemManager) {
  const ImGuiIO &io = ImGui::GetIO();
  const Engine::PlayMode mode = engine.getPlayMode();

  // Ctrl+P: Play from Editing, Stop while testing, and pause a running game. Ctrl+Shift+P: Pause and Resume a test.
  if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_P, false) && !io.WantTextInput &&
      !systemManager.getSystem<CameraSystem>().isNavigating()) {
    if (io.KeyShift) {
      if (mode == Engine::PlayMode::Testing || mode == Engine::PlayMode::Running)
        engine.pause();
      else if (mode == Engine::PlayMode::Paused)
        engine.resume();
    } else if (mode == Engine::PlayMode::Editing)
      engine.play();
    else if (mode == Engine::PlayMode::Testing || mode == Engine::PlayMode::Paused)
      engine.stop();
    else
      engine.pause();
  }

  // A small bar at the top right of the view
  const float right = m_viewRect.x + m_viewRect.z - ui(8.0f);
  ImGui::SetNextWindowPos(ImVec2(right, m_viewRect.y + ui(6.0f)), ImGuiCond_Always, ImVec2(1.0f, 0.0f));
  ImGui::SetNextWindowBgAlpha(0.85f);
  ImGui::Begin("##PlayToolbar", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove |
                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing);
  auto historyButton = [&](const char *label, bool enabled, const char *tooltip, const std::function<void()> &action) {
    ImGui::BeginDisabled(!enabled);
    if (ImGui::Button(label))
      action();
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
      ImGui::SetTooltip("%s", tooltip);
  };
  historyButton("Undo", engine.canUndo(), "Undoes the last edit (Ctrl+Z); not available while testing",
                [&] { engine.undo(); });
  ImGui::SameLine();
  historyButton("Redo", engine.canRedo(), "Does the undone edit again (Ctrl+Shift+Z or Ctrl+Y)",
                [&] { engine.redo(); });
  ImGui::SameLine();
  if (ImGui::Button(m_panelsHidden ? "Restore" : "Maximize"))
    toggleMaximized();
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("%s the panels, keeping the editor going: click to select, the gizmo, the camera\n"
                      "(Shift+Space or Tab)",
                      m_panelsHidden ? "Shows" : "Hides");
  ImGui::SameLine();
  ImGui::TextDisabled("|");
  ImGui::SameLine();
  const ImVec4 green(0.25f, 0.65f, 0.3f, 1.0f), red(0.75f, 0.2f, 0.2f, 1.0f), blue(0.25f, 0.45f, 0.8f, 1.0f);
  auto coloredButton = [this](const char *label, const ImVec4 &color) {
    ImGui::PushStyleColor(ImGuiCol_Button, color);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(color.x + 0.1f, color.y + 0.1f, color.z + 0.1f, 1.0f));
    const bool pressed = ImGui::Button(label, ImVec2(ui(70.0f), 0.0f));
    ImGui::PopStyleColor(2);
    return pressed;
  };
  if (mode == Engine::PlayMode::Running) {
    if (coloredButton("Pause", blue))
      engine.pause();
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Freezes the game to edit the scene (Ctrl+P or Ctrl+Shift+P). Changes made here are kept:\n"
                        "there is no way back. Pause, then Play, tests the game with a way back (Stop)");
    ImGui::SameLine();
    ImGui::TextDisabled("Live: changes are kept");
  } else if (mode == Engine::PlayMode::Editing) {
    if (coloredButton("Play", green))
      engine.play();
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Runs the game from the scene as it is now (Ctrl+P); Stop puts everything back");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.55f, 0.75f, 1.0f, 1.0f), "Editing");
  } else if (mode == Engine::PlayMode::Testing) {
    if (coloredButton("Pause", blue))
      engine.pause();
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Freezes the test; the camera and the editor keep working (Ctrl+Shift+P)");
    ImGui::SameLine();
    if (coloredButton("Stop", red))
      engine.stop();
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Puts every entity back as it was at Play (Ctrl+P)");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.45f, 0.9f, 0.5f, 1.0f), "Playing");
  } else {
    if (coloredButton("Resume", green))
      engine.resume();
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Continues the test (Ctrl+Shift+P), edits made while paused included");
    ImGui::SameLine();
    if (coloredButton("Stop", red))
      engine.stop();
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Ends the test and puts every entity back as it was at Play, edits made while paused\n"
                        "included (Ctrl+P)");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.3f, 1.0f), "Paused (test)");
  }
  ImGui::End();
}

void EditorUISystem::renderSceneExplorer(Engine &engine, SystemManager &systemManager,
                                         ComponentManager &componentManager) {
  auto &renderSystem = systemManager.getSystem<RenderSystem>();
  auto &resourceSystem = systemManager.getSystem<ResourceSystem>();
  auto &sceneSystem = systemManager.getSystem<SceneSystem>();
  auto *window = systemManager.getSystem<WindowSystem>().getWindow();
  ImGuiIO &io = ImGui::GetIO();

  // Docked at the left, the top part of the column (Settings is below it)
  ImGui::Begin("Scene Explorer");

  ImVec2 avail = ImGui::GetContentRegionAvail();
  float buttonHeight = ImGui::GetFrameHeightWithSpacing();
  const ImVec2 treeMin = ImGui::GetCursorScreenPos();
  ImGui::BeginChild("SceneTree", ImVec2(avail.x, avail.y - buttonHeight), true);

  renderHierarchy(engine, systemManager, componentManager);

  ImGui::EndChild();
  // A scene dragged from the Assets panel onto the list is added next to the open ones
  acceptSceneDrop(engine, treeMin, ImVec2(treeMin.x + avail.x, treeMin.y + avail.y - buttonHeight));

  if (ImGui::Button("+ Add Entity", ImVec2(-1, 0))) {
    ImGui::OpenPopup("AddEntityPopup");
  }
  renderAddEntityPopup(engine, sceneSystem, systemManager, componentManager, resourceSystem, renderSystem, window);

  ImGui::End();
}

namespace {
// A vec3 or vec4 named "color" or "colour" (any case, as part of a name too: "glowColor") gets a color picker
bool isColorName(const std::string &name) {
  std::string lower = name;
  std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });
  return lower.find("color") != std::string::npos || lower.find("colour") != std::string::npos;
}

bool editString(const char *label, std::string &text) {
  char buffer[256];
  std::strncpy(buffer, text.c_str(), sizeof(buffer) - 1);
  buffer[sizeof(buffer) - 1] = '\0';
  if (!ImGui::InputText(label, buffer, sizeof(buffer)))
    return false;
  text = buffer;
  return true;
}

// `values` points at 2, 3 or 4 floats; true when edited
bool editFloats(const std::string &name, float *values, int count) {
  if (count >= 3 && isColorName(name))
    return count == 3 ? ImGui::ColorEdit3(name.c_str(), values) : ImGui::ColorEdit4(name.c_str(), values);
  return ImGui::DragScalarN(name.c_str(), ImGuiDataType_Float, values, count, 0.05f);
}

bool editScriptValue(const std::string &name, ScriptValue &value) {
  if (auto *flag = std::get_if<bool>(&value))
    return ImGui::Checkbox(name.c_str(), flag);
  if (auto *number = std::get_if<float>(&value))
    return ImGui::DragFloat(name.c_str(), number, 0.05f);
  if (auto *text = std::get_if<std::string>(&value))
    return editString(name.c_str(), *text);
  return editFloats(name, &std::get<glm::vec3>(value).x, 3);
}

// The script's `properties`: each with the script's value until this entity overrides it
void drawScriptProperties(Script &script, const std::vector<ScriptProperty> &declared) {
  if (declared.empty())
    return;
  ImGui::Separator();
  ImGui::TextDisabled("Properties");
  for (const ScriptProperty &property : declared) {
    ImGui::PushID(property.name.c_str());
    auto own = script.properties.find(property.name);
    // An override of another kind than the script now declares is ignored (and replaced by the next edit)
    const bool overridden = own != script.properties.end() && own->second.index() == property.value.index();
    ScriptValue value = overridden ? own->second : property.value;
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.5f);
    if (editScriptValue(property.name, value)) {
      if (value == property.value)
        script.properties.erase(property.name); // back at the script's value: nothing to keep
      else
        script.properties[property.name] = value;
    }
    if (overridden) {
      ImGui::SameLine();
      if (ImGui::SmallButton("reset"))
        script.properties.erase(property.name);
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Back to the script's value");
    }
    ImGui::PopID();
  }
}

void drawReflectedField(const ReflectedField &field, void *component) {
  void *data = field.access(component);
  switch (field.kind) {
  case FieldKind::Float: ImGui::DragFloat(field.name.c_str(), static_cast<float *>(data), 0.05f); break;
  case FieldKind::Int: ImGui::DragInt(field.name.c_str(), static_cast<int *>(data)); break;
  case FieldKind::Bool: ImGui::Checkbox(field.name.c_str(), static_cast<bool *>(data)); break;
  case FieldKind::String: editString(field.name.c_str(), *static_cast<std::string *>(data)); break;
  case FieldKind::Vec2: editFloats(field.name, &static_cast<glm::vec2 *>(data)->x, 2); break;
  case FieldKind::Vec3: editFloats(field.name, &static_cast<glm::vec3 *>(data)->x, 3); break;
  case FieldKind::Vec4: editFloats(field.name, &static_cast<glm::vec4 *>(data)->x, 4); break;
  }
}
// The Animation State Machine section: the definition file (typed, or dropped from the asset browser), the state it
// is in, and a widget per parameter, so a character can be driven from here while the game plays
void drawAnimationStateMachine(Engine &engine, SystemManager &systemManager, Entity entity,
                               AnimationStateMachine &machine) {
  static char pathBuffer[256] = "";
  static Entity pathEntity = -1;
  static std::string shownPath;
  if (pathEntity != entity || shownPath != machine.path) {
    pathEntity = entity;
    shownPath = machine.path;
    std::strncpy(pathBuffer, machine.path.c_str(), sizeof(pathBuffer) - 1);
    pathBuffer[sizeof(pathBuffer) - 1] = '\0';
  }
  ImGui::SetNextItemWidth(200.0f);
  std::optional<std::string> path;
  if (ImGui::InputText("File##statemachine", pathBuffer, sizeof(pathBuffer), ImGuiInputTextFlags_EnterReturnsTrue))
    path = pathBuffer;
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Relative to the assets folder, e.g. animators/character.json (Enter applies); drop a file from the Assets panel here");
  if (ImGui::BeginDragDropTarget()) {
    const ImGuiPayload *asset = ImGui::GetDragDropPayload();
    if (asset && asset->IsDataType(AssetBrowser::kPayload) && asset->Data &&
        AssetBrowser::classify(static_cast<const char *>(asset->Data)) == AssetBrowser::Kind::Animator &&
        ImGui::AcceptDragDropPayload(AssetBrowser::kPayload))
      path = static_cast<const char *>(asset->Data);
    ImGui::EndDragDropTarget();
  }
  if (path) {
    engine.setAnimator(entity, *path);
    // setAnimator with "" removed the component
    if (!engine.has<AnimationStateMachine>(entity))
      return;
  }

  auto &animationSystem = systemManager.getSystem<AnimationSystem>();
  const std::shared_ptr<const AnimationGraph> graph = animationSystem.graph(machine.path);
  if (machine.path.empty()) {
    ImGui::TextDisabled("No file yet");
    return;
  }
  if (!graph) {
    ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "The file did not load (see the log)");
    return;
  }

  ImGui::Text("State: %s", engine.getAnimationState(entity).c_str());
  if (machine.manual) {
    ImGui::SameLine();
    ImGui::TextDisabled("(a clip was played by hand)");
  }
  ImGui::SameLine();
  if (ImGui::SmallButton("Restart##statemachine"))
    engine.setAnimator(entity, machine.path);
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Back to the default state with the parameters at their initial values");

  for (const auto &[name, param] : graph->params) {
    float &value = machine.params[name];
    const std::string label = name + "##param";
    if (param.source != AnimationGraph::Source::None) {
      ImGui::TextDisabled("%s: %.2f (from the character)", name.c_str(), value);
    } else if (param.type == AnimationGraph::ParamType::Float) {
      ImGui::DragFloat(label.c_str(), &value, 0.05f);
    } else if (param.type == AnimationGraph::ParamType::Bool) {
      bool flag = value != 0.0f;
      if (ImGui::Checkbox(label.c_str(), &flag))
        value = flag ? 1.0f : 0.0f;
    } else {
      if (ImGui::Button((name + "##trigger").c_str()))
        value = 1.0f;
      ImGui::SameLine();
      ImGui::TextDisabled(value != 0.0f ? "trigger (raised)" : "trigger");
    }
  }
}

} // namespace

void EditorUISystem::renderPropertiesPanel(Engine &engine, SystemManager &systemManager,
                                           ComponentManager &componentManager) {
  auto &renderSystem = systemManager.getSystem<RenderSystem>();
  auto &resourceSystem = systemManager.getSystem<ResourceSystem>();
  auto *window = systemManager.getSystem<WindowSystem>().getWindow();
  ImGuiIO &io = ImGui::GetIO();

  // Docked at the right, the whole height
  bool propOpen = true;
  ImGui::Begin("Properties", &propOpen);
  if (selectedEntity == -1) {
    ImGui::TextDisabled("Nothing selected");
    ImGui::End();
    return;
  }

  if (!propOpen) {
    selectOnly(-1);
    ImGui::End();
    return;
  }

  // --- All early-out actions (Remove buttons, Delete entity) set this flag.
  //     We NEVER return early from inside BeginChild/End — we always let the
  //     stack unwind cleanly first.
  bool earlyOut = false;

  ImGui::BeginChild("PropertiesScroll", ImVec2(0, 0), false, ImGuiWindowFlags_None);

  if (!earlyOut) {
    auto *nc = componentManager.tryGet<Name>(selectedEntity);
    if (nc) {
      char buf[64];
      std::strncpy(buf, nc->name.c_str(), sizeof(buf) - 1);
      buf[sizeof(buf) - 1] = '\0';
      if (ImGui::InputText("Name", buf, sizeof(buf))) {
        nc->name = buf;
      }
    }
    ImGui::Text("Entity ID: %d", selectedEntity);
    if (m_selection.size() > 1) {
      ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "%zu selected; showing the last one clicked", m_selection.size());
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Position, rotation and scale edits here apply to all of them (as changes);\n"
                          "the gizmo moves, turns and scales them together");
    }
    renderParentAndPrefab(systemManager, componentManager);
    renderPrefabLink(engine, systemManager, componentManager);
    ImGui::Separator();
  }

  // Transform
  if (!earlyOut && componentManager.has<Transform>(selectedEntity)) {
    bool open = ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen);
    if (open) {
      if (ImGui::SmallButton("Remove##transform")) {
        componentManager.removeComponent<Transform>(selectedEntity);
        earlyOut = true;
      }
      if (!earlyOut) {
        ImGui::Separator();
        auto &t = componentManager.get<Transform>(selectedEntity);
        if (!componentManager.has<Camera>(selectedEntity))
          renderGizmoControls();
        const Transform editedFrom = t;
        const bool movedField = ImGui::DragFloat3("Position##t", &t.position.x, 0.1f);
        const bool turnedField = ImGui::DragFloat3("Rotation##t", &t.rotation.x, 0.1f);
        const bool scaledField = ImGui::DragFloat3("Scale##t", &t.scale.x, 0.01f);
        // With several selected, the same change goes to the others (the roots, which carry their children)
        if ((movedField || turnedField || scaledField) && m_selection.size() > 1)
          for (Entity other : selectionRoots(componentManager)) {
            Transform *otherTransform = componentManager.tryGet<Transform>(other);
            if (other == selectedEntity || !otherTransform || componentManager.has<Camera>(other))
              continue;
            if (movedField)
              otherTransform->position += t.position - editedFrom.position;
            if (turnedField)
              otherTransform->rotation += t.rotation - editedFrom.rotation;
            if (scaledField)
              otherTransform->scale = glm::max(otherTransform->scale + (t.scale - editedFrom.scale), glm::vec3(0.001f));
          }
      }
    }
  }

  // Camera
  if (!earlyOut && componentManager.has<Camera>(selectedEntity)) {
    earlyOut = renderCameraInspector(selectedEntity, systemManager, componentManager, resourceSystem, window);
  }

  // Tags
  if (!earlyOut && componentManager.has<Tags>(selectedEntity)) {
    if (ImGui::CollapsingHeader("Tags", ImGuiTreeNodeFlags_DefaultOpen)) {
      if (ImGui::SmallButton("Remove##tags")) {
        componentManager.removeComponent<Tags>(selectedEntity);
        earlyOut = true;
      }
      if (!earlyOut) {
        ImGui::Separator();
        auto &tags = componentManager.get<Tags>(selectedEntity).tags;
        int removeIndex = -1;
        for (int i = 0; i < static_cast<int>(tags.size()); ++i) {
          ImGui::PushID(i);
          if (ImGui::SmallButton("x"))
            removeIndex = i;
          ImGui::SameLine();
          ImGui::TextUnformatted(tags[i].c_str());
          ImGui::PopID();
        }
        if (removeIndex >= 0)
          tags.erase(tags.begin() + removeIndex);
        static char newTag[64] = "";
        ImGui::SetNextItemWidth(140.0f);
        const bool enter = ImGui::InputText("##newTag", newTag, sizeof(newTag), ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        if ((ImGui::Button("Add tag##tags") || enter) && newTag[0] != '\0') {
          if (std::find(tags.begin(), tags.end(), std::string(newTag)) == tags.end())
            tags.push_back(newTag);
          newTag[0] = '\0';
        }
      }
    }
  }

  // Camera rig: follows another entity (only while the free camera is off, which the panel says)
  if (!earlyOut && componentManager.has<CameraRig>(selectedEntity)) {
    if (ImGui::CollapsingHeader("Camera Rig", ImGuiTreeNodeFlags_DefaultOpen)) {
      if (ImGui::SmallButton("Remove##rig")) {
        componentManager.removeComponent<CameraRig>(selectedEntity);
        earlyOut = true;
      }
      if (!earlyOut) {
        ImGui::Separator();
        auto &rig = componentManager.get<CameraRig>(selectedEntity);
        editString("Target (name)##rig", rig.target);
        ImGui::DragFloat3("Offset##rig", &rig.offset.x, 0.05f);
        if (ImGui::IsItemHovered())
          ImGui::SetTooltip("x: to the target's right, y: up, z: behind it");
        ImGui::DragFloat("Look ahead##rig", &rig.lookAhead, 0.05f, -20.0f, 20.0f, "%.2f");
        ImGui::DragFloat("Position smoothing##rig", &rig.positionSmoothing, 0.01f, 0.0f, 5.0f, "%.2f s");
        ImGui::DragFloat("Rotation smoothing##rig", &rig.rotationSmoothing, 0.01f, 0.0f, 5.0f, "%.2f s");
        ImGui::Checkbox("Turns with the target##rig", &rig.followRotation);
        ImGui::Checkbox("Orbit (mouse, right stick)##rig", &rig.orbit);
        if (rig.orbit) {
          ImGui::DragFloat("Mouse sensitivity##rig", &rig.orbitSensitivity, 0.005f, 0.01f, 2.0f, "%.3f");
          ImGui::DragFloat("Stick speed##rig", &rig.orbitSpeed, 1.0f, 10.0f, 720.0f, "%.0f deg/s");
        }
        if (!rig.target.empty() && rig.targetEntity == -1)
          ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f), "No entity named \"%s\"", rig.target.c_str());
        if (systemManager.getSystem<StateSystem>().isToggled(Toggle::FreeCamera))
          ImGui::TextDisabled("Off while Free camera is on (Settings)");
      }
    }
  }

  // Light
  if (!earlyOut && componentManager.has<Light>(selectedEntity)) {
    bool open = ImGui::CollapsingHeader("Light", ImGuiTreeNodeFlags_DefaultOpen);
    if (open) {
      if (ImGui::SmallButton("Remove##light")) {
        systemManager.getSystem<LightSystem>().destroyLight(selectedEntity);
        componentManager.removeComponent<Light>(selectedEntity);
        earlyOut = true;
      }
      if (!earlyOut) {
        ImGui::Separator();
        auto &light = componentManager.get<Light>(selectedEntity);
        const char *types[] = {"Directional", "Point", "Spot"};
        int type = static_cast<int>(light.type);
        if (ImGui::Combo("Type##light", &type, types, IM_ARRAYSIZE(types))) {
          light.type = static_cast<LightType>(type);
        }
        ImGui::DragFloat3("Local offset##lp", &light.position.x, 0.1f);
        if (ImGui::IsItemHovered())
          ImGui::SetTooltip("Where the light is, relative to the entity (turned by its rotation)");
        ImGui::DragFloat3("Local direction##ld", &light.direction.x, 0.01f);
        if (ImGui::IsItemHovered())
          ImGui::SetTooltip("Where the light shines, relative to the entity: rotate the entity (or its parent) to aim it. "
                            "Default (0, -1, 0): straight down.");
        ImGui::ColorEdit3("Color##lc", &light.color.x);
        ImGui::DragFloat("Intensity", &light.intensity, 0.1f, 0.0f, 50.0f);

        if (light.type == LightType::Spot) {
          ImGui::DragFloat("Cutoff", &light.cutOff, 0.01f, 0.0f, glm::min(light.outerCutOff - 0.01f, 0.98f));
          ImGui::DragFloat("Outer Cutoff", &light.outerCutOff, 0.01f, light.cutOff + 0.01f, 0.98f);
        }
        if (light.type == LightType::Point || light.type == LightType::Spot) {
          ImGui::Checkbox("Cast shadows##light", &light.castShadows);
          if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Cube shadow map; up to %d lights at once", EngineConfig::MAX_POINT_SHADOWS);
          if (light.castShadows)
            ImGui::DragFloat("Shadow range", &light.shadowRange, 0.5f, 1.0f, 500.0f);
          ImGui::DragFloat("Range", &light.range, 0.1f, 0.0f, 500.0f);
          if (ImGui::IsItemHovered())
            ImGui::SetTooltip("How far the light reaches (world units). It falls off like the inverse square of the "
                              "distance and is exactly 0 at the range.");
        }
      }
    }
  }

  // Model / Material
  if (!earlyOut && componentManager.has<Model>(selectedEntity)) {
    earlyOut = renderMaterialInspector(engine, selectedEntity, componentManager, resourceSystem, renderSystem, window);
  }

  // Particle
  if (!earlyOut && componentManager.has<ParticleEmitter>(selectedEntity)) {
    earlyOut = renderParticleInspector(selectedEntity, componentManager);
  }

  // Collision
  if (!earlyOut && componentManager.has<Collision>(selectedEntity)) {
    bool open = ImGui::CollapsingHeader("Collision (AABB)", ImGuiTreeNodeFlags_DefaultOpen);
    if (open) {
      if (ImGui::SmallButton("Remove##collision")) {
        componentManager.removeComponent<Collision>(selectedEntity);
        earlyOut = true;
      }
      if (!earlyOut) {
        ImGui::Separator();
        auto &col = componentManager.get<Collision>(selectedEntity);
        ImGui::Checkbox("Static", &col.isStatic);
        layerCombo("Layer##col", col.layer, engine.layers());
        ImGui::DragFloat3("Min##col", &col.min.x, 0.05f);
        ImGui::DragFloat3("Max##col", &col.max.x, 0.05f);
      }
    }
  }

  // Audio source
  if (!earlyOut && componentManager.has<AudioSource>(selectedEntity)) {
    bool open = ImGui::CollapsingHeader("Audio Source", ImGuiTreeNodeFlags_DefaultOpen);
    if (open) {
      if (ImGui::SmallButton("Remove##audio")) {
        componentManager.removeComponent<AudioSource>(selectedEntity);
        earlyOut = true;
      }
      if (!earlyOut) {
        ImGui::Separator();
        auto &src = componentManager.get<AudioSource>(selectedEntity);
        // Static: the file dialog fills it after this frame. Applied on Enter or Load, not on every keystroke.
        static char audioPath[256] = "";
        static Entity audioPathEntity = -1;
        if (audioPathEntity != selectedEntity) {
          audioPathEntity = selectedEntity;
          std::strncpy(audioPath, src.path.c_str(), sizeof(audioPath) - 1);
          audioPath[sizeof(audioPath) - 1] = '\0';
        }
        ImGui::SetNextItemWidth(140.0f);
        bool apply = ImGui::InputText("##audioPath", audioPath, sizeof(audioPath),
                                      ImGuiInputTextFlags_EnterReturnsTrue);
        pickFileButton("pick_audio", audioPath, sizeof(audioPath), window);
        ImGui::SameLine();
        if ((ImGui::Button("Load##audio") || apply) && src.path != audioPath) {
          src.path = audioPath;
          src.playing = true;
        }
        ImGui::Checkbox("Playing##audio", &src.playing);
        ImGui::SameLine();
        ImGui::Checkbox("Loop##audio", &src.loop);
        ImGui::SameLine();
        ImGui::Checkbox("Spatial##audio", &src.spatial);
        ImGui::SliderFloat("Volume##audio", &src.volume, 0.0f, 2.0f, "%.2f");
        ImGui::SliderFloat("Pitch##audio", &src.pitch, 0.1f, 3.0f, "%.2f");
        if (src.spatial) {
          ImGui::DragFloat("Min distance##audio", &src.minDistance, 0.1f, 0.01f, src.maxDistance);
          ImGui::DragFloat("Max distance##audio", &src.maxDistance, 0.5f, src.minDistance, 1000.0f);
          ImGui::DragFloat("Rolloff##audio", &src.rolloff, 0.05f, 0.0f, 10.0f);
        }
      }
    }
  }

  // Rigid body
  if (!earlyOut && componentManager.has<RigidBody>(selectedEntity)) {
    if (ImGui::CollapsingHeader("Rigid Body", ImGuiTreeNodeFlags_DefaultOpen)) {
      if (ImGui::SmallButton("Remove##body")) {
        componentManager.removeComponent<RigidBody>(selectedEntity);
        earlyOut = true;
      }
      if (!earlyOut) {
        ImGui::Separator();
        // Edited on a copy: the body is rebuilt when its settings change, so not on every drag step's write
        RigidBody body = componentManager.get<RigidBody>(selectedEntity);
        int type = static_cast<int>(body.type);
        ImGui::Combo("Type##body", &type, "Static\0Dynamic\0Kinematic\0");
        body.type = static_cast<BodyType>(type);
        int shape = static_cast<int>(body.shape);
        ImGui::Combo("Shape##body", &shape, "Box\0Sphere\0Capsule\0Convex hull\0Mesh\0");
        body.shape = static_cast<BodyShape>(shape);
        ImGui::DragFloat3("Size##body", &body.size.x, 0.05f, 0.0f, 1000.0f, "%.2f");
        if (ImGui::IsItemHovered())
          ImGui::SetTooltip("All zero: the model's bounds");
        if (body.type == BodyType::Dynamic) {
          ImGui::DragFloat("Mass##body", &body.mass, 0.1f, 0.0f, 100000.0f, body.mass > 0.0f ? "%.1f kg" : "from volume");
          ImGui::SliderFloat("Gravity##body", &body.gravityScale, -2.0f, 4.0f, "%.2fx");
          ImGui::Checkbox("Lock rotation##body", &body.lockRotation);
        }
        ImGui::SliderFloat("Friction##body", &body.friction, 0.0f, 2.0f, "%.2f");
        ImGui::SliderFloat("Bounce##body", &body.restitution, 0.0f, 1.0f, "%.2f");
        ImGui::Checkbox("Sensor##body", &body.sensor);
        if (ImGui::IsItemHovered())
          ImGui::SetTooltip("Reports contacts but lets things pass through");
        layerCombo("Layer##body", body.layer, engine.layers());
        componentManager.get<RigidBody>(selectedEntity) = body;
      }
    }
  }

  // Joint
  if (!earlyOut && componentManager.has<Joint>(selectedEntity)) {
    if (ImGui::CollapsingHeader("Joint", ImGuiTreeNodeFlags_DefaultOpen)) {
      if (ImGui::SmallButton("Remove##joint")) {
        componentManager.removeComponent<Joint>(selectedEntity);
        earlyOut = true;
      }
      if (!earlyOut) {
        ImGui::Separator();
        // Edited on a copy, like the body: the constraint is built again only when what it is made of changes
        Joint joint = componentManager.get<Joint>(selectedEntity);
        drawJoint(engine, selectedEntity, joint);
        componentManager.get<Joint>(selectedEntity) = joint;
      }
    }
  }

  // Script
  if (!earlyOut && componentManager.has<Script>(selectedEntity)) {
    if (ImGui::CollapsingHeader("Script", ImGuiTreeNodeFlags_DefaultOpen)) {
      if (ImGui::SmallButton("Remove##script")) {
        componentManager.removeComponent<Script>(selectedEntity);
        earlyOut = true;
      }
      if (!earlyOut) {
        ImGui::Separator();
        auto &script = componentManager.get<Script>(selectedEntity);
        static char scriptPath[256] = "";
        static Entity scriptPathEntity = -1;
        if (scriptPathEntity != selectedEntity) {
          scriptPathEntity = selectedEntity;
          std::strncpy(scriptPath, script.path.c_str(), sizeof(scriptPath) - 1);
          scriptPath[sizeof(scriptPath) - 1] = '\0';
        }
        ImGui::SetNextItemWidth(200.0f);
        if (ImGui::InputText("##scriptPath", scriptPath, sizeof(scriptPath), ImGuiInputTextFlags_EnterReturnsTrue))
          script.path = scriptPath;
        if (ImGui::IsItemHovered())
          ImGui::SetTooltip("Relative to the assets folder, e.g. scripts/coin.lua (Enter applies)");
        // The file on disk: open it in the system's editor for .lua files, or create it from a skeleton
        std::string file = script.path;
        if (!file.empty() && file.rfind("../", 0) != 0 && !std::filesystem::path(file).is_absolute())
          file = "../assets/" + file;
        file = EngineConfig::resolvePath(file);
        ImGui::SameLine();
        if (std::filesystem::exists(file)) {
          if (ImGui::Button("Open##script"))
            openInEditor(file);
        } else if (!script.path.empty() && ImGui::Button("Create##script")) {
          std::filesystem::create_directories(std::filesystem::path(file).parent_path());
          std::ofstream out(file);
          out << "-- " << std::filesystem::path(file).filename().string() << "\n\n"
              << "function onStart(self)\nend\n\n"
              << "function onUpdate(self, dt)\nend\n";
          out.close();
          openInEditor(file);
        }
        ImGui::TextDisabled("Saved changes to the file reload while the game runs");
        drawScriptProperties(script, engine.scripts().getProperties(selectedEntity));
      }
    }
  }

  // Animator
  if (!earlyOut && componentManager.has<Animator>(selectedEntity)) {
    if (ImGui::CollapsingHeader("Animator", ImGuiTreeNodeFlags_DefaultOpen)) {
      if (ImGui::SmallButton("Remove##anim")) {
        componentManager.removeComponent<Animator>(selectedEntity);
        earlyOut = true;
      }
      if (!earlyOut) {
        ImGui::Separator();
        auto &animator = componentManager.get<Animator>(selectedEntity);
        const Model *model = componentManager.tryGet<Model>(selectedEntity);
        const MeshAnimation *animation =
            model ? systemManager.getSystem<ResourceSystem>().getMesh(model->meshHandle).getAnimation() : nullptr;
        if (!animation || animation->clips.empty()) {
          ImGui::TextDisabled("Its model has no animations");
        } else {
          const int current = animator.clip.empty() ? 0 : animation->findClip(animator.clip);
          const char *preview = current >= 0 ? animation->clips[current].name.c_str() : animator.clip.c_str();
          if (ImGui::BeginCombo("Clip##anim", preview)) {
            for (size_t i = 0; i < animation->clips.size(); ++i)
              if (ImGui::Selectable(animation->clips[i].name.c_str(), static_cast<int>(i) == current)) {
                animator.clip = animation->clips[i].name;
                animator.time = 0.0f;
                animator.playing = true;
              }
            ImGui::EndCombo();
          }
          const float duration = current >= 0 ? animation->clips[current].duration : 0.0f;
          ImGui::SliderFloat("Time##anim", &animator.time, 0.0f, std::max(duration, 0.01f), "%.2f s");
          if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Drag to scrub through the clip (pause it first to hold a pose)");
          ImGui::SliderFloat("Speed##anim", &animator.speed, -2.0f, 3.0f, "%.2fx");
          ImGui::Checkbox("Playing##anim", &animator.playing);
          ImGui::SameLine();
          ImGui::Checkbox("Loop##anim", &animator.loop);
        }
      }
    }
  }

  // Animation State Machine
  if (!earlyOut && componentManager.has<AnimationStateMachine>(selectedEntity)) {
    if (ImGui::CollapsingHeader("Animation State Machine", ImGuiTreeNodeFlags_DefaultOpen)) {
      if (ImGui::SmallButton("Remove##statemachine")) {
        componentManager.removeComponent<AnimationStateMachine>(selectedEntity);
        earlyOut = true;
      }
      if (!earlyOut) {
        ImGui::Separator();
        drawAnimationStateMachine(engine, systemManager, selectedEntity,
                                  componentManager.get<AnimationStateMachine>(selectedEntity));
      }
    }
  }

  // Character controller
  if (!earlyOut && componentManager.has<CharacterController>(selectedEntity)) {
    if (ImGui::CollapsingHeader("Character Controller", ImGuiTreeNodeFlags_DefaultOpen)) {
      if (ImGui::SmallButton("Remove##character")) {
        componentManager.removeComponent<CharacterController>(selectedEntity);
        earlyOut = true;
      }
      if (!earlyOut) {
        ImGui::Separator();
        auto &character = componentManager.get<CharacterController>(selectedEntity);
        ImGui::DragFloat("Radius##character", &character.radius, 0.01f, 0.05f, 5.0f, "%.2f");
        ImGui::DragFloat("Height##character", &character.height, 0.01f, 0.1f, 10.0f, "%.2f");
        ImGui::SliderFloat("Max slope##character", &character.maxSlope, 0.0f, 89.0f, "%.0f deg");
        ImGui::DragFloat("Step height##character", &character.stepHeight, 0.01f, 0.0f, 2.0f, "%.2f");
        layerCombo("Layer##character", character.layer, engine.layers());
        ImGui::Text("On ground: %s   speed: %.2f", character.onGround ? "yes" : "no", glm::length(character.velocity));
      }
    }
  }

  // Skybox
  if (!earlyOut && componentManager.has<Skybox>(selectedEntity)) {
    if (ImGui::CollapsingHeader("Skybox / Environment", ImGuiTreeNodeFlags_DefaultOpen)) {
      if (ImGui::SmallButton("Remove##sky")) {
        componentManager.removeComponent<Skybox>(selectedEntity);
        earlyOut = true;
      }
      if (!earlyOut) {
        ImGui::Separator();
        auto &sky = componentManager.get<Skybox>(selectedEntity);
        // Applied on Enter or Load, like the audio path
        static char skyPath[256] = "";
        static Entity skyPathEntity = -1;
        if (skyPathEntity != selectedEntity) {
          skyPathEntity = selectedEntity;
          std::strncpy(skyPath, sky.path.c_str(), sizeof(skyPath) - 1);
          skyPath[sizeof(skyPath) - 1] = '\0';
        }
        ImGui::SetNextItemWidth(140.0f);
        bool apply = ImGui::InputText("##skyPath", skyPath, sizeof(skyPath), ImGuiInputTextFlags_EnterReturnsTrue);
        pickFileButton("pick_sky", skyPath, sizeof(skyPath), window);
        ImGui::SameLine();
        if (ImGui::Button("Load##sky") || apply)
          sky.path = skyPath;
        ImGui::DragFloat("Intensity##sky", &sky.intensity, 0.02f, 0.0f, 20.0f, "%.2f");
        ImGui::SliderFloat("Rotation##sky", &sky.rotation, -180.0f, 180.0f, "%.0f deg");
        ImGui::ColorEdit3("Sky color##sky", &sky.color.x); // the background; the panorama draws over it

        ImGui::Separator();
        ImGui::Checkbox("Fog##sky", &sky.fog.enabled);
        if (ImGui::IsItemHovered())
          ImGui::SetTooltip("Lit things blend toward the fog color with distance from the camera");
        if (sky.fog.enabled) {
          int mode = static_cast<int>(sky.fog.mode);
          ImGui::Combo("Mode##fog", &mode, "Linear\0Exponential\0");
          sky.fog.mode = static_cast<FogMode>(mode);
          ImGui::Checkbox("Match sky color##fog", &sky.fog.matchSky);
          if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Distant things fade into the sky color; a panorama is not matched, pick its color");
          if (!sky.fog.matchSky)
            ImGui::ColorEdit3("Fog color##fog", &sky.fog.color.x);
          if (sky.fog.mode == FogMode::Linear) {
            ImGui::DragFloat("Start##fog", &sky.fog.start, 0.5f, 0.0f, 10000.0f, "%.1f");
            ImGui::DragFloat("Finish##fog", &sky.fog.finish, 0.5f, 0.0f, 10000.0f, "%.1f");
          } else {
            ImGui::DragFloat("Density##fog", &sky.fog.density, 0.0005f, 0.0f, 1.0f, "%.4f");
          }
        }
        ImGui::Text("Ambient occlusion");
        ImGui::DragFloat("Radius##ssao", &sky.ssaoRadius, 0.01f, SsaoSettings::kMinRadius, SsaoSettings::kMaxRadius,
                         "%.2f");
        ImGui::DragFloat("Intensity##ssao", &sky.ssaoIntensity, 0.01f, 0.0f, SsaoSettings::kMaxIntensity, "%.2f");
        if (ImGui::IsItemHovered())
          ImGui::SetTooltip("How far geometry shades a point (world units) and how dark; saved with the scene. "
                            "Whether it is on is the player's setting.");
        ImGui::DragFloat("Sky lighting##environment", &sky.environmentIntensity, 0.01f, 0.0f, 10.0f, "%.2f");
        if (ImGui::IsItemHovered())
          ImGui::SetTooltip("How strongly the sky lights things: diffuse light and reflections from the panorama "
                            "(evenly from the sky color without one); 0 turns it off");
        ImGui::Text("Ambient light");
        ImGui::ColorEdit3("Color##ambient", &sky.ambientColor.x);
        ImGui::DragFloat("Intensity##ambient", &sky.ambientIntensity, 0.01f, 0.0f, 5.0f, "%.2f");
        if (ImGui::IsItemHovered())
          ImGui::SetTooltip("Light added to everything, from nowhere (color times intensity); 0 adds nothing");
      }
    }
  }

  // Reflection probe
  if (!earlyOut && componentManager.has<ReflectionProbe>(selectedEntity)) {
    if (ImGui::CollapsingHeader("Reflection Probe", ImGuiTreeNodeFlags_DefaultOpen)) {
      if (ImGui::SmallButton("Remove##probe")) {
        componentManager.removeComponent<ReflectionProbe>(selectedEntity);
        earlyOut = true;
      }
      if (!earlyOut)
        drawReflectionProbe(engine, selectedEntity, componentManager.get<ReflectionProbe>(selectedEntity));
    }
  }

  // The game's own components, registered with Engine::registerComponent
  for (const ReflectedComponent &reflected : engine.getReflectedComponents()) {
    void *component = earlyOut ? nullptr : reflected.get(componentManager, selectedEntity);
    if (!component)
      continue;
    ImGui::PushID(reflected.name.c_str());
    if (ImGui::CollapsingHeader(reflected.name.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
      if (ImGui::SmallButton("Remove")) {
        reflected.remove(componentManager, selectedEntity);
        earlyOut = true;
      } else {
        ImGui::Separator();
        for (const ReflectedField &field : reflected.fields)
          drawReflectedField(field, component);
      }
    }
    ImGui::PopID();
  }

  // Add Component section
  if (!earlyOut) {
    ImGui::Separator();
    if (ImGui::CollapsingHeader("Add Component", ImGuiTreeNodeFlags_None)) {
      if (!componentManager.has<Transform>(selectedEntity)) {
        if (ImGui::Button("+ Transform", ImVec2(-1, 0))) {
          componentManager.addInPlace<Transform>(selectedEntity);
        }
      }
      if (!componentManager.has<Light>(selectedEntity)) {
        if (ImGui::Button("+ Light", ImVec2(-1, 0))) {
          componentManager.addInPlace<Light>(selectedEntity);
          systemManager.getSystem<LightSystem>().createLight(selectedEntity);
        }
      }
      if (!componentManager.has<ParticleEmitter>(selectedEntity)) {
        if (ImGui::Button("+ ParticleEmitter Emitter", ImVec2(-1, 0))) {
          componentManager.addInPlace<ParticleEmitter>(selectedEntity);
        }
      }
      if (!componentManager.has<Collision>(selectedEntity)) {
        if (ImGui::Button("+ Collision (AABB)", ImVec2(-1, 0))) {
          componentManager.addInPlace<Collision>(selectedEntity);
        }
      }
      if (!componentManager.has<AudioSource>(selectedEntity)) {
        if (ImGui::Button("+ Audio Source", ImVec2(-1, 0))) {
          // Stopped until a file is chosen
          auto &src = componentManager.addInPlace<AudioSource>(selectedEntity);
          src.playing = false;
        }
      }
      if (!componentManager.has<RigidBody>(selectedEntity) && !componentManager.has<CharacterController>(selectedEntity)) {
        if (ImGui::Button("+ Rigid Body", ImVec2(-1, 0)))
          componentManager.addInPlace<RigidBody>(selectedEntity);
        if (ImGui::Button("+ Character Controller", ImVec2(-1, 0)))
          componentManager.addInPlace<CharacterController>(selectedEntity);
      }
      if (!componentManager.has<Joint>(selectedEntity) && componentManager.has<RigidBody>(selectedEntity)) {
        if (ImGui::Button("+ Joint", ImVec2(-1, 0)))
          componentManager.addInPlace<Joint>(selectedEntity);
      }
      if (!componentManager.has<Script>(selectedEntity)) {
        if (ImGui::Button("+ Script", ImVec2(-1, 0))) {
          std::string name = "entity";
          if (const Name *entityName = componentManager.tryGet<Name>(selectedEntity))
            name = entityName->name;
          std::transform(name.begin(), name.end(), name.begin(),
                         [](unsigned char c) { return std::isalnum(c) ? static_cast<char>(std::tolower(c)) : '_'; });
          componentManager.addInPlace<Script>(selectedEntity, "scripts/" + name + ".lua");
        }
      }
      if (!componentManager.has<Animator>(selectedEntity)) {
        const Model *model = componentManager.tryGet<Model>(selectedEntity);
        if (model && systemManager.getSystem<ResourceSystem>().getMesh(model->meshHandle).getAnimation() &&
            ImGui::Button("+ Animator", ImVec2(-1, 0)))
          componentManager.addInPlace<Animator>(selectedEntity);
      }
      if (!componentManager.has<AnimationStateMachine>(selectedEntity)) {
        const Model *model = componentManager.tryGet<Model>(selectedEntity);
        if (model && systemManager.getSystem<ResourceSystem>().getMesh(model->meshHandle).getAnimation() &&
            ImGui::Button("+ Animation State Machine", ImVec2(-1, 0)))
          componentManager.addInPlace<AnimationStateMachine>(selectedEntity);
      }
      if (!componentManager.has<Skybox>(selectedEntity)) {
        if (ImGui::Button("+ Skybox / Environment", ImVec2(-1, 0))) {
          componentManager.addInPlace<Skybox>(selectedEntity);
        }
      }
      if (!componentManager.has<ReflectionProbe>(selectedEntity)) {
        if (ImGui::Button("+ Reflection Probe", ImVec2(-1, 0))) {
          if (!componentManager.has<Transform>(selectedEntity))
            componentManager.addInPlace<Transform>(selectedEntity);
          componentManager.addInPlace<ReflectionProbe>(selectedEntity);
        }
      }
      if (!componentManager.has<Model>(selectedEntity)) {
        static char addModelPath[256] = "";
        static Entity addModelEntity = -1;
        if (addModelEntity != selectedEntity) {
          addModelEntity = selectedEntity;
          addModelPath[0] = '\0';
        }
        ImGui::SetNextItemWidth(140.0f);
        ImGui::InputText("##addModelPath", addModelPath, 256);
        pickFileButton("pick_add_model", addModelPath, 256, window);
        ImGui::SameLine();
        if (ImGui::Button("+ Model", ImVec2(-1, 0)) && addModelPath[0] != '\0') {
          if (!componentManager.has<Transform>(selectedEntity)) {
            componentManager.addInPlace<Transform>(selectedEntity);
          }
          auto &rs = resourceSystem;
          uint32_t shaderHandle = rs.getMaterial(0).getShaderHandle();
          auto modelData = rs.loadModel(addModelPath, shaderHandle);
          componentManager.addInPlace<Model>(selectedEntity, modelData.meshHandle,
                                             std::move(modelData.materialHandles), 1.0f);
          systemManager.getSystem<RenderSystem>().markBatchesDirty();
          addModelPath[0] = '\0';
        }
      }
      if (!componentManager.has<Camera>(selectedEntity)) {
        if (ImGui::Button("+ Camera", ImVec2(-1, 0))) {
          componentManager.addInPlace<Camera>(selectedEntity);
        }
      }
      if (!componentManager.has<Tags>(selectedEntity)) {
        if (ImGui::Button("+ Tags", ImVec2(-1, 0)))
          componentManager.addInPlace<Tags>(selectedEntity);
      }
      if (componentManager.has<Camera>(selectedEntity) && !componentManager.has<CameraRig>(selectedEntity)) {
        if (ImGui::Button("+ Camera Rig", ImVec2(-1, 0)))
          componentManager.addInPlace<CameraRig>(selectedEntity);
      }
      for (const ReflectedComponent &reflected : engine.getReflectedComponents()) {
        if (reflected.add && !reflected.get(componentManager, selectedEntity) &&
            ImGui::Button(("+ " + reflected.name).c_str(), ImVec2(-1, 0)))
          reflected.add(componentManager, selectedEntity);
      }
    }

    ImGui::Separator();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.15f, 0.15f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.9f, 0.2f, 0.2f, 1.0f));
    const std::string deleteLabel =
        m_selection.size() > 1 ? "Delete " + std::to_string(m_selection.size()) + " Entities" : "Delete Entity";
    if (ImGui::Button(deleteLabel.c_str(), ImVec2(-1, 0))) {
      deleteSelected(engine, systemManager, componentManager);
      earlyOut = true;
    }
    ImGui::PopStyleColor(2);
  }

  // Always close BeginChild and End before any return
  ImGui::EndChild();
  ImGui::End();
}

// Returns true if the component was removed (caller should set earlyOut).
bool EditorUISystem::renderCameraInspector(Entity entity, SystemManager &systemManager,
                                           ComponentManager &componentManager, ResourceSystem & /*resourceSystem*/,
                                           SDL_Window * /*window*/) {

  if (!ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen))
    return false;

  auto &camSys = systemManager.getSystem<CameraSystem>();
  if (ImGui::SmallButton("Remove##ce")) {
    if (camSys.getActiveCamera() == entity) {
      camSys.removeActiveCamera();
    }
    componentManager.removeComponent<Camera>(entity);
    return true;
  }

  ImGui::Separator();
  auto &cam = componentManager.get<Camera>(entity);

  bool camToogle = (camSys.getActiveCamera() == entity);

  ImGui::Checkbox("Active##ce", &camToogle);
  ImGui::SameLine();
  ImGui::Checkbox("Relative to Transform##ce", &cam.isRelative);
  ImGui::DragFloat3("Position##ce", &cam.position.x, 0.1f);
  ImGui::DragFloat("FOV##ce", &cam.fov, 0.5f, 30.0f, 120.0f);
  ImGui::DragFloat("Move Speed##ce", &cam.moveSpeed, 0.1f, 0.5f, 20.0f);
  ImGui::DragFloat("Sensitivity##ce", &cam.mouseSensitivity, 0.05f, 0.1f, 5.0f);
  ImGui::DragFloat("Yaw##ce", &cam.yaw, 0.5f);
  ImGui::DragFloat("Pitch##ce", &cam.pitch, 0.5f, -89.0f, 89.0f);

  if (camToogle) {
    camSys.setActiveCamera(entity);
  } else if (camSys.getActiveCamera() == entity) {
    camSys.removeActiveCamera();
  }

  return false;
}

// Returns true if the component was removed (caller should set earlyOut).
bool EditorUISystem::renderParticleInspector(Entity entity, ComponentManager &componentManager) {
  if (!ImGui::CollapsingHeader("ParticleEmitter Emitter", ImGuiTreeNodeFlags_DefaultOpen))
    return false;

  if (ImGui::SmallButton("Remove##pe")) {
    componentManager.removeComponent<ParticleEmitter>(entity);
    return true;
  }

  ImGui::Separator();
  auto &p = componentManager.get<ParticleEmitter>(entity);

  ImGui::Checkbox("Active##pe", &p.active);
  ImGui::SameLine();
  ImGui::Checkbox("Additive Blend##pe", &p.additiveBlending);

  ImGui::DragFloat("Emit Rate", &p.emitRate, 1.0f, 1.0f, 10000.0f);
  ImGui::DragFloat("Lifetime", &p.particleLifetime, 0.1f, 0.1f, 100.0f);
  ImGui::DragInt("Max Particles", &p.maxParticles, 1, 10, 50000);
  ImGui::Separator();

  ImGui::Text("Motion");
  int shape = static_cast<int>(p.emissionShape);
  if (ImGui::Combo("Emitter Shape##pe", &shape, "Cone\0Sphere\0Box\0")) {
    p.emissionShape = static_cast<EmissionShape>(shape);
  }

  if (p.emissionShape == EmissionShape::Sphere) {
    ImGui::DragFloat("Sphere Radius##pe", &p.sphereRadius, 0.05f, 0.0f, 100.0f);
  } else if (p.emissionShape == EmissionShape::Box) {
    ImGui::DragFloat3("Box Half Extents##pe", &p.boxHalfExtents.x, 0.05f, 0.0f, 100.0f);
  }

  ImGui::DragFloat("Speed", &p.speed, 0.1f, 0.0f, 100.0f);
  ImGui::DragFloat("Speed Variance", &p.speedVariance, 0.1f, 0.0f, 50.0f);

  if (p.emissionShape != EmissionShape::Sphere) {
    ImGui::DragFloat3("Direction##pe", &p.emitDirection.x, 0.01f);
    ImGui::DragFloat("Spread", &p.spread, 0.01f, 0.0f, 10.0f);
  }

  ImGui::DragFloat3("Offset##pe", &p.offset.x, 0.05f);
  ImGui::DragFloat3("Gravity##pe", &p.gravity.x, 0.05f);
  ImGui::Checkbox("Emit Tangentially##pe", &p.emitTangentially);
  ImGui::DragFloat("Tangent Variance##pe", &p.tangentVariance, 0.01f, 0.0f, 10.0f);
  ImGui::DragFloat("Friction##pe", &p.friction, 0.01f, 0.0f, 5.0f);
  ImGui::Separator();

  ImGui::Text("Appearance");
  ImGui::DragFloat("Size##pe", &p.size, 0.01f, 0.01f, 10.0f);
  ImGui::DragFloat("Size Decay", &p.sizeDecay, 0.1f, 0.0f, 20.0f);
  ImGui::ColorEdit4("Start Color", &p.startColor.x);
  ImGui::ColorEdit4("End Color", &p.endColor.x);
  ImGui::Checkbox("Distance-Based Color##pe", &p.distBasedColor);
  ImGui::DragFloat("Color Radius##pe", &p.colorRadius, 1.0f, 0.1f, 500.0f);
  ImGui::Separator();

  ImGui::Text("Attraction");
  ImGui::Checkbox("Attract Mode##pe", &p.attractMode);
  ImGui::SameLine();
  ImGui::Checkbox("Attract Point Relative##pe", &p.attractPointRelative);
  ImGui::Checkbox("Enable Recycling##pe", &p.enableRecycling);
  ImGui::DragFloat3("Attract Point##pe", &p.attractPoint.x, 0.1f);
  ImGui::DragFloat("Attract Strength##pe", &p.attractStrength, 0.05f, 0.0f, 100.0f);
  ImGui::DragFloat("Tangent Strength##pe", &p.tangentStrength, 0.050f, 0.0f, 100.0f);
  ImGui::DragFloat("Reset Radius##pe", &p.resetRadius, 0.1f, 0.0f, 500.0f);
  ImGui::Separator();

  int alive = static_cast<int>(p.particles.size());
  ImGui::Text("Alive: %d / %d", alive, p.maxParticles);
  ImGui::ProgressBar(static_cast<float>(alive) / static_cast<float>(p.maxParticles));

  return false;
}

// Returns true if the component was removed (caller should set earlyOut).
namespace {
// A text field that shows `current` until you type in it; true (with the text) when Enter was pressed. The ID stack
// tells fields apart, so call it inside the PushID of whatever it belongs to.
bool pathField(const char *label, const std::string &current, std::string &typed) {
  struct State {
    std::array<char, 256> text{};
    std::string seen;
  };
  static std::unordered_map<ImGuiID, State> states;
  State &state = states[ImGui::GetID(label)];
  if (state.seen != current) {
    state.seen = current;
    std::snprintf(state.text.data(), state.text.size(), "%s", current.c_str());
  }
  const bool entered = ImGui::InputText(label, state.text.data(), state.text.size(), ImGuiInputTextFlags_EnterReturnsTrue);
  typed = state.text.data();
  return entered;
}

// The last item takes a file from the asset browser of this kind: its path (relative to assets/) when dropped
std::optional<std::string> acceptAssetDrop(AssetBrowser::Kind kind) {
  std::optional<std::string> dropped;
  if (ImGui::BeginDragDropTarget()) {
    const ImGuiPayload *asset = ImGui::GetDragDropPayload();
    if (asset && asset->IsDataType(AssetBrowser::kPayload) && asset->Data &&
        AssetBrowser::classify(static_cast<const char *>(asset->Data)) == kind &&
        ImGui::AcceptDragDropPayload(AssetBrowser::kPayload))
      dropped = static_cast<const char *>(asset->Data);
    ImGui::EndDragDropTarget();
  }
  return dropped;
}

bool nameHasColor(const std::string &name) {
  std::string lower = name;
  std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });
  return lower.find("color") != std::string::npos || lower.find("colour") != std::string::npos;
}
} // namespace

bool EditorUISystem::renderMaterialInspector(Engine &engine, Entity entity, ComponentManager &componentManager,
                                             ResourceSystem &resourceSystem, RenderSystem &renderSystem,
                                             SDL_Window *window) {
  if (!ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen))
    return false;

  if (ImGui::SmallButton("Remove##model")) {
    componentManager.removeComponent<Model>(entity);
    renderSystem.markBatchesDirty();
    return true;
  }

  ImGui::Separator();
  auto &model = componentManager.get<Model>(entity);
  const Mesh &mesh = resourceSystem.getMesh(model.meshHandle);
  const auto &submeshes = mesh.getSubmeshes();

  const char *labels[5] = {"Diffuse", "Normal", "Emission", "Metal/Rough", "Occlusion"};
  const TextureSlot slots[5] = {TextureSlot::Diffuse, TextureSlot::Normal, TextureSlot::Emission,
                                TextureSlot::MetallicRoughness, TextureSlot::Occlusion};
  // The texture a part has in a slot, as an asset path (empty: none, or a solid color)
  const auto texturePath = [&](size_t part, int slot) -> std::string {
    if (part >= model.materialHandles.size() || model.materialHandles[part] == 0)
      return {};
    const Material &material = resourceSystem.getMaterial(model.materialHandles[part]);
    const GLuint texture = slot == 0 ? material.getDiffuse()
                           : slot == 1 ? material.getNormal()
                           : slot == 2 ? material.getEmission()
                           : slot == 3 ? material.getMetallicRoughness()
                                       : material.getOcclusion();
    const std::string path = texture ? resourceSystem.getTexturePath(texture) : std::string();
    if (path.empty() || path.rfind("solid_", 0) == 0)
      return {};
    std::string relative = EngineConfig::relativePath(path);
    if (relative.rfind(EngineConfig::ASSETS_PREFIX, 0) == 0)
      relative.erase(0, EngineConfig::ASSETS_PREFIX.size());
    return relative;
  };
  // The text fields show the texture each part has; what is typed stays until the texture changes (another entity
  // selected, Set, X, undo), when they show the new one. The "all" fields show a texture all the parts share.
  static char globalPaths[5][256] = {};
  static std::array<std::string, 5> globalShown{};
  static std::array<std::string, 5> globalTried{}; // the last path applied from an "all" field, not tried again
  static std::vector<std::array<char[256], 5>> paths;
  static std::vector<std::array<std::string, 5>> shown;
  static Entity textureEntity = -1;
  if (textureEntity != entity || paths.size() != model.materialHandles.size()) {
    textureEntity = entity;
    paths.assign(model.materialHandles.size(), {});
    shown.assign(model.materialHandles.size(), {});
    for (auto &all : globalShown)
      all = "\x01"; // matches no path, so every field is filled below
    for (auto &tried : globalTried)
      tried.clear();
    for (auto &part : shown)
      part.fill("\x01");
  }
  const auto show = [](char *field, std::string &seen, const std::string &now) {
    if (seen == now)
      return false;
    seen = now;
    std::snprintf(field, 256, "%s", now.c_str());
    return true;
  };
  for (size_t i = 0; i < paths.size(); ++i)
    for (int t = 0; t < 5; ++t)
      show(paths[i][t], shown[i][t], texturePath(i, t));
  for (int t = 0; t < 5; ++t) {
    std::string shared = texturePath(0, t);
    for (size_t i = 1; i < model.materialHandles.size() && !shared.empty(); ++i)
      if (texturePath(i, t) != shared)
        shared.clear();
    if (show(globalPaths[t], globalShown[t], shared))
      globalTried[t].clear(); // the texture changed: any path may be applied again
  }

  ImGui::Text("%zu parts", submeshes.size());

  // Global texture apply
  if (ImGui::TreeNode("Apply to All Submeshes")) {
    if (ImGui::DragFloat("Opacity", &model.opacity, 0.01f, 0.0f, 1.0f, "%.2f")) {
      renderSystem.markBatchesDirty();
    }

    for (int t = 0; t < 5; t++) {
      ImGui::Text("%s", labels[t]);
      ImGui::SetNextItemWidth(140.0f);
      ImGui::InputText((std::string("##global_") + labels[t]).c_str(), globalPaths[t], 256);
      const bool editing = ImGui::IsItemActive();
      pickFileButton((std::string("pick_g_") + labels[t]).c_str(), globalPaths[t], 256, window);
      ImGui::SameLine();

      // Applied when the field has a new path and is not being typed in (after picking a file, or leaving the field)
      const std::string typedPath(globalPaths[t]);
      if (!editing && !typedPath.empty() && typedPath != globalShown[t] && typedPath != globalTried[t]) {
        engine.beginCheckpoint();
        resourceSystem.setTexture(entity, -1, slots[t], typedPath);
        globalTried[t] = typedPath;
      }

      if (ImGui::Button((std::string("X##g_") + labels[t]).c_str())) {
        engine.beginCheckpoint();
        resourceSystem.removeTexture(entity, -1, slots[t]);
      }
    }

    ImGui::Separator();
    ImGui::Text("Shader (All Submeshes)");
    {
      ImGui::PushID("shader_all");
      const std::string current = model.materialHandles.empty() ? std::string()
                                                                  : resourceSystem.getShader(entity, 0);
      std::string typed;
      std::optional<std::string> dropped;
      ImGui::SetNextItemWidth(ui(180.0f));
      const bool entered = pathField("##path", current, typed);
      dropped = acceptAssetDrop(AssetBrowser::Kind::Shader);
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("A .glsl custom surface shader, e.g. shaders/examples/water.glsl; drop one from the asset browser");
      ImGui::SameLine();
      const bool reset = ImGui::Button("Reset");
      if (dropped || entered || reset) {
        engine.beginCheckpoint();
        resourceSystem.setShader(entity, -1, reset ? std::string() : (dropped ? *dropped : typed));
        renderSystem.markBatchesDirty();
      }
      ImGui::PopID();
    }

    ImGui::Separator();
    ImGui::Text("Emission (All Submeshes)");
    static glm::vec3 globalEmCol{0.0f};
    static float globalEmStr = 0.0f;
    ImGui::ColorEdit3("Emission Color##global", &globalEmCol.x);
    ImGui::DragFloat("Emission Strength##global", &globalEmStr, 0.1f, 0.0f, 50.0f);
    if (ImGui::Button("Apply Emission##global", ImVec2(-1, 0))) {
      resourceSystem.setEmission(entity, -1, globalEmCol, globalEmStr);
    }

    ImGui::Separator();
    ImGui::Text("Solid Color (All Submeshes)");
    static glm::vec3 globalFallbackColor{1.0f, 1.0f, 1.0f};
    ImGui::ColorEdit3("##global_fallback_color", &globalFallbackColor.x);
    if (ImGui::Button("Apply Solid Color to All##global", ImVec2(-1, 0))) {
      resourceSystem.setSolidColor(entity, -1, globalFallbackColor);
    }

    ImGui::TreePop();
  }

  // Per-submesh
  for (size_t i = 0; i < model.materialHandles.size(); ++i) {
    ImGui::PushID(static_cast<int>(i));
    const std::string &partName = i < submeshes.size() ? submeshes[i].name : std::string();
    // A part with a custom shader starts open, so its parameters are in sight
    const bool hasShader = model.materialHandles[i] != 0 && !resourceSystem.getMaterial(model.materialHandles[i]).getShaderPath().empty();
    if (ImGui::TreeNodeEx((std::to_string(i) + ": " + (partName.empty() ? "part" : partName)).c_str(),
                          hasShader ? ImGuiTreeNodeFlags_DefaultOpen : ImGuiTreeNodeFlags_None)) {
      uint32_t &handle = model.materialHandles[i];
      Material *matPtr = (handle != 0) ? &resourceSystem.getMaterial(handle) : nullptr;
      ImGui::Text("Material Handle: %u", handle);

      for (int t = 0; t < 5; t++) {
        ImGui::Text("%s", labels[t]);
        ImGui::SetNextItemWidth(140.0f);
        ImGui::InputText((std::string("##sub") + std::to_string(i) + "_" + labels[t]).c_str(), paths[i][t], 256);
        pickFileButton((std::string("pick_s") + std::to_string(i) + "_" + labels[t]).c_str(), paths[i][t], 256, window);
        ImGui::SameLine();
        if (ImGui::Button((std::string("Set##") + labels[t]).c_str())) {
          engine.beginCheckpoint();
          resourceSystem.setTexture(entity, static_cast<int>(i), slots[t], paths[i][t]);
        }
        ImGui::SameLine();
        if (ImGui::Button((std::string("X##") + labels[t]).c_str())) {
          engine.beginCheckpoint();
          resourceSystem.removeTexture(entity, static_cast<int>(i), slots[t]);
        }
      }

      ImGui::Separator();
      ImGui::Text("Shader");
      {
        const std::string current = matPtr ? matPtr->getShaderPath() : std::string();
        std::string typed;
        ImGui::SetNextItemWidth(ui(180.0f));
        const bool entered = pathField("##shaderPath", current, typed);
        const std::optional<std::string> dropped = acceptAssetDrop(AssetBrowser::Kind::Shader);
        if (ImGui::IsItemHovered())
          ImGui::SetTooltip("A .glsl custom surface shader, e.g. shaders/examples/water.glsl; drop one from the asset browser");
        ImGui::SameLine();
        const bool reset = ImGui::Button("Reset##shader");
        if (dropped || entered || reset) {
          engine.beginCheckpoint();
          resourceSystem.setShader(entity, static_cast<int>(i), reset ? std::string() : (dropped ? *dropped : typed));
          renderSystem.markBatchesDirty();
          matPtr = handle != 0 ? &resourceSystem.getMaterial(handle) : nullptr;
        }

        // The shader's own uniforms, found by reading its file
        if (matPtr && !matPtr->getShaderPath().empty()) {
          const CustomShader *custom = resourceSystem.findCustomShader(matPtr->getShaderPath());
          if (!custom || !custom->ok) {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Does not compile (see the log): drawn with the standard shader");
          } else {
            for (const ShaderParamDecl &decl : custom->code.params) {
              ImGui::PushID(decl.name.c_str());
              const ShaderValue now = resourceSystem.getShaderParam(entity, static_cast<int>(i), decl.name)
                                          .value_or(shaderParamDefault(decl));
              const char *label = decl.name.c_str();
              bool changed = false;
              ShaderValue edited = now;
              switch (decl.type) {
              case ShaderParamType::Float: {
                float v = shaderValueAsVec4(now).x;
                changed = ImGui::DragFloat(label, &v, 0.01f);
                edited = v;
                break;
              }
              case ShaderParamType::Vec2: {
                glm::vec2 v(shaderValueAsVec4(now));
                changed = ImGui::DragFloat2(label, &v.x, 0.01f);
                edited = v;
                break;
              }
              case ShaderParamType::Vec3: {
                glm::vec3 v(shaderValueAsVec4(now));
                changed = nameHasColor(decl.name) ? ImGui::ColorEdit3(label, &v.x, ImGuiColorEditFlags_Float)
                                                  : ImGui::DragFloat3(label, &v.x, 0.01f);
                edited = v;
                break;
              }
              case ShaderParamType::Vec4: {
                glm::vec4 v = shaderValueAsVec4(now);
                changed = nameHasColor(decl.name) ? ImGui::ColorEdit4(label, &v.x, ImGuiColorEditFlags_Float)
                                                  : ImGui::DragFloat4(label, &v.x, 0.01f);
                edited = v;
                break;
              }
              case ShaderParamType::Texture: {
                std::string typedTexture;
                const std::string *shown = std::get_if<std::string>(&now);
                ImGui::SetNextItemWidth(ui(180.0f));
                changed = pathField("##texture", shown ? *shown : std::string(), typedTexture);
                const std::optional<std::string> droppedTexture = acceptAssetDrop(AssetBrowser::Kind::Texture);
                ImGui::SameLine();
                const bool clear = ImGui::Button("X");
                ImGui::SameLine();
                ImGui::TextUnformatted(label);
                if (droppedTexture || clear)
                  typedTexture = clear ? std::string() : *droppedTexture;
                changed = changed || droppedTexture || clear;
                edited = typedTexture;
                break;
              }
              }
              if (changed) {
                engine.beginCheckpoint();
                resourceSystem.setShaderParam(entity, static_cast<int>(i), decl.name, edited);
              }
              ImGui::PopID();
            }
          }
        }
      }

      ImGui::Separator();
      float metallic = matPtr ? matPtr->getMetallic() : 0.0f;
      if (ImGui::SliderFloat("Metallic", &metallic, 0.0f, 1.0f))
        resourceSystem.setMetallic(entity, static_cast<int>(i), metallic);
      float roughness = matPtr ? matPtr->getRoughness() : 0.5f;
      if (ImGui::SliderFloat("Roughness", &roughness, 0.0f, 1.0f))
        resourceSystem.setRoughness(entity, static_cast<int>(i), roughness);
      bool unlit = matPtr && matPtr->isUnlit();
      if (ImGui::Checkbox("Unlit", &unlit))
        resourceSystem.setUnlit(entity, static_cast<int>(i), unlit);

      ImGui::Separator();
      ImGui::Text("Manual Emission");
      glm::vec3 emCol = matPtr ? matPtr->getEmissionColor() : glm::vec3(0.0f);
      if (ImGui::ColorEdit3("Emission Color", &emCol.x)) {
        resourceSystem.setEmission(entity, i, emCol, matPtr ? matPtr->getEmissionStrength() : 0.0f);
      }
      float emStr = matPtr ? matPtr->getEmissionStrength() : 0.0f;
      if (ImGui::DragFloat("Emission Strength", &emStr, 0.1f, 0.0f, 50.0f)) {
        resourceSystem.setEmission(entity, i, matPtr ? matPtr->getEmissionColor() : glm::vec3(0.0f), emStr);
      }

      ImGui::TreePop();
    }
    ImGui::PopID();
  }

  return false;
}

void EditorUISystem::renderAddEntityPopup(Engine &engine, SceneSystem &sceneSystem, SystemManager &systemManager,
                                          ComponentManager &componentManager, ResourceSystem &resourceSystem,
                                          RenderSystem &renderSystem, SDL_Window *window) {
  if (!ImGui::BeginPopup("AddEntityPopup"))
    return;

  ImGui::SetNextItemWidth(300.0f);
  ImGui::Dummy(ImVec2(300.0f, 0.0f));

  // A prefab: copies of a saved entity and its children, placed in front of the camera
  {
    static char prefabFile[256] = "../assets/prefabs/";
    ImGui::TextUnformatted("Prefab");
    ImGui::SetNextItemWidth(200.0f);
    ImGui::InputText("##prefabFile", prefabFile, sizeof(prefabFile));
    pickFileButton("pick_prefab", prefabFile, sizeof(prefabFile), window);
    ImGui::SameLine();
    if (ImGui::Button("Place##prefab")) {
      glm::vec3 position(0.0f);
      if (const Camera *camera = engine.getActiveCamera())
        position = camera->position + glm::normalize(camera->front) * 5.0f;
      const Entity placed = engine.instantiate(prefabFile, position);
      if (placed != -1) {
        selectOnly(placed);
        m_scrollToSelection = true;
        ImGui::CloseCurrentPopup();
      }
    }
    ImGui::Separator();
  }

  // Single unified create dialog with per-component checkboxes and options
  static char entityName[64] = "";
  static bool addModel = false;
  static bool addLight = false;
  static bool addParticle = false;
  static bool addCamera = false;
  static bool addCollision = false;
  static bool addProbe = false;

  // Transform defaults
  static float tpos[3] = {0, 0, 0};
  static float trot[3] = {0, 0, 0};
  static float tscale[3] = {1, 1, 1};

  // Model options
  static char modelPath[256] = "";
  static float modelOpacity = 1.0f;
  static float modelSolidColor[3] = {1.0f, 1.0f, 1.0f};
  static bool modelUseSolidTexture = false;

  // Light options
  static float lpos[3] = {0, 0, 0};
  static float ldir[3] = {0, -1, 0};
  static float lcolor[3] = {1, 1, 1};
  static int ltype = 0;
  static float lintensity = 1.0f;
  static float lcut = 0.207911f;
  static float louter = 0.139173f;

  // Particle options
  static char emitterName[64] = "Emitter";
  static float ppos[3] = {0, 0, 0};
  static float pemitRate = 40.0f;
  static float plifetime = 2.0f;
  static float pspeed = 2.0f;
  static float pspread = 0.5f;
  static float pstartCol[4] = {1, 0.8f, 0.2f, 1};
  static float pendCol[4] = {1, 0, 0, 0};

  // Camera options
  static char camName[64] = "New Camera";
  static float cpos[3] = {0, 0, 0};
  static float cyaw = 0;
  static float cpitch = 0;
  static float cfov = 60.0f;
  static bool cisActive = false;

  ImGui::InputText("Name", entityName, 64);

  ImGui::Separator();
  ImGui::Checkbox("Model", &addModel);
  ImGui::SameLine();
  ImGui::Checkbox("Light", &addLight);
  ImGui::SameLine();
  ImGui::Checkbox("ParticleEmitter", &addParticle);
  ImGui::SameLine();
  ImGui::Checkbox("Camera", &addCamera);
  ImGui::Checkbox("Collision (AABB)", &addCollision);
  ImGui::SameLine();
  ImGui::Checkbox("Reflection Probe", &addProbe);

  // Transform is mandatory
  ImGui::Text("Transform");
  ImGui::InputFloat3("Position##create", tpos);
  ImGui::InputFloat3("Rotation##create", trot);
  ImGui::InputFloat3("Scale##create", tscale);
  ImGui::Separator();

  if (addModel) {
    ImGui::Text("Model");
    ImGui::InputText("Model Path", modelPath, 256);
    pickFileButton("pick_model_path", modelPath, 256, window);
    ImGui::DragFloat("Opacity##create", &modelOpacity, 0.01f, 0.0f, 1.0f, "%.2f");
    ImGui::ColorEdit3("Solid Color##create", modelSolidColor);
    ImGui::Checkbox("Use Solid Texture", &modelUseSolidTexture);
    ImGui::Separator();
  }

  if (addLight) {
    ImGui::Text("Light");
    ImGui::InputFloat3("Local offset##lf", lpos);
    ImGui::InputFloat3("Local direction##lf", ldir);
    ImGui::ColorEdit3("Color##lf", lcolor);
    ImGui::Combo("Type##lf", &ltype, "Directional\0Point\0Spot\0");
    ImGui::DragFloat("Intensity##lf", &lintensity, 0.1f, 0.0f, 50.0f);
    if (ltype == 2) {
      ImGui::DragFloat("Cutoff##lf", &lcut, 0.01f, 0.0f, glm::min(louter - 0.01f, 0.98f));
      ImGui::DragFloat("Outer Cutoff##lf", &louter, 0.01f, lcut + 0.01f, 0.98f);
    }
    ImGui::Separator();
  }

  if (addParticle) {
    ImGui::Text("ParticleEmitter");
    ImGui::InputText("Name##pf", emitterName, 64);
    ImGui::InputFloat3("Position##pf", ppos);
    ImGui::DragFloat("Emit Rate##pf", &pemitRate, 1.0f, 1, 10000);
    ImGui::DragFloat("Lifetime##pf", &plifetime, 0.1f, 0.1f, 100);
    ImGui::DragFloat("Speed##pf", &pspeed, 0.1f, 0, 100);
    ImGui::DragFloat("Spread##pf", &pspread, 0.01f, 0, 10);
    ImGui::ColorEdit4("Start Color##pf", pstartCol);
    ImGui::ColorEdit4("End Color##pf", pendCol);
    ImGui::Separator();
  }

  if (addCamera) {
    ImGui::Text("Camera");
    ImGui::InputText("Name##cf", camName, 64);
    ImGui::InputFloat3("Position##cf", cpos);
    ImGui::InputFloat("Yaw##cf", &cyaw);
    ImGui::InputFloat("Pitch##cf", &cpitch);
    ImGui::SliderFloat("FOV##cf", &cfov, 30.0f, 120.0f);
    ImGui::Checkbox("Active##cf", &cisActive);
    ImGui::Separator();
  }

  if (ImGui::Button("Create", ImVec2(120, 0))) {
    const std::string finalName = entityName[0] != '\0' ? std::string(entityName) : std::string("Entity");
    auto builder = engine.entities().create(finalName);
    const bool createModel = addModel && modelPath[0] != '\0';

    // Transform is mandatory
    builder.withTransform(glm::vec3(tpos[0], tpos[1], tpos[2]), glm::vec3(trot[0], trot[1], trot[2]),
                          glm::vec3(tscale[0], tscale[1], tscale[2]));

    // Model
    if (createModel) {
      uint32_t shaderHandle = resourceSystem.getMaterial(0).getShaderHandle();
      builder.withModel(modelPath, glm::vec3(tpos[0], tpos[1], tpos[2]), glm::vec3(trot[0], trot[1], trot[2]),
                        glm::vec3(tscale[0], tscale[1], tscale[2]), shaderHandle);
    }

    // Light
    if (addLight) {
      builder.withComponent<Light>(static_cast<LightType>(ltype), glm::vec3(lpos[0], lpos[1], lpos[2]),
                                   glm::vec3(ldir[0], ldir[1], ldir[2]), glm::vec3(lcolor[0], lcolor[1], lcolor[2]),
                                   lintensity, lcut, louter);
    }

    // Particle
    if (addParticle) {
      builder.withComponent<ParticleEmitter>();
    }

    // Collision
    if (addCollision) {
      builder.withCollision(glm::vec3(tscale[0], tscale[1], tscale[2]));
    }

    // Camera
    if (addCamera) {
      builder.withComponent<Camera>();
    }

    // Reflection probe: its box is the default size, around the entity
    if (addProbe) {
      builder.withComponent<ReflectionProbe>();
    }

    Entity e = builder.build();

    if (createModel) {
      auto &model = componentManager.get<Model>(e);
      model.opacity = modelOpacity;
      renderSystem.markBatchesDirty();
      if (modelUseSolidTexture) {
        resourceSystem.setSolidColor(e, -1, glm::vec3(modelSolidColor[0], modelSolidColor[1], modelSolidColor[2]));
      }
    }

    if (addLight) {
      systemManager.getSystem<LightSystem>().createLight(e);
    }

    if (addParticle) {
      auto &p = componentManager.get<ParticleEmitter>(e);
      p.emitRate = pemitRate;
      p.particleLifetime = plifetime;
      p.speed = pspeed;
      p.spread = pspread;
      p.startColor = glm::vec4(pstartCol[0], pstartCol[1], pstartCol[2], pstartCol[3]);
      p.endColor = glm::vec4(pendCol[0], pendCol[1], pendCol[2], pendCol[3]);
    }

    if (addCamera) {
      auto &cam = componentManager.get<Camera>(e);
      cam.position = glm::vec3(cpos[0], cpos[1], cpos[2]);
      cam.yaw = cyaw;
      cam.pitch = cpitch;
      cam.fov = cfov;
      if (cisActive)
        systemManager.getSystem<CameraSystem>().setActiveCamera(e);
    }

    selectOnly(e);

    // Reset some fields
    memset(entityName, 0, sizeof(entityName));
    ImGui::CloseCurrentPopup();
  }
  ImGui::SameLine();
  if (ImGui::Button("Cancel", ImVec2(120, 0))) {
    ImGui::CloseCurrentPopup();
  }

  ImGui::EndPopup();
}