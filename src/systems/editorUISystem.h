#pragma once
// ImGui-based editor UI: scene explorer, properties panel, and entity creation.

#include "foundation/ecs/systemManager.h"
#include "systems/editor/assetBrowser.h"
#include <SDL3/SDL.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>
#include <vector>
#include "components/transform.h"

class EntityManager;
class SystemManager;
class ComponentManager;
class Engine;
class SceneSystem;
class ResourceSystem;
class RenderSystem;
using Entity = int;

// Opens a file with the program the system uses for its type (scripts: the editor for .lua files)
void openInEditor(const std::string &path);

class EditorUISystem : public BaseSystem {
public:
  // The primary selection: the one clicked last, shown in the Properties panel (-1: nothing selected). The whole
  // selection is selection(), the primary last.
  Entity selectedEntity = -1;
  bool m_renderUI = false;

  const std::vector<Entity> &selection() const { return m_selection; }
  bool isSelected(Entity entity) const;
  // Drops the entities that no longer exist (after undo, Stop or loading a scene)
  void pruneSelection(const EntityManager &entityManager);

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

  // Selection: a set with a primary. Everything that changes it goes through these, which keep selectedEntity the
  // last of m_selection.
  std::vector<Entity> m_selection;
  Entity m_selectionAnchor = -1; // where a Shift+click range starts: the entity last clicked without Shift
  void setSelection(std::vector<Entity> entities);
  void selectOnly(Entity entity);
  void toggleSelected(Entity entity);
  // The selected entities that have no selected ancestor: what moves, duplicates and deletes (the rest follow them)
  std::vector<Entity> selectionRoots(ComponentManager &componentManager) const;
  // A click on a row of a list shown in `order`: Ctrl/Cmd toggles, Shift selects from the anchor, plain selects it
  // alone (or, with `toggleOff`, clears it when it already is the only one)
  void clickInList(Entity entity, const std::vector<Entity> &order, bool toggleOff);
  // Dragging a box in empty space of the view selects the models inside it
  struct Marquee {
    bool active = false;   // pressed on empty space
    bool dragging = false; // moved far enough to be a box
    glm::vec2 start{0.0f};
  } m_marquee;
  void updateMarquee(Engine &engine, SystemManager &systemManager, ComponentManager &componentManager);

  // Interface scale. Automatic (from the display: 1 on macOS, the content scale on Windows) until the user picks
  // one in Settings (0 = automatic); kept in imgui.ini. Panels and fonts are drawn at it, and the fonts at the
  // framebuffer's pixel density too, so they stay sharp on high density screens.
  static constexpr float kMinUiScale = 0.75f, kMaxUiScale = 2.0f;
  SDL_Window *m_window = nullptr;
  float m_uiScaleUser = 0.0f;
  float m_uiScale = 1.0f; // the one in effect, in window units
  float m_appliedScale = 0.0f;
  float m_appliedDensity = 0.0f;
  void applyUiScale();
  void registerSettings();
  // Layout, all from the window's size and the scale
  float m_explorerWidth = 0.0f;   // this frame's left and right panels, 0 when not shown
  float m_propertiesWidth = 0.0f;
  float panelWidth(float base) const;
  float ui(float size) const { return size * m_uiScale; }
  bool m_panelsHidden = false;    // only the view, the toolbar and the gizmo: the editor stays on (Tab)
  bool m_resetLayout = false;     // put the Settings panel back where it starts, at the next frame
  bool m_wasShown = false;        // the debug UI was on last frame
  bool m_lockBeforeShown = false; // the cursor lock the game had when the debug UI came up
  // The hint over the view about flying the camera
  void renderViewportHint(SystemManager &systemManager);

