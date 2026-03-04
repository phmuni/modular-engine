// ImGui editor UI: scene explorer, property inspectors, and entity creation forms.
#include "systems/uiSystem.h"
#include "components/cameraComponent.h"
#include "components/lightComponent.h"
#include "components/modelComponent.h"
#include "components/nameComponent.h"
#include "components/particleComponent.h"
#include "components/transformComponent.h"
#include "rendering/resources/material.h"
#include "rendering/resources/mesh.h"
#include "systems/cameraSystem.h"
#include "systems/lightSystem.h"
#include "systems/renderSystem.h"
#include "systems/resourceSystem.h"
#include "systems/sceneSystem.h"
#include "systems/windowSystem.h"

#include <cstring>
#include <imgui/imgui.h>
#include <imgui/imgui_impl_opengl3.h>
#include <imgui/imgui_impl_sdl3.h>

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
    SDL_ShowOpenFileDialog(fileDialogCallback, buf, window, filters, nfilters, nullptr, false);
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

void UISystem::render(EntityManager &entityManager, SystemManager &systemManager, ComponentManager &componentManager) {

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
    auto *nc = componentManager.tryGet<NameComponent>(e);
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
    Entity camEntity = cameraSystem.getActiveCamera();
    if (camEntity != -1) {
      std::string label = "[C] Active Camera##" + std::to_string(camEntity);
      if (ImGui::Selectable(label.c_str(), selectedEntity == camEntity)) {
        selectToggle(camEntity);
      }
    } else {
      ImGui::TextDisabled("  (no camera)");
    }
  }

  if (ImGui::CollapsingHeader("Particles", ImGuiTreeNodeFlags_DefaultOpen)) {
    int particleCount = 0;
    componentManager.each<ParticleComponent>([&](Entity entity, ParticleComponent &) {
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
  renderAddEntityPopup(sceneSystem, entityManager, componentManager, window);

  ImGui::End();

  // Properties panel
  if (selectedEntity != -1) {
    float propW = 320.0f;
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - propW, 0));
    ImGui::SetNextWindowSize(ImVec2(propW, io.DisplaySize.y));
    bool propOpen = true;
    ImGui::Begin("Properties", &propOpen,
                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);
    if (!propOpen) {
      selectedEntity = -1;
      ImGui::End();
    } else {

      auto *nc = componentManager.tryGet<NameComponent>(selectedEntity);
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

      if (componentManager.has<TransformComponent>(selectedEntity)) {
        if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
          auto &t = componentManager.get<TransformComponent>(selectedEntity);
          ImGui::DragFloat3("Position##t", &t.position.x, 0.1f);
          ImGui::DragFloat3("Rotation##t", &t.rotation.x, 0.1f);
          ImGui::DragFloat3("Scale##t", &t.scale.x, 0.01f);
        }
      }

      if (componentManager.has<CameraComponent>(selectedEntity)) {
        if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen)) {
          auto &cam = componentManager.get<CameraComponent>(selectedEntity);
          ImGui::DragFloat3("Position##cam", &cam.position.x, 0.1f);
          ImGui::SliderFloat("FOV", &cam.fov, 30.0f, 120.0f);
          ImGui::SliderFloat("Move Speed", &cam.moveSpeed, 0.5f, 20.0f);
          ImGui::SliderFloat("Sensitivity", &cam.mouseSensitivity, 0.1f, 5.0f);
          ImGui::DragFloat("Yaw", &cam.yaw, 0.5f);
          ImGui::DragFloat("Pitch", &cam.pitch, 0.5f, -89.0f, 89.0f);
        }
      }

      if (componentManager.has<LightComponent>(selectedEntity)) {
        if (ImGui::CollapsingHeader("Light", ImGuiTreeNodeFlags_DefaultOpen)) {
          auto &light = componentManager.get<LightComponent>(selectedEntity);
          const char *types[] = {"Directional", "Point", "Spot"};
          int type = static_cast<int>(light.type);
          if (ImGui::Combo("Type##light", &type, types, IM_ARRAYSIZE(types))) {
            light.type = static_cast<LightType>(type);
          }
          ImGui::DragFloat3("Position##lp", &light.position.x, 0.1f);
          ImGui::DragFloat3("Direction##ld", &light.direction.x, 0.01f);
          ImGui::ColorEdit3("Color##lc", &light.color.x);
          ImGui::SliderFloat("Intensity", &light.intensity, 0.0f, 10.0f);
          ImGui::SliderFloat("Ambient", &light.ambient, 0.0f, 1.0f);

          if (light.type == LightType::Spot) {
            ImGui::SliderFloat("Cutoff", &light.cutOff, 0.0f, light.outerCutOff - 0.01f);
            ImGui::SliderFloat("Outer Cutoff", &light.outerCutOff, light.cutOff + 0.01f, 1.0f);
          }
          if (light.type == LightType::Point || light.type == LightType::Spot) {
            ImGui::SliderFloat("Constant", &light.constant, 0.01f, 5.0f);
            ImGui::SliderFloat("Linear", &light.linear, 0.0f, 1.0f);
            ImGui::SliderFloat("Quadratic", &light.quadratic, 0.0f, 1.0f);
          }
        }
      }

      if (componentManager.has<ModelComponent>(selectedEntity)) {
        renderMaterialInspector(selectedEntity, componentManager, resourceSystem, window);
      }

      if (componentManager.has<ParticleComponent>(selectedEntity)) {
        renderParticleInspector(selectedEntity, componentManager);
      }

      ImGui::Separator();
      ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.15f, 0.15f, 1.0f));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.9f, 0.2f, 0.2f, 1.0f));
      if (ImGui::Button("Delete Entity", ImVec2(-1, 0))) {
        sceneSystem.destroyEntity(selectedEntity);
        selectedEntity = -1;
      }
      ImGui::PopStyleColor(2);

      ImGui::End();
    } // else propOpen
  }
}

