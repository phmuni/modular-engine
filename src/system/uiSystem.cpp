#include "ecs/componentManager.h"
#include "ecs/entityManager.h"

#include "system/lightSystem.h"
#include "system/renderSystem.h"
#include "system/uiSystem.h"

#include "imgui/imgui.h"
#include "imgui/imgui_impl_opengl3.h"
#include "imgui/imgui_impl_sdl3.h"

#include "component/nameComponent.h"
#include "component/transformComponent.h"
#include "system/sceneSystem.h"

UISystem::UISystem(SDL_Window *w, SDL_GLContext gl) {
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGui::StyleColorsDark();

  ImGui_ImplSDL3_InitForOpenGL(w, gl);
  ImGui_ImplOpenGL3_Init("#version 330");
}

void UISystem::beginFrame() {
  ImGui_ImplOpenGL3_NewFrame();
  ImGui_ImplSDL3_NewFrame();
  ImGui::NewFrame();
}
void UISystem::render(EntityManager &entityManager, SystemManager &systemManager, ComponentManager &componentManager) {
  auto &renderSystem = systemManager.getSystem<RenderSystem>();
  auto &lightSystem = systemManager.getSystem<LightSystem>();
  ImGuiIO &io = ImGui::GetIO();

  ImGui::SetNextWindowPos(ImVec2(0, 0));
  ImGui::SetNextWindowSize(ImVec2(300, io.DisplaySize.y));
  ImGui::Begin("Scene Explorer", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);

  ImVec2 avail = ImGui::GetContentRegionAvail();

  float buttonHeight = ImGui::GetFrameHeightWithSpacing();

  ImGui::BeginChild("RenderList", ImVec2(avail.x, avail.y - buttonHeight), true);

  for (const auto &entry : renderSystem.getRenderQueue()) {
    Entity entity = entry.entity;
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

        if (ImGui::Button("Delete")) {
          auto &sceneSystem = systemManager.getSystem<SceneSystem>();
          sceneSystem.removeEntity(selectedEntity);
        }
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
        if (type != static_cast<int>(light.type)) {
          light.type = static_cast<LightType>(type);
        }

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
          auto &sceneSystem = systemManager.getSystem<SceneSystem>();
          sceneSystem.removeEntity(selectedEntity);
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
    auto &sceneSystem = systemManager.getSystem<SceneSystem>();

    static int formType = 0;

    if (formType == 0) {
      if (ImGui::Button("Add Model")) {
        formType = 1;
      }
      if (ImGui::Button("Add Light")) {
        formType = 2;
      }
    } else if (formType == 1) {
      static char modelName[64] = "";
      static char modelPath[128] = "";
      static char texturePath[128] = "";
      static float pos[3] = {0.0f, 0.0f, 0.0f};
      static float rot[3] = {0.0f, 0.0f, 0.0f};
      static float scale[3] = {1.0f, 1.0f, 1.0f};

      ImGui::InputText("Name", modelName, 64);
      ImGui::InputText("Model Path", modelPath, 128);
      ImGui::InputText("Texture Path", texturePath, 128);
      ImGui::InputFloat3("Position", pos);
      ImGui::InputFloat3("Rotation", rot);
      ImGui::InputFloat3("Scale", scale);

      if (ImGui::Button("Create")) {
        sceneSystem.createEntityModel(modelName, modelPath, texturePath, glm::vec3(pos[0], pos[1], pos[2]),
                                      glm::vec3(rot[0], rot[1], rot[2]), glm::vec3(scale[0], scale[1], scale[2]));
        formType = 0;
        ImGui::CloseCurrentPopup();
      }
      if (ImGui::Button("Back")) {
        formType = 0;
      }
    } else if (formType == 2) {
      static char lightName[64] = "";
      static float pos[3] = {0.0f, 0.0f, 0.0f};
      static float dir[3] = {0.0f, -1.0f, 0.0f};
      static float color[3] = {1.0f, 1.0f, 1.0f};
      static int type = 0; // 0 = Directional, 1 = Point, 2 = Spot
      static float intensity = 1.0f;
      static float ambient = 0.2f;
      static float cutOff = 0.207911f;      // cos(radians(12.5))
      static float outerCutOff = 0.139173f; // cos(radians(17.5))
      static float constant = 1.0f;
      static float linear = 0.09f;
      static float quadratic = 0.032f;

      ImGui::InputText("Name##light_form", lightName, 64);
      ImGui::Text("Position:");
      ImGui::InputFloat3("##pos_light_form", pos);
      ImGui::Text("Direction:");
      ImGui::InputFloat3("##dir_light_form", dir);
      ImGui::Text("Color:");
      ImGui::ColorEdit3("##color_light_form", color);
      ImGui::Separator();
      ImGui::Combo("Type##light_form", &type, "Directional\0Point\0Spot\0");
      ImGui::SliderFloat("Intensity##light_form", &intensity, 0.0f, 5.0f);
      ImGui::SliderFloat("Ambient##light_form", &ambient, 0.0f, 1.0f);

      if (type == 1 || type == 2) { // Point or Spot
        ImGui::Text("Attenuation:");
        ImGui::SliderFloat("Constant##light_form", &constant, 0.1f, 5.0f);
        ImGui::SliderFloat("Linear##light_form", &linear, 0.0f, 1.0f);
        ImGui::SliderFloat("Quadratic##light_form", &quadratic, 0.0f, 1.0f);
      }

      if (type == 2) { // Spot light
        ImGui::Text("Spotlight Cutoff:");
        ImGui::SliderFloat("Cutoff Angle##light_form", &cutOff, 0.0f, outerCutOff - 0.01f);
        ImGui::SliderFloat("Outer Cutoff##light_form", &outerCutOff, cutOff + 0.01f, 1.0f);
      }

      if (ImGui::Button("Create##light_form")) {
        sceneSystem.createEntityLight(lightName, glm::vec3(pos[0], pos[1], pos[2]), glm::vec3(dir[0], dir[1], dir[2]),
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
        constant = 1.0f;
        linear = 0.09f;
        quadratic = 0.032f;
        formType = 0;
        ImGui::CloseCurrentPopup();
      }
      if (ImGui::Button("Back")) {
        formType = 0;
      }
    }

    ImGui::EndPopup();
  }

  ImGui::End();
}

void UISystem::endFrame() {
  ImGui::Render();
  ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}