  // Gizmo on the primary selection: move (W), rotate (E) or scale (R), in world or local axes, optionally snapped.
  // It acts on one entity (m_gizmoEntity: the primary, or its topmost selected ancestor); what it does to that
  // entity's transform is then done to the rest of the selection around it, so they move, turn and scale together.
  enum class GizmoOperation { Move, Rotate, Scale, Resize };
  GizmoOperation m_gizmoOperation = GizmoOperation::Move;
  Entity m_gizmoEntity = -1;
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
  // The entity's rotation and angles when the gizmo drag started: snapped rotations are measured from it
  glm::quat m_dragStartRotation{1.0f, 0.0f, 0.0f, 0.0f};
  glm::vec3 m_dragStartAngles{0.0f};
  glm::quat m_dragRotation{1.0f, 0.0f, 0.0f, 0.0f}; // the rotate gizmo's unsnapped rotation during the drag
  bool m_gizmoAlign = false;
  bool m_gizmoAlignTilt = true; // Align to surface: tilt the entity to the slope (else it keeps its rotation)
  float m_alignHeading = 0.0f;  // degrees around +Y, from when the drag started; R adds 90 in Tilt mode
  // The rotation the entity keeps without Tilt: from when the drag started, turned by R and T
  glm::quat m_alignRotation{1.0f, 0.0f, 0.0f, 0.0f};
  bool m_alignTurned = false; // R or T changed m_alignRotation during this drag
  // The point of the entity the cursor holds, in its own (rotated, unscaled) frame, found on the first frame of the
  // drag: it is put on the surface under the cursor, so nothing jumps and perspective does not drift it
  glm::vec3 m_alignGrab{0.0f};
  bool m_alignGrabbed = false; // m_alignGrab is set for this drag
  // Places the selected entity on the surface under the cursor; false if there is none. With `snapped`, its place on
  // the surface goes to the grid of the model under it (0.5 units along the surface) and its heading to 15 degrees.
  // The rotate gizmo's result with Snap on, as angles: on the 15 degree steps when the turn changed one angle, else
  // the turn itself in 15 degree steps from where the drag started
  glm::vec3 snappedRotation(const glm::quat &dragged) const;
  bool alignToSurface(SystemManager &systemManager, ComponentManager &componentManager, Transform &transform,
                      bool snapped);
  // Applies a gizmo change to the transform: as is, or kept out of other models when m_gizmoSolid is on
  // Returns false when Solid refused a rotation or scale
  bool applyGizmoChange(SystemManager &systemManager, ComponentManager &componentManager, Transform &transform,
                        const Transform &wanted, bool translating);
  void renderGizmo(SystemManager &systemManager, ComponentManager &componentManager);
  void renderGizmoOn(SystemManager &systemManager, ComponentManager &componentManager);
  // Carries the change that took `before` to `after` (the gizmo entity's transform) over to the other selected roots
  void followGizmo(SystemManager &systemManager, ComponentManager &componentManager, const Transform &before,
                   const Transform &after);
  bool m_gizmoShown = false; // drawn this frame, so ImGuizmo::IsOver means something

  // Clicking a model in the scene selects it, clicking empty space clears the selection (debug UI shown, cursor
  // unlocked, not over a panel or the gizmo). The selected model's box is outlined.
  void handleClickSelection(Engine &engine, SystemManager &systemManager, ComponentManager &componentManager);
  void drawSelectionBoxes(SystemManager &systemManager, ComponentManager &componentManager);
  void drawBox(SystemManager &systemManager, ComponentManager &componentManager, Entity entity, unsigned color,
               float thickness);
  // The navigation map's walkable cells and the last path found, over the scene (Settings: Show navigation)
  void drawNavigation(Engine &engine, SystemManager &systemManager, ComponentManager &componentManager);
  bool m_showNavigation = false;
  bool m_deleteRequested = false;   // from a context menu: done at the next frame, not while the lists are drawn
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
  // Undo, Redo, Edit (freeze), Play and Stop, at the top of the screen
  void renderPlayToolbar(Engine &engine, SystemManager &systemManager);
  // Ctrl+Z undoes, Ctrl+Shift+Z and Ctrl+Y redo (Cmd on macOS, as for ImGui's Ctrl)
  void handleHistoryShortcuts(Engine &engine, SystemManager &systemManager);
  // Ctrl+D duplicates the selected entity and its children, Ctrl+C / Ctrl+V copy and paste them through the system
  // clipboard, Delete (Backspace too) deletes them; each is one undo step
  void handleEditShortcuts(Engine &engine, SystemManager &systemManager, ComponentManager &componentManager);
  void duplicateSelected(Engine &engine, SystemManager &systemManager, ComponentManager &componentManager);
  void copySelected(SystemManager &systemManager, ComponentManager &componentManager);
  void pasteClipboard(Engine &engine, SystemManager &systemManager, ComponentManager &componentManager);
  void deleteSelected(Engine &engine, SystemManager &systemManager, ComponentManager &componentManager);
  // The right-click menu of the entity just drawn (hierarchy and section lists)
  void entityContextMenu(Engine &engine, SystemManager &systemManager, ComponentManager &componentManager,
                         Entity entity);
  // Opens an undo step when a click lands on the editor and closes it when the edit is over (Engine::beginCheckpoint)
  void beginEditTracking(Engine &engine);
  void endEditTracking(Engine &engine);
  // Hierarchy: the families (entities with a parent or children) as trees; drag an entity onto another to attach it,
  // onto the empty space below to detach it
  void renderHierarchy(Engine &engine, SystemManager &systemManager, ComponentManager &componentManager);
  void dragToParent(Engine &engine, SystemManager &systemManager, ComponentManager &componentManager, Entity entity,
                    const std::string &label);
  // In the Properties panel: the entity's parent, and saving it with its children as a prefab
  void renderParentAndPrefab(SystemManager &systemManager, ComponentManager &componentManager);
  void renderPropertiesPanel(Engine &engine, SystemManager &systemManager, ComponentManager &componentManager);

