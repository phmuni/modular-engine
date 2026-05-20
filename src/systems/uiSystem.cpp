
// UI system implementation for rendering the ImGui-based user interface, including the scene explorer and properties
// panel.

#include "systems/uiSystem.h"
#include "components/collision.h"
#include "components/light.h"
#include "components/model.h"
#include "components/name.h"
#include "components/particleEmitter.h"
#include "components/transform.h"
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

void UISystem::pickFileButton(const char *id, char *buf, int bufSize, SDL_Window *window,
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

void UISystem::render(EntityManager &entityManager, SystemManager &systemManager, ComponentManager &componentManager) {

  if (!m_renderUI) {
    return;
  }

  auto &renderSystem = systemManager.getSystem<RenderSystem>();
  auto &lightSystem = systemManager.getSystem<LightSystem>();
  auto &resourceSystem = systemManager.getSystem<ResourceSystem>();
  auto &sceneSystem = systemManager.getSystem<SceneSystem>();
  auto &cameraSystem = systemManager.getSystem<CameraSystem>();
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
    std::string name = nc ? nc->name : ("Entity " + std::to_string(e));
    return std::string(icon) + " " + name + "##" + std::to_string(e);
  };

  auto selectToggle = [&](Entity entity) {
    if (selectedEntity == entity)
      selectedEntity = -1;
    else
      selectedEntity = entity;
  };

  if (ImGui::CollapsingHeader("Models", ImGuiTreeNodeFlags_DefaultOpen)) {
    for (Entity entity : renderSystem.getRenderQueue()) {
      std::string label = entityLabel(entity, "[M]");
      if (ImGui::Selectable(label.c_str(), selectedEntity == entity)) {
        selectToggle(entity);
      }
    }
    if (renderSystem.getRenderQueue().empty()) {
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
  renderAddEntityPopup(sceneSystem, entityManager, componentManager, resourceSystem, renderSystem, window);

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
    } else {

      ImGui::BeginChild("PropertiesScroll", ImVec2(0, 0), false, ImGuiWindowFlags_None);

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

      if (componentManager.containsComponent<Transform>(selectedEntity)) {
        if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
          auto &t = componentManager.getOrThrow<Transform>(selectedEntity);
          ImGui::DragFloat3("Position##t", &t.position.x, 0.1f);
          ImGui::DragFloat3("Rotation##t", &t.rotation.x, 0.1f);
          ImGui::DragFloat3("Scale##t", &t.scale.x, 0.01f);
        }
      }

      if (componentManager.containsComponent<Camera>(selectedEntity)) {
        renderCameraInspector(selectedEntity, systemManager, componentManager, resourceSystem, window);
      }

      if (componentManager.containsComponent<Light>(selectedEntity)) {
        if (ImGui::CollapsingHeader("Light", ImGuiTreeNodeFlags_DefaultOpen)) {
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

      if (componentManager.containsComponent<Model>(selectedEntity)) {
        renderMaterialInspector(selectedEntity, componentManager, resourceSystem, renderSystem, window);
      }

      if (componentManager.containsComponent<ParticleEmitter>(selectedEntity)) {
        renderParticleInspector(selectedEntity, componentManager);
      }

      if (componentManager.containsComponent<Collision>(selectedEntity)) {
        if (ImGui::CollapsingHeader("Collision (AABB)", ImGuiTreeNodeFlags_DefaultOpen)) {
          auto &col = componentManager.getOrThrow<Collision>(selectedEntity);
          ImGui::Checkbox("Static", &col.isStatic);
          ImGui::DragFloat3("Min##col", &col.min.x, 0.05f);
          ImGui::DragFloat3("Max##col", &col.max.x, 0.05f);
        }
      }

      // Add Component section
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
            auto &rs = resourceSystem;
            auto modelData = rs.loadModel(addModelPath);
            componentManager.addInPlace<Model>(selectedEntity, modelData.meshHandle,
                                               std::move(modelData.materialHandles), modelData.transparent);
            renderSystem.insertRenderable(selectedEntity);
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
      }
      ImGui::PopStyleColor(2);

      ImGui::EndChild();
      ImGui::End();
    }
  }
}

