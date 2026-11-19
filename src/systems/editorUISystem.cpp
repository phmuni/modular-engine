// Editor UI implementation: the ImGui scene explorer, properties panel and settings panel (F2), apart from the game
// UI that games build with RmlUi (GameUISystem).

#include "systems/editorUISystem.h"
#include "components/animator.h"
#include "components/audioSource.h"
#include "components/collision.h"
#include "components/hidden.h"
#include "components/light.h"
#include "components/model.h"
#include "components/name.h"
#include "components/parent.h"
#include "components/particleEmitter.h"
#include "components/skybox.h"
#include "components/characterController.h"
#include "components/rigidBody.h"
#include "components/transform.h"
#include "foundation/core/engine.h"
#include "systems/modelQueries.h"
#include "rendering/resources/material.h"
#include "rendering/resources/mesh.h"
#include "rendering/resources/textureUploader.h"
#include "systems/audioSystem.h"
#include "systems/sceneSerializer.h"
#include "systems/cameraSystem.h"
#include "systems/lightSystem.h"
#include "systems/renderSystem.h"
#include "systems/resourceSystem.h"
#include "systems/sceneSystem.h"
#include "systems/stateSystem.h"
#include "systems/timeSystem.h"
#include "systems/transformSystem.h"
#include "systems/windowSystem.h"

#include <algorithm>
#include <functional>
#include <limits>
#include <array>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/euler_angles.hpp>
#include <glm/gtx/quaternion.hpp>
#include <imgui/imgui.h>
#include <imgui/imgui_impl_opengl3.h>
#include <imgui/imgui_impl_sdl3.h>
#include <ImGuizmo.h> // after imgui.h, which it does not include itself
#include <string>
#include <vector>

