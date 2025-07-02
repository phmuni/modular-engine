#include "system/uiSystem.h"
#include "ecs/componentManager.h"

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
void UISystem::render(SystemManager &systemManager, ComponentManager &componentManager) {
  auto &renderSystem = systemManager.getSystem<RenderSystem>();

  ImGuiIO &io = ImGui::GetIO();

  ImGui::SetNextWindowPos(ImVec2(0, 0));                   // canto superior esquerdo
  ImGui::SetNextWindowSize(ImVec2(300, io.DisplaySize.y)); // largura fixa, altura total
  ImGui::Begin("Scene Explorer", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);

  ImGui::Text("Render Queue:");
  ImGui::Separator();

  ImGui::BeginChild("RenderList", ImVec2(0, 0), true); // ocupa o resto da janela

  for (const auto &entry : renderSystem.getRenderQueue()) {
    std::string label = "- " + componentManager.get<NameComponent>(entry.entity).name;
    ImGui::Selectable(label.c_str());
  }

  ImGui::EndChild();
  ImGui::End();
}

void UISystem::endFrame() {
  ImGui::Render();
  ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}