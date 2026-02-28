#include "systems/uiSystem.h"
#include "components/lightComponent.h"
#include "components/modelComponent.h"
#include "components/nameComponent.h"

#include "components/transformComponent.h"
#include "rendering/resources/material.h"
#include "rendering/resources/mesh.h"
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

void UISystem::pickFileButton(const char *id, char *buf, int bufSize,
                              SDL_Window *window,
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
  auto *window = systemManager.getSystem<WindowSystem>().getWindow();
  ImGuiIO &io = ImGui::GetIO();

  ImGui::SetNextWindowPos(ImVec2(0, 0));
  ImGui::SetNextWindowSize(ImVec2(300, io.DisplaySize.y));
  ImGui::Begin("Scene Explorer", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);

  ImVec2 avail = ImGui::GetContentRegionAvail();
  float buttonHeight = ImGui::GetFrameHeightWithSpacing();

  ImGui::BeginChild("RenderList", ImVec2(avail.x, avail.y - buttonHeight), true);

  for (Entity entity : renderSystem.getRenderQueue()) {
    std::string label = "- " + componentManager.get<NameComponent>(entity).name + "##" + std::to_string(entity);
    bool isSelected = (selectedEntity == entity);

    if (ImGui::Selectable(label.c_str(), isSelected)) {
      selectedEntity = entity;
    }

    if (ImGui::BeginPopupContextItem(label.c_str())) {
      selectedEntity = entity;
      ImGui::Text("Entity ID: %d", selectedEntity);
      ImGui::Separator();

      if (componentManager.has<TransformComponent>(selectedEntity)) {
        auto &transform = componentManager.get<TransformComponent>(selectedEntity);
        ImGui::Text("Transform:");
        ImGui::DragFloat3("Position", &transform.position.x, 0.1f);
        ImGui::DragFloat3("Rotation", &transform.rotation.x, 0.1f);
        ImGui::DragFloat3("Scale", &transform.scale.x, 0.1f);
      }

      if (componentManager.has<ModelComponent>(selectedEntity)) {
        auto &model = componentManager.get<ModelComponent>(selectedEntity);
        Mesh &mesh = resourceSystem.getMesh(model.meshHandle);
        const auto &submeshes = mesh.getSubmeshes();

        ImGui::Separator();
        ImGui::Text("Materials (%zu submeshes):", submeshes.size());

        static char globalPaths[4][256] = {};
        const char *labels[4] = {"Diffuse", "Specular", "Normal", "Emission"};
        enum TexType { DIFF = 0, SPEC = 1, NORM = 2, EMIS = 3 };

        auto applyAll = [&](TexType type, const char *path) {
          GLuint tex = resourceSystem.loadTexture(path);

          for (size_t i = 0; i < model.materialHandles.size(); ++i) {
            uint32_t &handle = model.materialHandles[i];

            // Copy-on-write if shared
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
          }
        };

        // Reset to default texture for all submeshes
        auto removeAll = [&](TexType type) {
          Material &defaultMat = resourceSystem.getMaterial(0);

          for (size_t i = 0; i < model.materialHandles.size(); ++i) {
            uint32_t &handle = model.materialHandles[i];

            if (handle == 0)
              continue;

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
          }
        };

        // Global texture controls
        for (int t = 0; t < 4; t++) {
          ImGui::InputText((std::string("Set All ") + labels[t]).c_str(), globalPaths[t], 256);
          pickFileButton((std::string("pick_global_") + labels[t]).c_str(), globalPaths[t], 256, window);
          ImGui::SameLine();
          if (ImGui::Button((std::string("Apply##all_") + labels[t]).c_str())) {
            applyAll((TexType)t, globalPaths[t]);
          }
          ImGui::SameLine();
          if (ImGui::Button((std::string("[X]##all_") + labels[t]).c_str())) {
            removeAll((TexType)t);
          }
        }

        ImGui::Separator();

        // Per-submesh material controls
        static std::vector<std::array<char[256], 4>> paths;
        if (paths.size() != submeshes.size())
          paths.resize(submeshes.size());

        for (size_t i = 0; i < model.materialHandles.size(); ++i) {
          ImGui::PushID(i);

          if (ImGui::CollapsingHeader((std::string("Submesh ") + std::to_string(i)).c_str())) {

            uint32_t &handle = model.materialHandles[i];
            Material *matPtr = (handle != 0) ? &resourceSystem.getMaterial(handle) : nullptr;

            if (matPtr) {
              ImGui::Text("Current Material: %u", handle);
            } else {
              ImGui::Text("Current Material: DEFAULT (0)");
            }

            for (int t = 0; t < 4; t++) {
              ImGui::InputText(labels[t], paths[i][t], 256);
              pickFileButton((std::string("pick_sub") + std::to_string(i) + "_" + labels[t]).c_str(), paths[i][t], 256, window);
              ImGui::SameLine();

              if (ImGui::Button((std::string("Set##") + labels[t]).c_str())) {
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

                GLuint tex = resourceSystem.loadTexture(paths[i][t]);
                Material &m = resourceSystem.getMaterial(handle);

                switch (t) {
                case DIFF:
                  m.setDiffuseTexture(tex);
                  break;
                case SPEC:
                  m.setSpecularTexture(tex);
                  break;
                case NORM:
                  m.setNormalTexture(tex);
                  break;
                case EMIS:
                  m.setEmissionTexture(tex);
                  break;
                }
              }

              ImGui::SameLine();

              if (ImGui::Button((std::string("[X]##") + labels[t]).c_str())) {
                if (handle != 0) {
                  int count = 0;
                  for (auto h : model.materialHandles)
                    if (h == handle)
                      count++;
                  if (count > 1) {
                    uint32_t newHandle = resourceSystem.createMaterial();
                    resourceSystem.getMaterial(newHandle) = resourceSystem.getMaterial(handle);
                    handle = newHandle;
                  }

                  Material &m = resourceSystem.getMaterial(handle);
                  Material &defaultMat = resourceSystem.getMaterial(0);

                  switch (t) {
                  case DIFF:
                    m.setDiffuseTexture(defaultMat.getDiffuse());
                    break;
                  case SPEC:
                    m.setSpecularTexture(defaultMat.getSpecular());
                    break;
                  case NORM:
                    m.setNormalTexture(defaultMat.getNormal());
                    break;
                  case EMIS:
                    m.setEmissionTexture(defaultMat.getEmission());
                    break;
                  }
                }
              }
            }

            float shininess = matPtr ? matPtr->getShininess() : 16.0f;
            if (ImGui::SliderFloat("Shininess", &shininess, 1.0f, 256.0f)) {
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
              resourceSystem.getMaterial(handle).setShininess(shininess);
            }
          }

          ImGui::PopID();
        }
      }

      if (ImGui::Button("Delete")) {
        sceneSystem.destroyEntity(selectedEntity);
      }

      if (ImGui::Button("Close")) {
        ImGui::CloseCurrentPopup();
      }

      ImGui::EndPopup();
    }
  }

  ImGui::Separator();
  ImGui::Text("Lights:");
  ImGui::Separator();

  for (const auto &entity : lightSystem.getLights()) {
    std::string label = "- " + componentManager.get<NameComponent>(entity).name + "##" + std::to_string(entity);
    bool isSelected = (selectedEntity == entity);

    if (ImGui::Selectable(label.c_str(), isSelected)) {
      selectedEntity = entity;
    }

    if (ImGui::BeginPopupContextItem(label.c_str())) {
      selectedEntity = entity;
      ImGui::Text("Light ID: %d", selectedEntity);
      ImGui::Separator();

      if (componentManager.has<LightComponent>(selectedEntity)) {
        auto &light = componentManager.get<LightComponent>(selectedEntity);

        ImGui::Text("Light Properties:");
        ImGui::Separator();
        ImGui::DragFloat3("Position##light", &light.position.x, 0.1f);
        ImGui::DragFloat3("Direction##light", &light.direction.x, 0.1f);
        ImGui::ColorEdit3("Color##light", &light.color.x);
        ImGui::SliderFloat("Intensity##light", &light.intensity, 0.0f, 5.0f);
        ImGui::SliderFloat("Ambient##light", &light.ambient, 0.0f, 1.0f);

        const char *types[] = {"Directional", "Point", "Spot"};
        int type = static_cast<int>(light.type);
        ImGui::Combo("Type##light", &type, types, IM_ARRAYSIZE(types));
        light.type = static_cast<LightType>(type);

        if (light.type == LightType::Spot) {
          ImGui::SliderFloat("Cutoff Angle##light", &light.cutOff, 0.0f, light.outerCutOff - 0.01f);
          ImGui::SliderFloat("Outer Cutoff##light", &light.outerCutOff, light.cutOff + 0.01f, 1.0f);
        }

        if (light.type == LightType::Point || light.type == LightType::Spot) {
          ImGui::SliderFloat("Constant##atten", &light.constant, 0.1f, 5.0f);
          ImGui::SliderFloat("Linear##atten", &light.linear, 0.0f, 1.0f);
          ImGui::SliderFloat("Quadratic##atten", &light.quadratic, 0.0f, 1.0f);
        }

        if (ImGui::Button("Delete")) {
          sceneSystem.destroyEntity(selectedEntity);
        }
      }

      if (ImGui::Button("Close")) {
        ImGui::CloseCurrentPopup();
      }

      ImGui::EndPopup();
    }
  }

  ImGui::EndChild();

  if (ImGui::Button("Add Entity", ImVec2(-1, 0))) {
    ImGui::OpenPopup("AddEntityPopup");
  }

  if (ImGui::BeginPopup("AddEntityPopup")) {
    static int formType = 0; // 0 = menu, 1 = model, 2 = light

    if (formType == 0) {
      if (ImGui::Button("Add Model"))
        formType = 1;
      if (ImGui::Button("Add Light"))
        formType = 2;
    } else if (formType == 1) {
      static char modelName[64] = "";
      static char modelPath[256] = "";
      static float pos[3] = {0.0f, 0.0f, 0.0f};
      static float rot[3] = {0.0f, 0.0f, 0.0f};
      static float scale[3] = {1.0f, 1.0f, 1.0f};

      ImGui::InputText("Name", modelName, 64);
      ImGui::InputText("Model Path", modelPath, 256);
      pickFileButton("pick_model_path", modelPath, 256, window);
      ImGui::InputFloat3("Position", pos);
      ImGui::InputFloat3("Rotation", rot);
      ImGui::InputFloat3("Scale", scale);

      if (ImGui::Button("Create")) {
        sceneSystem.createModelEntity(modelName, modelPath, glm::vec3(pos[0], pos[1], pos[2]),
                                      glm::vec3(rot[0], rot[1], rot[2]), glm::vec3(scale[0], scale[1], scale[2]));
        formType = 0;
        ImGui::CloseCurrentPopup();
      }
      if (ImGui::Button("Back"))
        formType = 0;
    } else if (formType == 2) {
      static char lightName[64] = "";
      static float pos[3] = {0.0f, 0.0f, 0.0f};
      static float dir[3] = {0.0f, -1.0f, 0.0f};
      static float color[3] = {1.0f, 1.0f, 1.0f};
      static int type = 0;
      static float intensity = 1.0f;
      static float ambient = 0.2f;
      static float cutOff = 0.207911f;
      static float outerCutOff = 0.139173f;

      ImGui::InputText("Name##light_form", lightName, 64);
      ImGui::InputFloat3("Position##pos_light_form", pos);
      ImGui::InputFloat3("Direction##dir_light_form", dir);
      ImGui::ColorEdit3("Color##color_light_form", color);
      ImGui::Separator();
      ImGui::Combo("Type##light_form", &type, "Directional\0Point\0Spot\0");
      ImGui::SliderFloat("Intensity##light_form", &intensity, 0.0f, 5.0f);
      ImGui::SliderFloat("Ambient##light_form", &ambient, 0.0f, 1.0f);

      if (type == 2) {
        ImGui::SliderFloat("Cutoff Angle##light_form", &cutOff, 0.0f, outerCutOff - 0.01f);
        ImGui::SliderFloat("Outer Cutoff##light_form", &outerCutOff, cutOff + 0.01f, 1.0f);
      }

      if (ImGui::Button("Create##light_form")) {
        sceneSystem.createLightEntity(lightName, glm::vec3(pos[0], pos[1], pos[2]), glm::vec3(dir[0], dir[1], dir[2]),
                                      glm::vec3(color[0], color[1], color[2]), static_cast<LightType>(type), intensity,
                                      cutOff, outerCutOff);

        memset(lightName, 0, sizeof(lightName));
        pos[0] = pos[1] = pos[2] = 0.0f;
        dir[0] = 0.0f;
        dir[1] = -1.0f;
        dir[2] = 0.0f;
        color[0] = color[1] = color[2] = 1.0f;
        type = 0;
        intensity = 1.0f;
        ambient = 0.2f;
        cutOff = 0.207911f;
        outerCutOff = 0.139173f;
        formType = 0;
        ImGui::CloseCurrentPopup();
      }
      if (ImGui::Button("Back"))
        formType = 0;
    }

    ImGui::EndPopup();
  }

  ImGui::End();
}