EditorUISystem::EditorUISystem(SDL_Window *window, SDL_GLContext glContext) {
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGui::StyleColorsDark();

  ImGui_ImplSDL3_InitForOpenGL(window, glContext);
  ImGui_ImplOpenGL3_Init("#version 330");
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

void EditorUISystem::beginFrame() {
  ImGui_ImplOpenGL3_NewFrame();
  ImGui_ImplSDL3_NewFrame();
  ImGui::NewFrame();
  ImGuizmo::BeginFrame();
}

void EditorUISystem::endFrame() {
  ImGui::Render();
  ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void EditorUISystem::update(SystemManager &systemManager) {
  auto &state = systemManager.getSystem<StateSystem>();
  auto &windowSystem = systemManager.getSystem<WindowSystem>();

  bool cursorLocked = state.isToggled(Toggle::CursorLock);
  windowSystem.setCursor(cursorLocked);

  m_renderUI = state.isToggled(Toggle::ShowUI);
  m_showStats = state.isToggled(Toggle::ShowStats);
}

namespace {
constexpr float kPropertiesWidth = 380.0f;

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

void EditorUISystem::renderStatsOverlay(float rightInset) {
  const ImGuiViewport *viewport = ImGui::GetMainViewport();
  constexpr float kMargin = 10.0f;
  ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x - rightInset - kMargin,
                                 viewport->WorkPos.y + kMargin),
                          ImGuiCond_Always, ImVec2(1.0f, 0.0f));
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

void EditorUISystem::renderSettingsPanel(Engine &engine, EntityManager &entityManager, SystemManager &systemManager,
                                         ComponentManager &componentManager) {
  auto &state = systemManager.getSystem<StateSystem>();
  auto &timeSystem = systemManager.getSystem<TimeSystem>();
  auto &renderSystem = systemManager.getSystem<RenderSystem>();
  auto &cameraSystem = systemManager.getSystem<CameraSystem>();

  // Starts next to the Scene Explorer; movable and collapsible
  ImGui::SetNextWindowPos(ImVec2(290.0f, 10.0f), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(340.0f, 0.0f), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("Settings")) {
    ImGui::End();
    return;
  }
  // Leave room for slider labels
  ImGui::PushItemWidth(-ImGui::GetFontSize() * 7.0f);

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

  if (ImGui::CollapsingHeader("Performance", ImGuiTreeNodeFlags_DefaultOpen)) {
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

  if (ImGui::CollapsingHeader("Rendering", ImGuiTreeNodeFlags_DefaultOpen)) {
    toggleCheckbox("Wireframe", Toggle::Wireframe);
    toggleCheckbox("Shadows", Toggle::Shadows);
    toggleCheckbox("Gamma-correct lighting", Toggle::GammaCorrect, "Lights in linear space; off is the old look");
    toggleCheckbox("Bloom", Toggle::Bloom, "Glow around light brighter than white (emission, highlights)");
    toggleCheckbox("Game UI debugger", Toggle::UIDebugger, "RmlUi's element inspector and log for the game UI");
  }

  if (ImGui::CollapsingHeader("Simulation", ImGuiTreeNodeFlags_DefaultOpen)) {
    toggleCheckbox("Paused", Toggle::Paused, "Freezes particles and game logic; the camera keeps moving");
    float timeScale = timeSystem.getTimeScale();
    if (ImGui::SliderFloat("Time scale", &timeScale, 0.0f, 4.0f, "%.2fx"))
      timeSystem.setTimeScale(timeScale);
    if (ImGui::SmallButton("Reset to 1x"))
      timeSystem.setTimeScale(1.0f);
  }

  if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen)) {
    toggleCheckbox("Free camera", Toggle::CameraMovement,
                   "WASD + mouse while the cursor is locked; examples that drive the camera let go of it meanwhile");
    if (state.isToggled(Toggle::CameraMovement) && !state.isToggled(Toggle::CursorLock)) {
      const char *lockKey = toggleKeyName(Toggle::CursorLock);
      ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "  Lock the cursor (%s) to look around", lockKey ? lockKey : "Cursor lock");
    }
    toggleCheckbox("Fly mode", Toggle::CameraFly, "Space and Shift move up and down");
    toggleCheckbox("Cursor lock", Toggle::CursorLock);

    Entity cameraEntity = cameraSystem.getActiveCamera();
    if (auto *camera = cameraEntity != -1 ? componentManager.tryGet<Camera>(cameraEntity) : nullptr) {
      ImGui::SliderFloat("FOV", &camera->fov, 20.0f, 120.0f, "%.0f deg");
      ImGui::SliderFloat("Move speed", &camera->moveSpeed, 0.5f, 50.0f, "%.1f", ImGuiSliderFlags_Logarithmic);
      ImGui::SliderFloat("Sensitivity", &camera->mouseSensitivity, 0.1f, 5.0f, "%.2f");
    } else {
      ImGui::TextDisabled("No active camera");
    }
  }

  if (ImGui::CollapsingHeader("Scene", ImGuiTreeNodeFlags_DefaultOpen)) {
    // Follows the engine's current scene, so Save writes back to the file that was loaded
    static char scenePath[256] = "../assets/scenes/untitled.json";
    static std::string followedPath;
    if (!engine.getScenePath().empty() && engine.getScenePath() != followedPath) {
      followedPath = engine.getScenePath();
      std::string shown = EngineConfig::relativePath(EngineConfig::resolvePath(followedPath));
      std::strncpy(scenePath, shown.c_str(), sizeof(scenePath) - 1);
      scenePath[sizeof(scenePath) - 1] = '\0';
    }
    ImGui::InputText("File##scene", scenePath, sizeof(scenePath));
    // Listed again every second, so files saved meanwhile show up
    static std::vector<std::string> scenes;
    static float listAge = 1.0f;
    listAge += ImGui::GetIO().DeltaTime;
    if (ImGui::Button("Save##scene")) {
      engine.saveScene(scenePath);
      listAge = 1.0f;
    }
    ImGui::SameLine();
    if (ImGui::Button("Load##scene"))
      engine.loadScene(scenePath);
    if (listAge >= 1.0f) {
      scenes = systemManager.getSystem<SceneSerializer>().listScenes(EngineConfig::SCENES_DIR);
      listAge = 0.0f;
    }
    if (scenes.empty())
      ImGui::TextDisabled("No scenes in %s", EngineConfig::SCENES_DIR);
    std::string current = engine.getScenePath().empty()
                              ? std::string()
                              : EngineConfig::relativePath(EngineConfig::resolvePath(engine.getScenePath()));
    for (const std::string &scene : scenes) {
      std::string label = std::filesystem::path(scene).filename().string();
      if (ImGui::Selectable(label.c_str(), scene == current))
        engine.loadScene(scene);
    }
  }

  if (ImGui::CollapsingHeader("Audio", ImGuiTreeNodeFlags_DefaultOpen)) {
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

  if (ImGui::CollapsingHeader("Capture", ImGuiTreeNodeFlags_DefaultOpen)) {
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

namespace {
template <typename T> std::vector<Entity> entitiesWith(ComponentManager &componentManager) {
  std::vector<Entity> result;
  componentManager.forEachComponent<T>([&](Entity entity, T &) { result.push_back(entity); });
  return result;
}
} // namespace

void EditorUISystem::render(Engine &engine, EntityManager &entityManager, SystemManager &systemManager,
                            ComponentManager &componentManager) {
  if (m_showStats)
    renderStatsOverlay(m_renderUI && selectedEntity != -1 ? kPropertiesWidth : 0.0f);

  if (!m_renderUI)
    return;

  renderSettingsPanel(engine, entityManager, systemManager, componentManager);
  renderSceneExplorer(engine, systemManager, componentManager);
  renderPlayToolbar(engine);
  m_gizmoShown = false;
  if (selectedEntity != -1) {
    renderPropertiesPanel(systemManager, componentManager);
    drawSelectionBox(systemManager, componentManager);
    renderGizmo(systemManager, componentManager);
  }
  handleClickSelection(engine, systemManager);
}

void EditorUISystem::handleClickSelection(Engine &engine, SystemManager &systemManager) {
  const ImGuiIO &io = ImGui::GetIO();
  if (!ImGui::IsMouseClicked(ImGuiMouseButton_Left) || io.WantCaptureMouse ||
      systemManager.getSystem<StateSystem>().isToggled(Toggle::CursorLock))
    return;
  if (m_gizmoShown && (ImGuizmo::IsOver() || ImGuizmo::IsUsing()))
    return; // the click grabs the gizmo
  if (m_resizeHovered || m_resize.axis >= 0)
    return; // the click grabs a resize handle
  const auto hit = engine.pickModel(glm::vec2(io.MousePos.x, io.MousePos.y));
  selectedEntity = hit ? hit->entity : -1;
  m_scrollToSelection = hit.has_value();
}

void EditorUISystem::drawSelectionBox(SystemManager &systemManager, ComponentManager &componentManager) {
  const auto *model = componentManager.tryGet<Model>(selectedEntity);
  const auto *transform = componentManager.tryGet<Transform>(selectedEntity);
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

  const ImVec2 size = ImGui::GetIO().DisplaySize;
  auto toScreen = [&](const glm::vec4 &clip) {
    return ImVec2((clip.x / clip.w * 0.5f + 0.5f) * size.x, (0.5f - clip.y / clip.w * 0.5f) * size.y);
  };
  ImDrawList *drawList = ImGui::GetBackgroundDrawList();
  // The 12 edges: corners that differ in one bit
  for (int a = 0; a < 8; ++a)
    for (int bit = 1; bit < 8; bit <<= 1) {
      const int b = a | bit;
      if (b == a || corners[a].w <= 0.01f || corners[b].w <= 0.01f) // skip edges behind the camera
        continue;
      drawList->AddLine(toScreen(corners[a]), toScreen(corners[b]), IM_COL32(255, 200, 60, 200), 1.5f);
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
  ImGui::TextDisabled("[W/E/R/T]");
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
    ImGui::SetTooltip("Dragging the move gizmo places the entity on the surface under the cursor: its bottom on\n"
                      "it and its up axis along the surface's slope, keeping its heading.");
}

void EditorUISystem::renderGizmo(SystemManager &systemManager, ComponentManager &componentManager) {
  // Cameras place themselves from their Camera component, not the Transform the gizmo would move
  auto *transform = componentManager.tryGet<Transform>(selectedEntity);
  if (!transform || componentManager.has<Camera>(selectedEntity))
    return;
  auto &cameraSystem = systemManager.getSystem<CameraSystem>();
  const Entity cameraEntity = cameraSystem.getActiveCamera();
  const Camera *camera = cameraEntity != -1 ? componentManager.tryGet<Camera>(cameraEntity) : nullptr;
  if (!camera)
    return;

  ImGuiIO &io = ImGui::GetIO();
  // Shortcuts, unless a text field has the keyboard (the free camera only reads WASD with the cursor locked)
  if (!io.WantTextInput && !ImGuizmo::IsUsing() && m_resize.axis < 0) {
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
    renderResizeHandles(systemManager, componentManager, *transform, view, projection);
    return;
  }
  m_resize.axis = -1;
  m_resizeHovered = false;
  glm::mat4 model = systemManager.getSystem<TransformSystem>().calculateModelMatrix(*transform);

  const ImGuizmo::OPERATION operation = m_gizmoOperation == GizmoOperation::Move     ? ImGuizmo::TRANSLATE
                                        : m_gizmoOperation == GizmoOperation::Rotate ? ImGuizmo::ROTATE
                                                                                     : ImGuizmo::SCALE;
  // Scaling is always along the entity's own axes
  const ImGuizmo::MODE mode = m_gizmoLocal || operation == ImGuizmo::SCALE ? ImGuizmo::LOCAL : ImGuizmo::WORLD;
  const float snapStep = operation == ImGuizmo::TRANSLATE ? 0.5f : operation == ImGuizmo::ROTATE ? 15.0f : 0.1f;
  const float snap[3] = {snapStep, snapStep, snapStep};

  // Drawn over the whole screen, behind the editor's windows
  ImGuizmo::SetOrthographic(false);
  ImGuizmo::SetDrawlist(ImGui::GetBackgroundDrawList());
  ImGuizmo::SetRect(0.0f, 0.0f, io.DisplaySize.x, io.DisplaySize.y);
  m_gizmoShown = true;
  // The heading kept while aligning to surfaces is the one the entity had when the drag started
  if (!ImGuizmo::IsUsing())
    m_alignHeading = transform->rotation.y;
  if (!ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(projection), operation, mode,
                            glm::value_ptr(model), nullptr, m_gizmoSnap || io.KeyCtrl ? snap : nullptr))
    return;
  // Align to surface: moving follows the cursor over surfaces instead of the gizmo's axes (off any surface, the
  // gizmo moves it as usual)
  if (operation == ImGuizmo::TRANSLATE && m_gizmoAlign && alignToSurface(systemManager, componentManager, *transform))
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
  float yaw = 0.0f, pitch = 0.0f, roll = 0.0f;
  glm::extractEulerAngleYXZ(rotation, yaw, pitch, roll);

  // Only what this operation changes, so moving never nudges the rotation through float round-off
  Transform wanted = *transform;
  if (operation == ImGuizmo::TRANSLATE)
    wanted.position = glm::vec3(model[3]);
  else if (operation == ImGuizmo::ROTATE)
    wanted.rotation = glm::degrees(glm::vec3(pitch, yaw, roll));
  else
    wanted.scale = scale;
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
  const auto *model = componentManager.tryGet<Model>(selectedEntity);
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
    if (entity == selectedEntity || !otherTransform || componentManager.has<Hidden>(entity))
      return;
    const WorldBox box =
        worldBox(resourceSystem.getMesh(other.meshHandle), transformSystem.calculateModelMatrix(*otherTransform));
    if (boxesOverlap(startBox, box) && queries.intersects(selectedEntity, transform, entity)) {
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
        if (!collides && entity != selectedEntity && blocks(entity) && !componentManager.has<Hidden>(entity))
          collides = queries.intersects(selectedEntity, pose, entity);
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
    float fraction = queries.sweep(selectedEntity, pose, delta, blocks).fraction;

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
  const auto *model = componentManager.tryGet<Model>(selectedEntity);
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
  const auto *model = componentManager.tryGet<Model>(selectedEntity);
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
  const auto *model = componentManager.tryGet<Model>(selectedEntity);
  ImGuiIO &io = ImGui::GetIO();
  if (!model || io.DisplaySize.x <= 0.0f || io.DisplaySize.y <= 0.0f) {
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
    screen = ImVec2((clip.x / clip.w * 0.5f + 0.5f) * io.DisplaySize.x, (0.5f - clip.y / clip.w * 0.5f) * io.DisplaySize.y);
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
      if (distance < hoveredDistance && !io.WantCaptureMouse) {
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
  }

  // Drag: the face follows the point of its line nearest to the cursor's ray
  if (m_resize.axis >= 0) {
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
      m_resize.axis = -1;
    } else {
      const glm::vec2 ndc(io.MousePos.x / io.DisplaySize.x * 2.0f - 1.0f, 1.0f - io.MousePos.y / io.DisplaySize.y * 2.0f);
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

bool EditorUISystem::alignToSurface(SystemManager &systemManager, ComponentManager &componentManager,
                                    Transform &transform) {
  const auto *model = componentManager.tryGet<Model>(selectedEntity);
  auto &cameraSystem = systemManager.getSystem<CameraSystem>();
  const Entity cameraEntity = cameraSystem.getActiveCamera();
  const Camera *camera = cameraEntity != -1 ? componentManager.tryGet<Camera>(cameraEntity) : nullptr;
  const ImGuiIO &io = ImGui::GetIO();
  if (!model || !camera || io.DisplaySize.x <= 0.0f || io.DisplaySize.y <= 0.0f)
    return false;

  // The cursor's ray, onto the nearest surface of anything but the entity itself
  const glm::vec2 ndc(io.MousePos.x / io.DisplaySize.x * 2.0f - 1.0f, 1.0f - io.MousePos.y / io.DisplaySize.y * 2.0f);
  const glm::mat4 inverse = glm::inverse(cameraSystem.getProjectionMatrix(*camera) * cameraSystem.getViewMatrix(*camera));
  glm::vec4 nearPoint = inverse * glm::vec4(ndc, -1.0f, 1.0f), farPoint = inverse * glm::vec4(ndc, 1.0f, 1.0f);
  nearPoint /= nearPoint.w;
  farPoint /= farPoint.w;
  auto &resourceSystem = systemManager.getSystem<ResourceSystem>();
  const ModelQueries queries(componentManager, resourceSystem, systemManager.getSystem<TransformSystem>());
  const auto hit = queries.raycast(glm::vec3(nearPoint), glm::vec3(farPoint - nearPoint),
                                   std::numeric_limits<float>::max(),
                                   [&](Entity entity) { return entity != selectedEntity; });
  if (!hit)
    return false;

  // Its up (+Y) along the surface's normal, keeping the heading it had when the drag started
  const glm::vec3 up(0.0f, 1.0f, 0.0f);
  const glm::quat tilt = glm::rotation(up, hit->normal);
  const glm::quat heading = glm::angleAxis(glm::radians(m_alignHeading), up);
  float yaw = 0.0f, pitch = 0.0f, roll = 0.0f;
  glm::extractEulerAngleYXZ(glm::mat4_cast(tilt * heading), yaw, pitch, roll);
  transform.rotation = glm::degrees(glm::vec3(pitch, yaw, roll));
  // Its lowest point (the bottom of its bounds) on the surface
  transform.position = hit->point - hit->normal * (model ? resourceSystem.getMesh(model->meshHandle).getBoundsMin().y *
                                                                transform.scale.y
                                                          : 0.0f);
  return true;
}

namespace {
std::string displayName(ComponentManager &componentManager, Entity entity) {
  const Name *name = componentManager.tryGet<Name>(entity);
  return name && !name->name.empty() ? name->name : "Entity " + std::to_string(entity);
}
} // namespace

void EditorUISystem::dragToParent(SystemManager &systemManager, ComponentManager &componentManager, Entity entity,
                                  const std::string &label) {
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
    ImGui::EndDragDropTarget();
  }
  (void)label;
}

void EditorUISystem::renderHierarchy(SystemManager &systemManager, ComponentManager &componentManager) {
  if (!ImGui::CollapsingHeader("Hierarchy", ImGuiTreeNodeFlags_DefaultOpen))
    return;
  auto &transformSystem = systemManager.getSystem<TransformSystem>();
  ImGui::BeginChild("Hierarchy", ImVec2(0, 120.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeY);

  // Families only: the entities that have a parent or children, from their roots down
  std::vector<Entity> roots;
  componentManager.forEachComponent<Parent>([&](Entity entity, Parent &) {
    Entity root = entity;
    for (int guard = 0; guard < 256 && transformSystem.getParent(componentManager, root) != -1; ++guard)
      root = transformSystem.getParent(componentManager, root);
    if (std::find(roots.begin(), roots.end(), root) == roots.end())
      roots.push_back(root);
  });
  std::sort(roots.begin(), roots.end());

  std::function<void(Entity, int)> node = [&](Entity entity, int depth) {
    const std::vector<Entity> children = transformSystem.getChildren(componentManager, entity);
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_DefaultOpen |
                               ImGuiTreeNodeFlags_SpanAvailWidth;
    if (children.empty())
      flags |= ImGuiTreeNodeFlags_Leaf;
    if (selectedEntity == entity)
      flags |= ImGuiTreeNodeFlags_Selected;
    const std::string label = displayName(componentManager, entity) + "##tree" + std::to_string(entity);
    const bool open = ImGui::TreeNodeEx(label.c_str(), flags);
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
      selectedEntity = entity;
    dragToParent(systemManager, componentManager, entity, label);
    if (open) {
      if (depth < 64)
        for (Entity child : children)
          node(child, depth + 1);
      ImGui::TreePop();
    }
  };
  for (Entity root : roots)
    node(root, 0);
  if (roots.empty())
    ImGui::TextDisabled("  Drag an entity onto another\n  to attach it to it");

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
  if (ImGui::Button("Save as prefab"))
    systemManager.getSystem<SceneSerializer>().savePrefab(selectedEntity, EngineConfig::resolvePath(prefabPath));
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Saves this entity and its children; Add Entity > Prefab places copies of it");
}

void EditorUISystem::renderPlayToolbar(Engine &engine) {
  const ImGuiIO &io = ImGui::GetIO();
  const Engine::PlayMode mode = engine.getPlayMode();

  // Ctrl+P: Play from Editing, Stop while testing, and freeze a running game
  if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_P, false) && !io.WantTextInput) {
    if (mode == Engine::PlayMode::Editing)
      engine.play();
    else if (mode == Engine::PlayMode::Testing)
      engine.stop();
    else
      engine.edit();
  }

  // A small bar at the top right of the view, left of the Properties panel when it is open
  const float right = io.DisplaySize.x - (selectedEntity != -1 ? kPropertiesWidth : 0.0f) - 8.0f;
  ImGui::SetNextWindowPos(ImVec2(right, 6.0f), ImGuiCond_Always, ImVec2(1.0f, 0.0f));
  ImGui::SetNextWindowBgAlpha(0.85f);
  ImGui::Begin("##PlayToolbar", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove |
                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing);
  const ImVec4 green(0.25f, 0.65f, 0.3f, 1.0f), red(0.75f, 0.2f, 0.2f, 1.0f), blue(0.25f, 0.45f, 0.8f, 1.0f);
  auto coloredButton = [](const char *label, const ImVec4 &color) {
    ImGui::PushStyleColor(ImGuiCol_Button, color);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(color.x + 0.1f, color.y + 0.1f, color.z + 0.1f, 1.0f));
    const bool pressed = ImGui::Button(label, ImVec2(70.0f, 0.0f));
    ImGui::PopStyleColor(2);
    return pressed;
  };
  if (mode == Engine::PlayMode::Running) {
    if (coloredButton("Edit", blue))
      engine.edit();
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Freezes the game to edit the scene (Ctrl+P); Play then tests it from there,\n"
                        "and Stop puts the scene back as it was");
    ImGui::SameLine();
    ImGui::TextDisabled("Running");
  } else if (mode == Engine::PlayMode::Editing) {
    if (coloredButton("Play", green))
      engine.play();
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Runs the game from the scene as it is now (Ctrl+P); Stop puts everything back");
    ImGui::SameLine();
    if (ImGui::Button("Resume", ImVec2(70.0f, 0.0f)))
      engine.resume();
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Unfreezes the game and keeps whatever happens (nothing to go back to)");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.55f, 0.75f, 1.0f, 1.0f), "Editing (frozen)");
  } else {
    if (coloredButton("Stop", red))
      engine.stop();
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Puts every entity back as it was at Play (Ctrl+P)");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.45f, 0.9f, 0.5f, 1.0f), "Playing");
  }
  ImGui::End();
}

void EditorUISystem::renderSceneExplorer(Engine &engine, SystemManager &systemManager,
                                         ComponentManager &componentManager) {
  auto &renderSystem = systemManager.getSystem<RenderSystem>();
  auto &lightSystem = systemManager.getSystem<LightSystem>();
  auto &resourceSystem = systemManager.getSystem<ResourceSystem>();
  auto &sceneSystem = systemManager.getSystem<SceneSystem>();
  auto *window = systemManager.getSystem<WindowSystem>().getWindow();
  ImGuiIO &io = ImGui::GetIO();

  ImGui::SetNextWindowPos(ImVec2(0, 0));
  ImGui::SetNextWindowSize(ImVec2(280, io.DisplaySize.y));
  ImGui::Begin("Scene Explorer", nullptr,
               ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);

  ImVec2 avail = ImGui::GetContentRegionAvail();
  float buttonHeight = ImGui::GetFrameHeightWithSpacing();
  ImGui::BeginChild("SceneTree", ImVec2(avail.x, avail.y - buttonHeight), true);

  auto entityLabel = [&](Entity e, const char *icon) -> std::string {
    auto *nc = componentManager.tryGet<Name>(e);
    std::string name;
    if (nc && !nc->name.empty()) {
      name = nc->name;
    } else {
      name = std::string("Entity ") + std::to_string(e);
    }
    return std::string(icon) + " " + name + "##" + std::to_string(e);
  };

  constexpr float kSectionHeight = 120.0f; // initial height
  auto section = [&](const char *header, const char *icon, const std::vector<Entity> &entities) {
    if (!ImGui::CollapsingHeader(header, ImGuiTreeNodeFlags_DefaultOpen))
      return;
    // Scroll area with a stable, user-resizable height so sections keep their size as entities come and go.
    ImGui::BeginChild(header, ImVec2(0, kSectionHeight), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeY);
    for (Entity entity : entities) {
      std::string label = entityLabel(entity, icon);
      if (ImGui::Selectable(label.c_str(), selectedEntity == entity))
        selectedEntity = (selectedEntity == entity) ? -1 : entity;
      dragToParent(systemManager, componentManager, entity, label);
      if (m_scrollToSelection && selectedEntity == entity) {
        ImGui::SetScrollHereY();
        m_scrollToSelection = false;
      }
    }
    if (entities.empty())
      ImGui::TextDisabled("  (empty)");
    ImGui::EndChild();
  };

  renderHierarchy(systemManager, componentManager);
  section("Models", "[M]", entitiesWith<Model>(componentManager));
  section("Lights", "[L]", lightSystem.getLights());
  section("Camera", "[C]", entitiesWith<Camera>(componentManager));
  section("Particles", "[P]", entitiesWith<ParticleEmitter>(componentManager));

  ImGui::EndChild();

  if (ImGui::Button("+ Add Entity", ImVec2(-1, 0))) {
    ImGui::OpenPopup("AddEntityPopup");
  }
  renderAddEntityPopup(engine, sceneSystem, systemManager, componentManager, resourceSystem, renderSystem, window);

  ImGui::End();
}

void EditorUISystem::renderPropertiesPanel(SystemManager &systemManager, ComponentManager &componentManager) {
  auto &renderSystem = systemManager.getSystem<RenderSystem>();
  auto &resourceSystem = systemManager.getSystem<ResourceSystem>();
  auto &sceneSystem = systemManager.getSystem<SceneSystem>();
  auto *window = systemManager.getSystem<WindowSystem>().getWindow();
  ImGuiIO &io = ImGui::GetIO();

  float propW = kPropertiesWidth;
  ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - propW, 0));
  ImGui::SetNextWindowSize(ImVec2(propW, io.DisplaySize.y));
  bool propOpen = true;
  ImGui::Begin("Properties", &propOpen,
               ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);

  if (!propOpen) {
    selectedEntity = -1;
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
    renderParentAndPrefab(systemManager, componentManager);
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
        ImGui::DragFloat3("Position##t", &t.position.x, 0.1f);
        ImGui::DragFloat3("Rotation##t", &t.rotation.x, 0.1f);
        ImGui::DragFloat3("Scale##t", &t.scale.x, 0.01f);
      }
    }
  }

  // Camera
  if (!earlyOut && componentManager.has<Camera>(selectedEntity)) {
    earlyOut = renderCameraInspector(selectedEntity, systemManager, componentManager, resourceSystem, window);
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
        ImGui::DragFloat3("Offset##lp", &light.position.x, 0.1f);
        ImGui::DragFloat3("Direction##ld", &light.direction.x, 0.01f);
        ImGui::ColorEdit3("Color##lc", &light.color.x);
        ImGui::DragFloat("Intensity", &light.intensity, 0.1f, 0.0f, 50.0f);
        ImGui::DragFloat("Ambient", &light.ambient, 0.01f, 0.0f, 1.0f);

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
          ImGui::DragFloat("Constant", &light.constant, 0.01f, 0.01f, 5.0f);
          ImGui::DragFloat("Linear", &light.linear, 0.01f, 0.0f, 1.0f);
          ImGui::DragFloat("Quadratic", &light.quadratic, 0.001f, 0.0f, 1.0f);
        }
      }
    }
  }

  // Model / Material
  if (!earlyOut && componentManager.has<Model>(selectedEntity)) {
    earlyOut = renderMaterialInspector(selectedEntity, componentManager, resourceSystem, renderSystem, window);
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
        componentManager.get<RigidBody>(selectedEntity) = body;
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
        ImGui::Text("On ground: %s   speed: %.2f", character.onGround ? "yes" : "no", glm::length(character.velocity));
      }
    }
  }

  // Skybox
  if (!earlyOut && componentManager.has<Skybox>(selectedEntity)) {
    if (ImGui::CollapsingHeader("Skybox", ImGuiTreeNodeFlags_DefaultOpen)) {
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
      }
    }
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
      if (!componentManager.has<Animator>(selectedEntity)) {
        const Model *model = componentManager.tryGet<Model>(selectedEntity);
        if (model && systemManager.getSystem<ResourceSystem>().getMesh(model->meshHandle).getAnimation() &&
            ImGui::Button("+ Animator", ImVec2(-1, 0)))
          componentManager.addInPlace<Animator>(selectedEntity);
      }
      if (!componentManager.has<Skybox>(selectedEntity)) {
        if (ImGui::Button("+ Skybox", ImVec2(-1, 0))) {
          componentManager.addInPlace<Skybox>(selectedEntity);
        }
      }
      if (!componentManager.has<Model>(selectedEntity)) {
        static char addModelPath[256] = "";
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
    }

    ImGui::Separator();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.15f, 0.15f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.9f, 0.2f, 0.2f, 1.0f));
    if (ImGui::Button("Delete Entity", ImVec2(-1, 0))) {
      sceneSystem.destroyEntity(selectedEntity);
      selectedEntity = -1;
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
bool EditorUISystem::renderMaterialInspector(Entity entity, ComponentManager &componentManager,
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

  static char globalPaths[4][256] = {};
  const char *labels[4] = {"Diffuse", "Specular", "Normal", "Emission"};
  const TextureSlot slots[4] = {TextureSlot::Diffuse, TextureSlot::Specular, TextureSlot::Normal,
                                TextureSlot::Emission};

  ImGui::Text("%zu parts", submeshes.size());

  // Global texture apply
  if (ImGui::TreeNode("Apply to All Submeshes")) {
    static std::array<std::string, 4> lastAppliedPaths{};

    if (ImGui::DragFloat("Opacity", &model.opacity, 0.01f, 0.0f, 1.0f, "%.2f")) {
      renderSystem.markBatchesDirty();
    }

    for (int t = 0; t < 4; t++) {
      ImGui::Text("%s", labels[t]);
      ImGui::SetNextItemWidth(140.0f);
      ImGui::InputText((std::string("##global_") + labels[t]).c_str(), globalPaths[t], 256);
      pickFileButton((std::string("pick_g_") + labels[t]).c_str(), globalPaths[t], 256, window);
      ImGui::SameLine();

      // Auto-apply if path changed after picking
      std::string currentPath(globalPaths[t]);
      if (!currentPath.empty() && currentPath != lastAppliedPaths[t]) {
        resourceSystem.setTexture(entity, -1, slots[t], globalPaths[t]);
        lastAppliedPaths[t] = currentPath;
      }

      if (ImGui::Button((std::string("X##g_") + labels[t]).c_str())) {
        resourceSystem.removeTexture(entity, -1, slots[t]);
        globalPaths[t][0] = '\0';
        lastAppliedPaths[t] = "";
      }
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
  static std::vector<std::array<char[256], 4>> paths;
  if (paths.size() != submeshes.size())
    paths.resize(submeshes.size());

  for (size_t i = 0; i < model.materialHandles.size(); ++i) {
    ImGui::PushID(static_cast<int>(i));
    const std::string &partName = i < submeshes.size() ? submeshes[i].name : std::string();
    if (ImGui::TreeNode((std::to_string(i) + ": " + (partName.empty() ? "part" : partName)).c_str())) {
      uint32_t &handle = model.materialHandles[i];
      Material *matPtr = (handle != 0) ? &resourceSystem.getMaterial(handle) : nullptr;
      ImGui::Text("Material Handle: %u", handle);

      for (int t = 0; t < 4; t++) {
        ImGui::Text("%s", labels[t]);
        ImGui::SetNextItemWidth(140.0f);
        ImGui::InputText((std::string("##sub") + std::to_string(i) + "_" + labels[t]).c_str(), paths[i][t], 256);
        pickFileButton((std::string("pick_s") + std::to_string(i) + "_" + labels[t]).c_str(), paths[i][t], 256, window);
        ImGui::SameLine();
        if (ImGui::Button((std::string("Set##") + labels[t]).c_str())) {
          resourceSystem.setTexture(entity, static_cast<int>(i), slots[t], paths[i][t]);
        }
        ImGui::SameLine();
        if (ImGui::Button((std::string("X##") + labels[t]).c_str())) {
          resourceSystem.removeTexture(entity, static_cast<int>(i), slots[t]);
        }
      }

      float shininess = matPtr ? matPtr->getShininess() : 16.0f;
      if (ImGui::DragFloat("Shininess", &shininess, 1.0f, 1.0f, 256.0f)) {
        resourceSystem.setShininess(entity, static_cast<int>(i), shininess);
      }

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
        selectedEntity = placed;
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
    ImGui::InputFloat3("Position##lf", lpos);
    ImGui::InputFloat3("Direction##lf", ldir);
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

    selectedEntity = e;

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