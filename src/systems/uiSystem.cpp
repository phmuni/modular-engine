// UI system implementation for rendering the ImGui-based user interface, including the scene explorer and properties
// panel.

#include "systems/uiSystem.h"
#include "components/collision.h"
#include "components/light.h"
#include "components/model.h"
#include "components/name.h"
#include "components/particleEmitter.h"
#include "components/transform.h"
#include "foundation/core/engine.h"
#include "rendering/resources/material.h"
#include "rendering/resources/mesh.h"
#include "systems/cameraSystem.h"
#include "systems/lightSystem.h"
#include "systems/renderSystem.h"
#include "systems/resourceSystem.h"
#include "systems/sceneSystem.h"
#include "systems/windowSystem.h"

#include <array>
#include <glm/glm.hpp>
#include <imgui/imgui.h>
#include <imgui/imgui_impl_opengl3.h>
#include <imgui/imgui_impl_sdl3.h>
#include <string>

UISystem::UISystem(SDL_Window *window, SDL_GLContext glContext) {
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGui::StyleColorsDark();

  ImGui_ImplSDL3_InitForOpenGL(window, glContext);
  ImGui_ImplOpenGL3_Init("#version 330");
}

void UISystem::fileDialogCallback(void *userdata, const char *const *filelist, int /*filter*/) {
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

void UISystem::pickFileButton(const char *id, char *buf, int /*bufSize*/, SDL_Window *window,
                              const SDL_DialogFileFilter *filters, int nfilters) {
  ImGui::SameLine();
  std::string label = std::string("...") + "##" + id;
  if (ImGui::Button(label.c_str())) {
    SDL_ShowOpenFileDialog(fileDialogCallback, buf, window, filters, nfilters, nullptr, true);
  }
}

void UISystem::beginFrame() {
  ImGui_ImplOpenGL3_NewFrame();
  ImGui_ImplSDL3_NewFrame();
  ImGui::NewFrame();
}

void UISystem::endFrame() {
  ImGui::Render();
  ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void UISystem::update(SystemManager &systemManager) {
  auto &state = systemManager.getSystem<StateSystem>();
  auto &windowSystem = systemManager.getSystem<WindowSystem>();

  bool cursorLocked = state.isToggled(Toggle::CursorLock);
  windowSystem.setCursor(cursorLocked);

  m_renderUI = state.isToggled(Toggle::ShowUI);
}

void UISystem::render(Engine &engine, EntityManager &entityManager, SystemManager &systemManager,
                      ComponentManager &componentManager) {

  if (!m_renderUI) {
    return;
  }

  auto &renderSystem = systemManager.getSystem<RenderSystem>();
  auto &lightSystem = systemManager.getSystem<LightSystem>();
  auto &resourceSystem = systemManager.getSystem<ResourceSystem>();
  auto &sceneSystem = systemManager.getSystem<SceneSystem>();
  auto *window = systemManager.getSystem<WindowSystem>().getWindow();
  ImGuiIO &io = ImGui::GetIO();

  // Scene Explorer
  ImGui::SetNextWindowPos(ImVec2(0, 0));
  ImGui::SetNextWindowSize(ImVec2(280, io.DisplaySize.y));
  ImGui::Begin("Scene Explorer", nullptr,
               ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);

  ImVec2 avail = ImGui::GetContentRegionAvail();
  float buttonHeight = ImGui::GetFrameHeightWithSpacing();
  ImGui::BeginChild("SceneTree", ImVec2(avail.x, avail.y - buttonHeight), true);

  auto entityLabel = [&](Entity e, const char *icon) -> std::string {
    auto *nc = componentManager.getOrNil<Name>(e);
    std::string name;
    if (nc && !nc->name.empty()) {
      name = nc->name;
    } else {
      name = std::string("Entity ") + std::to_string(e);
    }
    return std::string(icon) + " " + name + "##" + std::to_string(e);
  };

  auto selectToggle = [&](Entity entity) {
    if (selectedEntity == entity)
      selectedEntity = -1;
    else
      selectedEntity = entity;
  };

  if (ImGui::CollapsingHeader("Models", ImGuiTreeNodeFlags_DefaultOpen)) {
    int modelCount = 0;
    componentManager.forEachComponent<Model>([&](Entity entity, Model &) {
      std::string label = entityLabel(entity, "[M]");
      if (ImGui::Selectable(label.c_str(), selectedEntity == entity)) {
        selectToggle(entity);
      }
      modelCount++;
    });
    if (modelCount == 0) {
      ImGui::TextDisabled("  (empty)");
    }
  }

  if (ImGui::CollapsingHeader("Lights", ImGuiTreeNodeFlags_DefaultOpen)) {
    for (Entity entity : lightSystem.getLights()) {
      std::string label = entityLabel(entity, "[L]");
      if (ImGui::Selectable(label.c_str(), selectedEntity == entity)) {
        selectToggle(entity);
      }
    }
    if (lightSystem.getLights().empty()) {
      ImGui::TextDisabled("  (empty)");
    }
  }

  if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen)) {
    int cameraCount = 0;
    componentManager.forEachComponent<Camera>([&](Entity entity, Camera &) {
      std::string label = entityLabel(entity, "[C]");
      if (ImGui::Selectable(label.c_str(), selectedEntity == entity)) {
        selectToggle(entity);
      }
      cameraCount++;
    });
    if (cameraCount == 0) {
      ImGui::TextDisabled("  (empty)");
    }
  }

  if (ImGui::CollapsingHeader("Particles", ImGuiTreeNodeFlags_DefaultOpen)) {
    int particleCount = 0;
    componentManager.forEachComponent<ParticleEmitter>([&](Entity entity, ParticleEmitter &) {
      std::string label = entityLabel(entity, "[P]");
      if (ImGui::Selectable(label.c_str(), selectedEntity == entity)) {
        selectToggle(entity);
      }
      particleCount++;
    });
    if (particleCount == 0) {
      ImGui::TextDisabled("  (empty)");
    }
  }

  ImGui::EndChild();

  if (ImGui::Button("+ Add Entity", ImVec2(-1, 0))) {
    ImGui::OpenPopup("AddEntityPopup");
  }
  renderAddEntityPopup(engine, sceneSystem, systemManager, componentManager, resourceSystem, renderSystem, window);

  ImGui::End();

  // Properties panel
  if (selectedEntity != -1) {
    float propW = 380.0f;
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
      auto *nc = componentManager.getOrNil<Name>(selectedEntity);
      if (nc) {
        char buf[64];
        std::strncpy(buf, nc->name.c_str(), sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';
        if (ImGui::InputText("Name", buf, sizeof(buf))) {
          nc->name = buf;
        }
      }
      ImGui::Text("Entity ID: %d", selectedEntity);
      ImGui::Separator();
    }

    // Transform
    if (!earlyOut && componentManager.containsComponent<Transform>(selectedEntity)) {
      bool open = ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen);
      if (open) {
        if (ImGui::SmallButton("Remove##transform")) {
          componentManager.removeComponent<Transform>(selectedEntity);
          earlyOut = true;
        }
        if (!earlyOut) {
          ImGui::Separator();
          auto &t = componentManager.getOrThrow<Transform>(selectedEntity);
          ImGui::DragFloat3("Position##t", &t.position.x, 0.1f);
          ImGui::DragFloat3("Rotation##t", &t.rotation.x, 0.1f);
          ImGui::DragFloat3("Scale##t", &t.scale.x, 0.01f);
        }
      }
    }

    // Camera
    if (!earlyOut && componentManager.containsComponent<Camera>(selectedEntity)) {
      earlyOut = renderCameraInspector(selectedEntity, systemManager, componentManager, resourceSystem, window);
    }

    // Light
    if (!earlyOut && componentManager.containsComponent<Light>(selectedEntity)) {
      bool open = ImGui::CollapsingHeader("Light", ImGuiTreeNodeFlags_DefaultOpen);
      if (open) {
        if (ImGui::SmallButton("Remove##light")) {
          systemManager.getSystem<LightSystem>().destroyLight(selectedEntity);
          componentManager.removeComponent<Light>(selectedEntity);
          earlyOut = true;
        }
        if (!earlyOut) {
          ImGui::Separator();
          auto &light = componentManager.getOrThrow<Light>(selectedEntity);
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
            ImGui::DragFloat("Constant", &light.constant, 0.01f, 0.01f, 5.0f);
            ImGui::DragFloat("Linear", &light.linear, 0.01f, 0.0f, 1.0f);
            ImGui::DragFloat("Quadratic", &light.quadratic, 0.001f, 0.0f, 1.0f);
          }
        }
      }
    }

    // Model / Material
    if (!earlyOut && componentManager.containsComponent<Model>(selectedEntity)) {
      earlyOut = renderMaterialInspector(selectedEntity, componentManager, resourceSystem, renderSystem, window);
    }

    // Particle
    if (!earlyOut && componentManager.containsComponent<ParticleEmitter>(selectedEntity)) {
      earlyOut = renderParticleInspector(selectedEntity, componentManager);
    }

    // Collision
    if (!earlyOut && componentManager.containsComponent<Collision>(selectedEntity)) {
      bool open = ImGui::CollapsingHeader("Collision (AABB)", ImGuiTreeNodeFlags_DefaultOpen);
      if (open) {
        if (ImGui::SmallButton("Remove##collision")) {
          componentManager.removeComponent<Collision>(selectedEntity);
          earlyOut = true;
        }
        if (!earlyOut) {
          ImGui::Separator();
          auto &col = componentManager.getOrThrow<Collision>(selectedEntity);
          ImGui::Checkbox("Static", &col.isStatic);
          ImGui::DragFloat3("Min##col", &col.min.x, 0.05f);
          ImGui::DragFloat3("Max##col", &col.max.x, 0.05f);
        }
      }
    }

    // Add Component section
    if (!earlyOut) {
      ImGui::Separator();
      if (ImGui::CollapsingHeader("Add Component", ImGuiTreeNodeFlags_None)) {
        if (!componentManager.containsComponent<Transform>(selectedEntity)) {
          if (ImGui::Button("+ Transform", ImVec2(-1, 0))) {
            componentManager.addInPlace<Transform>(selectedEntity);
          }
        }
        if (!componentManager.containsComponent<Light>(selectedEntity)) {
          if (ImGui::Button("+ Light", ImVec2(-1, 0))) {
            componentManager.addInPlace<Light>(selectedEntity);
            systemManager.getSystem<LightSystem>().createLight(selectedEntity);
          }
        }
        if (!componentManager.containsComponent<ParticleEmitter>(selectedEntity)) {
          if (ImGui::Button("+ ParticleEmitter Emitter", ImVec2(-1, 0))) {
            componentManager.addInPlace<ParticleEmitter>(selectedEntity);
          }
        }
        if (!componentManager.containsComponent<Collision>(selectedEntity)) {
          if (ImGui::Button("+ Collision (AABB)", ImVec2(-1, 0))) {
            componentManager.addInPlace<Collision>(selectedEntity);
          }
        }
        if (!componentManager.containsComponent<Model>(selectedEntity)) {
          static char addModelPath[256] = "";
          ImGui::SetNextItemWidth(140.0f);
          ImGui::InputText("##addModelPath", addModelPath, 256);
          pickFileButton("pick_add_model", addModelPath, 256, window);
          ImGui::SameLine();
          if (ImGui::Button("+ Model", ImVec2(-1, 0)) && addModelPath[0] != '\0') {
            if (!componentManager.containsComponent<Transform>(selectedEntity)) {
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
        if (!componentManager.containsComponent<Camera>(selectedEntity)) {
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
}

// Returns true if the component was removed (caller should set earlyOut).
bool UISystem::renderCameraInspector(Entity entity, SystemManager &systemManager, ComponentManager &componentManager,
                                     ResourceSystem & /*resourceSystem*/, SDL_Window * /*window*/) {

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
  auto &cam = componentManager.getOrThrow<Camera>(entity);

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
bool UISystem::renderParticleInspector(Entity entity, ComponentManager &componentManager) {
  if (!ImGui::CollapsingHeader("ParticleEmitter Emitter", ImGuiTreeNodeFlags_DefaultOpen))
    return false;

  if (ImGui::SmallButton("Remove##pe")) {
    componentManager.removeComponent<ParticleEmitter>(entity);
    return true;
  }

  ImGui::Separator();
  auto &p = componentManager.getOrThrow<ParticleEmitter>(entity);

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
bool UISystem::renderMaterialInspector(Entity entity, ComponentManager &componentManager,
                                       ResourceSystem &resourceSystem, RenderSystem &renderSystem, SDL_Window *window) {
  if (!ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen))
    return false;

  if (ImGui::SmallButton("Remove##model")) {
    componentManager.removeComponent<Model>(entity);
    renderSystem.markBatchesDirty();
    return true;
  }

  ImGui::Separator();
  auto &model = componentManager.getOrThrow<Model>(entity);
  const Mesh &mesh = resourceSystem.getMesh(model.meshHandle);
  const auto &submeshes = mesh.getSubmeshes();

  static char globalPaths[4][256] = {};
  const char *labels[4] = {"Diffuse", "Specular", "Normal", "Emission"};
  const TextureSlot slots[4] = {TextureSlot::Diffuse, TextureSlot::Specular, TextureSlot::Normal,
                                TextureSlot::Emission};

  ImGui::Text("%zu submeshes", submeshes.size());

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
    if (ImGui::TreeNode((std::string("Submesh ") + std::to_string(i)).c_str())) {
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

void UISystem::renderAddEntityPopup(Engine &engine, SceneSystem &sceneSystem, SystemManager &systemManager,
                                    ComponentManager &componentManager, ResourceSystem &resourceSystem,
                                    RenderSystem &renderSystem, SDL_Window *window) {
  if (!ImGui::BeginPopup("AddEntityPopup"))
    return;

  ImGui::SetNextItemWidth(300.0f);
  ImGui::Dummy(ImVec2(300.0f, 0.0f));

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
      auto &model = componentManager.getOrThrow<Model>(e);
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
      auto &p = componentManager.getOrThrow<ParticleEmitter>(e);
      p.emitRate = pemitRate;
      p.particleLifetime = plifetime;
      p.speed = pspeed;
      p.spread = pspread;
      p.startColor = glm::vec4(pstartCol[0], pstartCol[1], pstartCol[2], pstartCol[3]);
      p.endColor = glm::vec4(pendCol[0], pendCol[1], pendCol[2], pendCol[3]);
    }

    if (addCamera) {
      auto &cam = componentManager.getOrThrow<Camera>(e);
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