  // --- Assets and linked prefabs (editor/editorAssets.cpp)
  AssetBrowser m_assets;
  bool m_assetsCollapsed = true;     // the strip at the bottom starts as its title bar
  bool m_assetsOpenRequest = false;  // open it at the next frame (Select prefab file)
  float m_assetsHeight = 0.0f;       // this frame's strip, 0 when not shown
  std::string m_pendingScene;        // a scene asked for while the current one has unsaved changes
  Entity m_overrideRoot = -1;        // the instance whose overrides are listed in Properties
  double m_overrideTime = -1000.0;
  std::vector<std::string> m_overrideList;
  void renderAssetPanel(Engine &engine, SystemManager &systemManager, ComponentManager &componentManager);
  void handleAssetEvent(Engine &engine, SystemManager &systemManager, ComponentManager &componentManager,
                        const AssetBrowser::Event &event);
  // An asset being dragged over the view: shows where it would go, and on release places it or hands it to the
  // entity under the cursor
  void handleAssetDrop(Engine &engine, SystemManager &systemManager, ComponentManager &componentManager);
  // A script or a sound dropped on an entity gives it (or replaces) its Script or AudioSource; false if the asset is
  // not one of those
  bool assignAssetToEntity(Engine &engine, ComponentManager &componentManager, Entity entity, const std::string &asset);
  // A model or a prefab placed at `point` (on a surface: with its lowest point on it), selected
  Entity placeAsset(Engine &engine, SystemManager &systemManager, ComponentManager &componentManager,
                    const std::string &asset, const glm::vec3 &point, bool onSurface);
  void requestSceneLoad(Engine &engine, const std::string &asset);
  void renderSceneLoadDialog(Engine &engine);
  // In Properties: the prefab an entity is an instance of, its overrides, Apply / Revert / Unpack
  void renderPrefabLink(Engine &engine, SystemManager &systemManager, ComponentManager &componentManager);

  static void fileDialogCallback(void *userdata, const char *const *filelist, int filter);
  void pickFileButton(const char *id, char *buf, int bufSize, SDL_Window *window,
                      const SDL_DialogFileFilter *filters = nullptr, int nfilters = 0);

  bool renderCameraInspector(Entity entity, SystemManager &systemManager, ComponentManager &componentManager,
                             ResourceSystem &resourceSystem, SDL_Window *window);
  bool renderParticleInspector(Entity entity, ComponentManager &componentManager);
  bool renderMaterialInspector(Engine &engine, Entity entity, ComponentManager &componentManager,
                               ResourceSystem &resourceSystem, RenderSystem &renderSystem, SDL_Window *window);
  void renderAddEntityPopup(Engine &engine, SceneSystem &sceneSystem, SystemManager &systemManager,
                            ComponentManager &componentManager, ResourceSystem &resourceSystem,
                            RenderSystem &renderSystem, SDL_Window *window);
};