void UISystem::renderParticleInspector(Entity entity, ComponentManager &componentManager) {
  if (!ImGui::CollapsingHeader("Particle Emitter", ImGuiTreeNodeFlags_DefaultOpen))
    return;

  auto &p = componentManager.get<ParticleComponent>(entity);

  ImGui::Checkbox("Active##pe", &p.active);
  ImGui::SameLine();
  ImGui::Checkbox("Additive Blend##pe", &p.additiveBlending);

  ImGui::SliderFloat("Emit Rate", &p.emitRate, 1.0f, 500.0f);
  ImGui::SliderFloat("Lifetime", &p.particleLifetime, 0.1f, 10.0f);
  ImGui::SliderInt("Max Particles", &p.maxParticles, 10, 5000);
  ImGui::Separator();

  ImGui::Text("Motion");
  ImGui::SliderFloat("Speed", &p.speed, 0.0f, 20.0f);
  ImGui::SliderFloat("Speed Variance", &p.speedVariance, 0.0f, 10.0f);
  ImGui::DragFloat3("Direction##pe", &p.emitDirection.x, 0.01f);
  ImGui::SliderFloat("Spread", &p.spread, 0.0f, 1.0f);
  ImGui::DragFloat3("Offset##pe", &p.offset.x, 0.05f);
  ImGui::DragFloat3("Gravity##pe", &p.gravity.x, 0.05f);
  ImGui::Separator();

  ImGui::Text("Appearance");
  ImGui::SliderFloat("Size##pe", &p.size, 0.01f, 2.0f);
  ImGui::SliderFloat("Size Decay", &p.sizeDecay, 0.0f, 3.0f);
  ImGui::ColorEdit4("Start Color", &p.startColor.x);
  ImGui::ColorEdit4("End Color", &p.endColor.x);

  ImGui::Separator();
  int alive = static_cast<int>(p.particles.size());
  ImGui::Text("Alive: %d / %d", alive, p.maxParticles);
  ImGui::ProgressBar(static_cast<float>(alive) / static_cast<float>(p.maxParticles));
}