void UISystem::renderCameraInspector(Entity entity, SystemManager &systemManager, ComponentManager &componentManager,
                                     ResourceSystem &resourceSystem, SDL_Window *window) {

  if (!ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen))
    return;
  auto &camSys = systemManager.getSystem<CameraSystem>();
  auto &cam = componentManager.getOrThrow<Camera>(selectedEntity);

  bool camToogle = (camSys.getActiveCamera() == selectedEntity);

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
    camSys.setActiveCamera(selectedEntity);
  } else if (camSys.getActiveCamera() == selectedEntity) {
    camSys.removeActiveCamera();
  }
}

void UISystem::renderParticleInspector(Entity entity, ComponentManager &componentManager) {
  if (!ImGui::CollapsingHeader("ParticleEmitter Emitter", ImGuiTreeNodeFlags_DefaultOpen))
    return;

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
}

void UISystem::renderMaterialInspector(Entity entity, ComponentManager &componentManager,
                                       ResourceSystem &resourceSystem, RenderSystem &renderSystem, SDL_Window *window) {
  if (!ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen))
    return;

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
        GLuint tex = resourceSystem.loadTexture(globalPaths[t]);
        resourceSystem.setTexture(model, slots[t], tex);
        lastAppliedPaths[t] = currentPath;
      }

      if (ImGui::Button((std::string("X##g_") + labels[t]).c_str())) {
        resourceSystem.resetTexture(model, slots[t]);
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
      resourceSystem.setEmission(model, globalEmCol, globalEmStr);
    }

    ImGui::Separator();
    ImGui::Text("Solid Color (All Submeshes)");
    static glm::vec3 globalFallbackColor{1.0f, 1.0f, 1.0f};
    ImGui::ColorEdit3("##global_fallback_color", &globalFallbackColor.x);
    if (ImGui::Button("Apply Solid Color to All##global", ImVec2(-1, 0))) {
      resourceSystem.applySolidColorToModel(model, globalFallbackColor);
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
          GLuint tex = resourceSystem.loadTexture(paths[i][t]);
          resourceSystem.setMaterialTexture(handle, model.materialHandles, slots[t], tex);
          if (t == 0 && Material::hasAlphaTexture(paths[i][t])) {
            // Do not auto-mark model transparent; user can toggle opacity manually.
          }
        }
        ImGui::SameLine();
        if (ImGui::Button((std::string("X##") + labels[t]).c_str())) {
          resourceSystem.resetMaterialTexture(handle, model.materialHandles, slots[t]);
        }
      }

      float shininess = matPtr ? matPtr->getShininess() : 16.0f;
      if (ImGui::DragFloat("Shininess", &shininess, 1.0f, 1.0f, 256.0f)) {
        resourceSystem.setMaterialShininess(handle, model.materialHandles, shininess);
      }

      ImGui::Separator();
      ImGui::Text("Manual Emission");
      glm::vec3 emCol = matPtr ? matPtr->getEmissionColor() : glm::vec3(0.0f);
      if (ImGui::ColorEdit3("Emission Color", &emCol.x)) {
        resourceSystem.setMaterialEmission(handle, model.materialHandles, emCol,
                                           matPtr ? matPtr->getEmissionStrength() : 0.0f);
      }
      float emStr = matPtr ? matPtr->getEmissionStrength() : 0.0f;
      if (ImGui::DragFloat("Emission Strength", &emStr, 0.1f, 0.0f, 50.0f)) {
        resourceSystem.setMaterialEmission(handle, model.materialHandles,
                                           matPtr ? matPtr->getEmissionColor() : glm::vec3(0.0f), emStr);
      }

      ImGui::TreePop();
    }
    ImGui::PopID();
  }
}

