#pragma once
// ImGui-based editor UI: scene explorer, properties panel, and entity creation.

#include "foundation/ecs/systemManager.h"
#include <SDL3/SDL.h>
#include <glm/glm.hpp>
#include <string>
#include "components/transform.h"

class EntityManager;
class SystemManager;
class ComponentManager;
class Engine;
class SceneSystem;
class ResourceSystem;
class RenderSystem;
using Entity = int;

class EditorUISystem : public BaseSystem {
public:
  Entity selectedEntity = -1;
  bool m_renderUI = false;

  EditorUISystem(SDL_Window *window, SDL_GLContext glContext);

  void beginFrame();
  void update(SystemManager &systemManager);
  void render(Engine &engine, EntityManager &entityManager, SystemManager &systemManager,
              ComponentManager &componentManager);
  void endFrame();

  // Feeds the performance overlay: the whole frame, and the part spent in update + render before the buffer swap
  void recordFrame(float frameSeconds, float workSeconds);

private:
  bool m_showStats = false;

  // Gizmo on the selected entity: move (W), rotate (E) or scale (R), in world or local axes, optionally snapped
  enum class GizmoOperation { Move, Rotate, Scale, Resize };
  GizmoOperation m_gizmoOperation = GizmoOperation::Move;
  bool m_gizmoLocal = false;
  bool m_gizmoSnap = false;
  // Solid: the gizmo does not push the entity into other models (it stops against them and slides along them)
  bool m_gizmoSolid = false;
  // Where the entity goes when scaled to newScale with one face kept in place: its low side on each axis when
  // keepLowSide, else its high side
  glm::vec3 oneSidedScalePosition(SystemManager &systemManager, ComponentManager &componentManager,
                                  const Transform &transform, const glm::vec3 &newScale, bool keepLowSide) const;

  // Resize (T): a handle on each face of the model; dragging one moves that face alone, the opposite one stays
  struct ResizeDrag {
    int axis = -1;        // the entity's own axis of the face being dragged, -1 when none
    float side = 1.0f;    // +1: the face at the high end of that axis, -1: the low end
    Transform start;      // the transform when the drag began
    glm::vec3 faceStart;  // the face's center then, world space
    glm::vec3 direction;  // the face's outward normal, world space
    float applied = 0.0f; // how far the face has moved so far (world units, outward)
  } m_resize;
  bool m_resizeHovered = false; // the cursor is on a handle this frame (a click there drags, not selects)
  void renderResizeHandles(SystemManager &systemManager, ComponentManager &componentManager, Transform &transform,
                           const glm::mat4 &view, const glm::mat4 &projection);
  // The transform with the dragged face moved `offset` outward from where it started
  Transform resizedTransform(SystemManager &systemManager, ComponentManager &componentManager, float offset) const;
  // Align to surface: the move gizmo drags the entity over surfaces under the cursor, tilted to their slope
  bool m_gizmoAlign = false;
  float m_alignHeading = 0.0f; // degrees around +Y, from when the drag started
  // Places the selected entity on the surface under the cursor; false if there is none
  bool alignToSurface(SystemManager &systemManager, ComponentManager &componentManager, Transform &transform);
  // Applies a gizmo change to the transform: as is, or kept out of other models when m_gizmoSolid is on
  // Returns false when Solid refused a rotation or scale
  bool applyGizmoChange(SystemManager &systemManager, ComponentManager &componentManager, Transform &transform,
                        const Transform &wanted, bool translating);
  void renderGizmo(SystemManager &systemManager, ComponentManager &componentManager);
  bool m_gizmoShown = false; // drawn this frame, so ImGuizmo::IsOver means something

  // Clicking a model in the scene selects it, clicking empty space clears the selection (debug UI shown, cursor
  // unlocked, not over a panel or the gizmo). The selected model's box is outlined.
  void handleClickSelection(Engine &engine, SystemManager &systemManager);
  void drawSelectionBox(SystemManager &systemManager, ComponentManager &componentManager);
  bool m_scrollToSelection = false; // the scene explorer brings the entity a click selected into view
  void renderGizmoControls();

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
  // Edit (freeze), Play and Stop, at the top of the screen
  void renderPlayToolbar(Engine &engine);
  // Hierarchy: the families (entities with a parent or children) as trees; drag an entity onto another to attach it,
  // onto the empty space below to detach it
  void renderHierarchy(SystemManager &systemManager, ComponentManager &componentManager);
  void dragToParent(SystemManager &systemManager, ComponentManager &componentManager, Entity entity,
                    const std::string &label);
  // In the Properties panel: the entity's parent, and saving it with its children as a prefab
  void renderParentAndPrefab(SystemManager &systemManager, ComponentManager &componentManager);
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