void UISystem::renderMaterialInspector(Entity entity, ComponentManager &componentManager,
                                       ResourceSystem &resourceSystem, SDL_Window *window) {
  if (!ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen))
    return;

  auto &model = componentManager.get<ModelComponent>(entity);
  const Mesh &mesh = resourceSystem.getMesh(model.meshHandle);
  const auto &submeshes = mesh.getSubmeshes();

  static char globalPaths[4][256] = {};
  const char *labels[4] = {"Diffuse", "Specular", "Normal", "Emission"};
  enum TexType { DIFF = 0, SPEC = 1, NORM = 2, EMIS = 3 };

  auto applyTex = [&](uint32_t &handle, TexType type, GLuint tex) {
    if (handle == 0) {
      handle = resourceSystem.createMaterial();
    } else {
      int count = 0;
      for (auto h : model.materialHandles)
        if (h == handle)
          count++;
      if (count > 1) {
        uint32_t newHandle = resourceSystem.createMaterial();
        resourceSystem.getMaterial(newHandle) = resourceSystem.getMaterial(handle);
        handle = newHandle;
      }
    }
    Material &mat = resourceSystem.getMaterial(handle);
    switch (type) {
    case DIFF:
      mat.setDiffuseTexture(tex);
      break;
    case SPEC:
      mat.setSpecularTexture(tex);
      break;
    case NORM:
      mat.setNormalTexture(tex);
      break;
    case EMIS:
      mat.setEmissionTexture(tex);
      break;
    }
  };

  auto resetTex = [&](uint32_t &handle, TexType type) {
    if (handle == 0)
      return;
    Material &defaultMat = resourceSystem.getMaterial(0);
    int count = 0;
    for (auto h : model.materialHandles)
      if (h == handle)
        count++;
    if (count > 1) {
      uint32_t newHandle = resourceSystem.createMaterial();
      resourceSystem.getMaterial(newHandle) = resourceSystem.getMaterial(handle);
      handle = newHandle;
    }
    Material &mat = resourceSystem.getMaterial(handle);
    switch (type) {
    case DIFF:
      mat.setDiffuseTexture(defaultMat.getDiffuse());
      break;
    case SPEC:
      mat.setSpecularTexture(defaultMat.getSpecular());
      break;
    case NORM:
      mat.setNormalTexture(defaultMat.getNormal());
      break;
    case EMIS:
      mat.setEmissionTexture(defaultMat.getEmission());
      break;
    }
  };

  ImGui::Text("%zu submeshes", submeshes.size());

  // Global texture apply
  if (ImGui::TreeNode("Apply to All Submeshes")) {
    for (int t = 0; t < 4; t++) {
      ImGui::InputText((std::string("##global_") + labels[t]).c_str(), globalPaths[t], 256);
      pickFileButton((std::string("pick_g_") + labels[t]).c_str(), globalPaths[t], 256, window);
      ImGui::SameLine();
      if (ImGui::Button((std::string("Apply ") + labels[t]).c_str())) {
        GLuint tex = resourceSystem.loadTexture(globalPaths[t]);
        for (size_t i = 0; i < model.materialHandles.size(); ++i) {
          applyTex(model.materialHandles[i], (TexType)t, tex);
        }
      }
      ImGui::SameLine();
      if (ImGui::Button((std::string("X##g_") + labels[t]).c_str())) {
        for (size_t i = 0; i < model.materialHandles.size(); ++i) {
          resetTex(model.materialHandles[i], (TexType)t);
        }
      }
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
        ImGui::InputText(labels[t], paths[i][t], 256);
        pickFileButton((std::string("pick_s") + std::to_string(i) + "_" + labels[t]).c_str(), paths[i][t], 256, window);
        ImGui::SameLine();
        if (ImGui::Button((std::string("Set##") + labels[t]).c_str())) {
          GLuint tex = resourceSystem.loadTexture(paths[i][t]);
          applyTex(handle, (TexType)t, tex);
        }
        ImGui::SameLine();
        if (ImGui::Button((std::string("X##") + labels[t]).c_str())) {
          resetTex(handle, (TexType)t);
        }
      }

      float shininess = matPtr ? matPtr->getShininess() : 16.0f;
      if (ImGui::SliderFloat("Shininess", &shininess, 1.0f, 256.0f)) {
        if (handle == 0) {
          handle = resourceSystem.createMaterial();
        }
        resourceSystem.getMaterial(handle).setShininess(shininess);
      }

      ImGui::TreePop();
    }
    ImGui::PopID();
  }
}

void UISystem::renderAddEntityPopup(SceneSystem &sceneSystem, EntityManager &entityManager,
                                    ComponentManager &componentManager, SDL_Window *window) {
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
    if (ImGui::Button("Add Particle Emitter", ImVec2(-1, 0)))
      formType = 3;
  } else if (formType == 1) {
    static char modelName[64] = "";
    static char modelPath[256] = "";
    static float pos[3] = {0, 0, 0};
    static float rot[3] = {0, 0, 0};
    static float scale[3] = {1, 1, 1};

    ImGui::Text("New Model");
    ImGui::Separator();
    ImGui::InputText("Name", modelName, 64);
    ImGui::InputText("Model Path", modelPath, 256);
    pickFileButton("pick_model_path", modelPath, 256, window);
    ImGui::InputFloat3("Position", pos);
    ImGui::InputFloat3("Rotation", rot);
    ImGui::InputFloat3("Scale", scale);

    if (ImGui::Button("Create", ImVec2(120, 0))) {
      sceneSystem.createModelEntity(modelName, modelPath, glm::vec3(pos[0], pos[1], pos[2]),
                                    glm::vec3(rot[0], rot[1], rot[2]), glm::vec3(scale[0], scale[1], scale[2]));
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
    ImGui::SliderFloat("Intensity##lf", &intensity, 0.0f, 5.0f);
    ImGui::SliderFloat("Ambient##lf", &ambient, 0.0f, 1.0f);

    if (type == 2) {
      ImGui::SliderFloat("Cutoff##lf", &cutOff, 0.0f, outerCutOff - 0.01f);
      ImGui::SliderFloat("Outer Cutoff##lf", &outerCutOff, cutOff + 0.01f, 1.0f);
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

    ImGui::Text("New Particle Emitter");
    ImGui::Separator();
    ImGui::InputText("Name##pf", emitterName, 64);
    ImGui::InputFloat3("Position##pf", pos);
    ImGui::SliderFloat("Emit Rate##pf", &emitRate, 1, 500);
    ImGui::SliderFloat("Lifetime##pf", &lifetime, 0.1f, 10);
    ImGui::SliderFloat("Speed##pf", &speed, 0, 20);
    ImGui::SliderFloat("Spread##pf", &spread, 0, 1);
    ImGui::ColorEdit4("Start Color##pf", startCol);
    ImGui::ColorEdit4("End Color##pf", endCol);

    if (ImGui::Button("Create##pf", ImVec2(120, 0))) {
      Entity e = entityManager.createEntity();
      componentManager.add<NameComponent>(e, std::string(emitterName));
      componentManager.add<TransformComponent>(e, glm::vec3(pos[0], pos[1], pos[2]));
      auto &p = componentManager.add<ParticleComponent>(e);
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
  }

  ImGui::EndPopup();
}