void UISystem::renderAddEntityPopup(SceneSystem &sceneSystem, EntityManager &entityManager,
                                    ComponentManager &componentManager, ResourceSystem &resourceSystem,
                                    RenderSystem &renderSystem, SDL_Window *window) {
  if (!ImGui::BeginPopup("AddEntityPopup"))
    return;

  ImGui::SetNextItemWidth(300.0f);
  ImGui::Dummy(ImVec2(300.0f, 0.0f));

  static int formType = 0; // 0 = menu, 1 = model, 2 = light, 3 = particle

  if (formType == 0) {
    if (ImGui::Button("Add Model", ImVec2(-1, 0)))
      formType = 1;
    if (ImGui::Button("Add Light", ImVec2(-1, 0)))
      formType = 2;
    if (ImGui::Button("Add ParticleEmitter Emitter", ImVec2(-1, 0)))
      formType = 3;
    if (ImGui::Button("Add Camera", ImVec2(-1, 0)))
      formType = 4;
  } else if (formType == 1) {
    static char modelName[64] = "";
    static char modelPath[256] = "";
    static float pos[3] = {0, 0, 0};
    static float rot[3] = {0, 0, 0};
    static float scale[3] = {1, 1, 1};
    static float modelOpacity = 1.0f;
    static float modelSolidColor[3] = {1.0f, 1.0f, 1.0f};

    ImGui::Text("New Model");
    ImGui::Separator();
    ImGui::InputText("Name", modelName, 64);
    ImGui::InputText("Model Path", modelPath, 256);
    pickFileButton("pick_model_path", modelPath, 256, window);
    ImGui::InputFloat3("Position", pos);
    ImGui::InputFloat3("Rotation", rot);
    ImGui::InputFloat3("Scale", scale);
    ImGui::Separator();
    ImGui::DragFloat("Opacity##create", &modelOpacity, 0.01f, 0.0f, 1.0f, "%.2f");
    ImGui::ColorEdit3("Solid Color##create", modelSolidColor);

    if (ImGui::Button("Create", ImVec2(120, 0))) {
      Entity e =
          sceneSystem.createModelEntity(modelName, modelPath, glm::vec3(pos[0], pos[1], pos[2]),
                                        glm::vec3(rot[0], rot[1], rot[2]), glm::vec3(scale[0], scale[1], scale[2]));
      // Apply opacity and solid color
      auto *modelComp = componentManager.getOrNil<Model>(e);
      if (modelComp) {
        modelComp->opacity = modelOpacity;
        if (modelComp->materialHandles.size() > 0) {
          auto &firstMat = resourceSystem.getMaterial(modelComp->materialHandles[0]);
          if (!firstMat.hasDiffuseTexture()) {
            resourceSystem.applySolidColorToModel(
                *modelComp, glm::vec3(modelSolidColor[0], modelSolidColor[1], modelSolidColor[2]));
          }
        }
      }
      formType = 0;
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Back", ImVec2(120, 0)))
      formType = 0;
  } else if (formType == 2) {
    static char lightName[64] = "";
    static float pos[3] = {0, 0, 0};
    static float dir[3] = {0, -1, 0};
    static float color[3] = {1, 1, 1};
    static int type = 0;
    static float intensity = 1.0f;
    static float ambient = 0.2f;
    static float cutOff = 0.207911f;
    static float outerCutOff = 0.139173f;

    ImGui::Text("New Light");
    ImGui::Separator();
    ImGui::InputText("Name##lf", lightName, 64);
    ImGui::InputFloat3("Position##lf", pos);
    ImGui::InputFloat3("Direction##lf", dir);
    ImGui::ColorEdit3("Color##lf", color);
    ImGui::Combo("Type##lf", &type, "Directional\0Point\0Spot\0");
    ImGui::DragFloat("Intensity##lf", &intensity, 0.1f, 0.0f, 50.0f);
    ImGui::DragFloat("Ambient##lf", &ambient, 0.01f, 0.0f, 1.0f);

    if (type == 2) {
      ImGui::DragFloat("Cutoff##lf", &cutOff, 0.01f, 0.0f, glm::min(outerCutOff - 0.01f, 0.98f));
      ImGui::DragFloat("Outer Cutoff##lf", &outerCutOff, 0.01f, cutOff + 0.01f, 0.98f);
    }

    if (ImGui::Button("Create##lf", ImVec2(120, 0))) {
      sceneSystem.createLightEntity(lightName, glm::vec3(pos[0], pos[1], pos[2]), glm::vec3(dir[0], dir[1], dir[2]),
                                    glm::vec3(color[0], color[1], color[2]), static_cast<LightType>(type), intensity,
                                    cutOff, outerCutOff);
      memset(lightName, 0, sizeof(lightName));
      pos[0] = pos[1] = pos[2] = 0;
      dir[0] = 0;
      dir[1] = -1;
      dir[2] = 0;
      color[0] = color[1] = color[2] = 1;
      type = 0;
      intensity = 1.0f;
      ambient = 0.2f;
      cutOff = 0.207911f;
      outerCutOff = 0.139173f;
      formType = 0;
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Back##lf", ImVec2(120, 0)))
      formType = 0;
  } else if (formType == 3) {
    static char emitterName[64] = "Emitter";
    static float pos[3] = {0, 0, 0};
    static float emitRate = 40.0f;
    static float lifetime = 2.0f;
    static float speed = 2.0f;
    static float spread = 0.5f;
    static float startCol[4] = {1, 0.8f, 0.2f, 1};
    static float endCol[4] = {1, 0, 0, 0};

    ImGui::Text("New ParticleEmitter Emitter");
    ImGui::Separator();
    ImGui::InputText("Name##pf", emitterName, 64);
    ImGui::InputFloat3("Position##pf", pos);
    ImGui::DragFloat("Emit Rate##pf", &emitRate, 1.0f, 1, 10000);
    ImGui::DragFloat("Lifetime##pf", &lifetime, 0.1f, 0.1f, 100);
    ImGui::DragFloat("Speed##pf", &speed, 0.1f, 0, 100);
    ImGui::DragFloat("Spread##pf", &spread, 0.01f, 0, 10);
    ImGui::ColorEdit4("Start Color##pf", startCol);
    ImGui::ColorEdit4("End Color##pf", endCol);

    if (ImGui::Button("Create##pf", ImVec2(120, 0))) {
      Entity e = entityManager.createEntity();
      componentManager.addInPlace<Name>(e, std::string(emitterName));
      componentManager.addInPlace<Transform>(e, glm::vec3(pos[0], pos[1], pos[2]), glm::vec3(0), glm::vec3(1));
      auto &p = componentManager.addInPlace<ParticleEmitter>(e);
      p.emitRate = emitRate;
      p.particleLifetime = lifetime;
      p.speed = speed;
      p.spread = spread;
      p.startColor = glm::vec4(startCol[0], startCol[1], startCol[2], startCol[3]);
      p.endColor = glm::vec4(endCol[0], endCol[1], endCol[2], endCol[3]);

      memset(emitterName, 0, sizeof(emitterName));
      std::strncpy(emitterName, "Emitter", sizeof(emitterName));
      pos[0] = pos[1] = pos[2] = 0;
      emitRate = 40;
      lifetime = 2;
      speed = 2;
      spread = 0.5f;
      startCol[0] = 1;
      startCol[1] = 0.8f;
      startCol[2] = 0.2f;
      startCol[3] = 1;
      endCol[0] = 1;
      endCol[1] = 0;
      endCol[2] = 0;
      endCol[3] = 0;
      formType = 0;
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Back##pf", ImVec2(120, 0)))
      formType = 0;
  } else if (formType == 4) {
    static char emitterName[64] = "New Camera";
    static float position[3] = {0, 0, 0};
    static float yaw = 0;
    static float pitch = 0;
    static float fov = 60.0f;
    static bool isActive = false;

    ImGui::Text("New Camera");
    ImGui::Separator();
    ImGui::InputText("Name##cf", emitterName, 64);
    ImGui::InputFloat3("Position##cf", position);
    ImGui::InputFloat("Yaw##cf", &yaw);
    ImGui::InputFloat("Pitch##cf", &pitch);
    ImGui::SliderFloat("FOV##cf", &fov, 30.0f, 120.0f);
    ImGui::Checkbox("Active##cf", &isActive);

    if (ImGui::Button("Create##cf", ImVec2(120, 0))) {
      sceneSystem.createCameraEntity(std::string(emitterName), glm::vec3(position[0], position[1], position[2]), yaw,
                                     pitch, fov, isActive);
      position[0] = position[1] = position[2] = 0;
      yaw = 0;
      pitch = 0;
      fov = 60.0f;
      isActive = true;
      formType = 0;
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Back##cf", ImVec2(120, 0)))
      formType = 0;
  }

  ImGui::EndPopup();
}
