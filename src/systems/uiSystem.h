#pragma once
// ImGui-based editor UI: scene explorer, properties panel, and entity creation.

#include "foundation/ecs/systemManager.h"
#include <SDL3/SDL.h>

class EntityManager;
class SystemManager;
class ComponentManager;
class Engine;
class SceneSystem;
class ResourceSystem;
class RenderSystem;
using Entity = int;

class UISystem : public BaseSystem {
public:
  Entity selectedEntity = -1;
  bool m_renderUI = false;

  UISystem(SDL_Window *window, SDL_GLContext glContext);

  void beginFrame();
  void update(SystemManager &systemManager);
  void render(Engine &engine, EntityManager &entityManager, SystemManager &systemManager,
              ComponentManager &componentManager);
  void endFrame();

  // Feeds the performance overlay: the whole frame, and the part spent in update + render before the buffer swap
  void recordFrame(float frameSeconds, float workSeconds);

private:
  bool m_showStats = false;

  // Performance overlay, averaged over kStatsWindow seconds so the numbers stay readable
  static constexpr float kStatsWindow = 0.5f;
  float m_statsElapsed = 0.0f;
  float m_statsWork = 0.0f;
  float m_statsMaxFrame = 0.0f;
  int m_statsFrames = 0;
  float m_shownFps = 0.0f;
  float m_shownFrameMs = 0.0f;
  float m_shownWorkMs = 0.0f;
  float m_shownMaxMs = 0.0f;

  void renderStatsOverlay(float rightInset);
  void renderSettingsPanel(Engine &engine, EntityManager &entityManager, SystemManager &systemManager,
                           ComponentManager &componentManager);
  void renderSceneExplorer(Engine &engine, SystemManager &systemManager, ComponentManager &componentManager);
  void renderPropertiesPanel(SystemManager &systemManager, ComponentManager &componentManager);

  static void fileDialogCallback(void *userdata, const char *const *filelist, int filter);
  void pickFileButton(const char *id, char *buf, int bufSize, SDL_Window *window,
                      const SDL_DialogFileFilter *filters = nullptr, int nfilters = 0);

  bool renderCameraInspector(Entity entity, SystemManager &systemManager, ComponentManager &componentManager,
                             ResourceSystem &resourceSystem, SDL_Window *window);
  bool renderParticleInspector(Entity entity, ComponentManager &componentManager);
  bool renderMaterialInspector(Entity entity, ComponentManager &componentManager, ResourceSystem &resourceSystem,
                               RenderSystem &renderSystem, SDL_Window *window);
  void renderAddEntityPopup(Engine &engine, SceneSystem &sceneSystem, SystemManager &systemManager,
                            ComponentManager &componentManager, ResourceSystem &resourceSystem,
                            RenderSystem &renderSystem, SDL_Window *window);
};
