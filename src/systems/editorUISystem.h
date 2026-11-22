#pragma once
// ImGui-based editor UI: scene explorer, properties panel, and entity creation.

#include "foundation/ecs/systemManager.h"
#include "rendering/viewRect.h"
#include "systems/editor/assetBrowser.h"
#include <SDL3/SDL.h>
#include <glm/glm.hpp>
#include <imgui/imgui.h>
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
  // The scene list's search text ("wheel", "t:Light", "tag:enemy"); empty shows the whole tree
  void setSceneFilter(const std::string &text);
  bool isSelected(Entity entity) const;
  // Drops the entities that no longer exist (after undo, Stop or loading a scene)
  void pruneSelection(const EntityManager &entityManager);

  EditorUISystem(SDL_Window *window, SDL_GLContext glContext);

  void beginFrame();
  void update(SystemManager &systemManager);
  void render(Engine &engine, EntityManager &entityManager, SystemManager &systemManager,
              ComponentManager &componentManager);
  void endFrame();

  // The game view: where the game is drawn in the window. With the editor open it is the content of the docked "Game"
  // panel (updated by beginFrame); closed, or maximized (Shift+Space, Tab), it is the whole window.
  // gameViewRect(): x, y, width, height in window units (what ImGui and mouse events use);
  // gameViewPixels(): the same in framebuffer pixels, `active` only while it is not the whole window.
  glm::vec4 gameViewRect() const { return m_viewRect; }
  const ViewRect &gameViewPixels() const { return m_viewPixels; }
  // The mouse is over the game view (nothing in front of it), or a press that began there is still held
  bool gameHasMouse() const { return m_gameMouse && !m_altPicking; }
  // The gizmo bar's "Align to surface": dragging an object by its body, and the gizmo's center, slide it over surfaces
  void setAlignToSurface(bool enabled) { m_gizmoAlign = enabled; }
  // The Game panel is the window ImGui has focused
  bool gameFocused() const { return m_gameFocused; }
  // Maximized: the panels are hidden and the game view fills the window, the editor staying on (toolbar, gizmo,
  // selection). The docked layout is kept and comes back as it was.
  bool isMaximized() const { return m_panelsHidden; }
  void setMaximized(bool maximized) { m_panelsHidden = maximized; }
  // Maximize when Play starts and restore at Stop (kept in imgui.ini, unlike the maximized state)
  bool maximizeOnPlay() const { return m_maximizeOnPlay; }
  void setMaximizeOnPlay(bool enabled) { m_maximizeOnPlay = enabled; }
  // Puts the docked layout back to the default one, at the next frame
  void toggleMaximized();
  void resetLayout() { m_resetLayout = true; }

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
  float panelWidth(float base) const;
  float ui(float size) const { return size * m_uiScale; }
  bool m_panelsHidden = false;    // maximized: only the view, the toolbar and the gizmo, the editor stays on (Tab)
  bool m_resetLayout = false;     // build the default docked layout again, at the next frame
  bool m_maximizeOnPlay = false;
  bool m_maximizedByPlay = false; // Play maximized the view, so Stop restores it
  bool m_wasPlaying = false;      // the game was in play mode last frame, to see Play and Stop
  // Docking: the panels around the Game panel. beginFrame submits the dock space and the Game panel first, so the
  // frame's game view is known before the scene is drawn.
  void beginDockSpace();
  void followPlayMode(bool playing);
  void buildDefaultLayout(ImGuiID dockSpace, float width, float height);
  bool m_docked = false;          // the editor is open with its panels: the game view is the Game panel
  bool m_layoutBuilt = false;     // a docked layout exists (saved or built), to be kept alive while it is not shown
  glm::vec4 m_viewRect{0.0f};     // see gameViewRect()
  ViewRect m_viewPixels;
  bool m_gameMouse = false;
  // Clicks and drags in the view: the editor's (select, box select, gizmo, resize handles) while editing or paused, the
  // game's while it plays, except with Alt held, which picks without the game seeing the click
  bool m_viewForEditor = true;
  bool m_altPicking = false; // playing with Alt held: the view's clicks are kept from the game
  bool m_gameFocused = false;
  bool m_gameHovered = false;
  glm::vec4 m_lastGameRect{0.0f}; // the Game panel's last visible rectangle (window units)
  // The view's corner and size as ImGui draws (window units), the window's own when not docked
  ImVec2 viewPos() const { return ImVec2(m_viewRect.x, m_viewRect.y); }
  ImVec2 viewSize() const { return ImVec2(m_viewRect.z, m_viewRect.w); }
  // The mouse measured from the view's corner, as Engine::screenRay and pickModel take it (window units)
  glm::vec2 viewMouse() const {
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    return glm::vec2(mouse.x - m_viewRect.x, mouse.y - m_viewRect.y);
  }
  // A point in ImGui's window units to the view's normalized device coordinates (y up), and the reverse
  glm::vec2 toNdc(const ImVec2 &point) const {
    return glm::vec2((point.x - m_viewRect.x) / m_viewRect.z * 2.0f - 1.0f, 1.0f - (point.y - m_viewRect.y) / m_viewRect.w * 2.0f);
  }
  ImVec2 fromClip(const glm::vec4 &clip) const {
    return ImVec2(m_viewRect.x + (clip.x / clip.w * 0.5f + 0.5f) * m_viewRect.z,
                  m_viewRect.y + (0.5f - clip.y / clip.w * 0.5f) * m_viewRect.w);
  }
  // The panels have the mouse: over one, or dragging in one (the view, an ImGui window too, does not count)
  bool mouseOnPanels() const;
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
  // The surface the cursor points at, and whether the gizmo's center is held on the cursor over it (decided when the
  // cursor comes onto it: true when the held center is over it then; else the entity follows the spot pointed at)
  Entity m_alignSurface = -1;
  bool m_alignHeld = false;
  // Places the selected entity on the surface under the cursor; false if there is none. With `snapped`, its place on
  // the surface goes to the grid of the model under it (0.5 units along the surface) and its heading to 15 degrees.
  // The rotate gizmo's result with Snap on, as angles: on the 15 degree steps when the turn changed one angle, else
  // the turn itself in 15 degree steps from where the drag started
  glm::vec3 snappedRotation(const glm::quat &dragged) const;
  // `grab`: the point of the entity held under the cursor, an offset from its origin in its own frame (zero: the origin)
  bool alignToSurface(SystemManager &systemManager, ComponentManager &componentManager, Transform &transform,
                      bool snapped, const glm::vec3 &grab = glm::vec3(0.0f));
  // Dragging by the body (Move, whatever the Align toggle says): pressing on a model and dragging it keeps the grabbed point under the cursor (not
  // the entity's center, which for a tall one is far from the floor it should rest on)
  struct BodyGrab {
    bool pending = false;         // pressed on a model; a drag past a few pixels starts the grab
    bool active = false;          // dragging
    bool fresh = false;           // the first frame of the drag: the grab point is taken from the entity as it is
    Entity collapseTo = -1;       // released without a drag, the selection becomes this one (a click on a selected one)
    glm::vec2 start{0.0f};        // where the press was (window units)
    glm::vec3 point{0.0f};        // the spot pressed, world space; then the grabbed point as last placed
    glm::vec3 local{0.0f};        // the grab: the pressed spot from the entity's origin, in its own frame
  } m_bodyGrab;
  // Starts, ends and cancels the body grab from the mouse; begins the undo step of a drag
  void updateBodyGrab(Engine &engine);
  // Without a surface under the cursor: the grabbed point follows the cursor on the plane facing the camera
  // The grabbed spot onto the cursor's ray, on the plane through it facing the camera, or with `level` the horizontal
  // one (it stays put when the ray does not meet that in front); `snapped` puts it on the Snap grid
  // `point` is the grabbed spot now (world), `local` the same in the entity's own frame
  void moveGrabFreely(SystemManager &systemManager, ComponentManager &componentManager, Transform &transform,
                      const glm::vec3 &point, const glm::vec3 &local, bool level = false, bool snapped = false);
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
  char m_sceneFilter[128] = "";     // the scene list's search box
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

  void renderStatsOverlay();
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
  bool m_assetsOpenRequest = false;  // bring the Assets panel to the front at the next frame (Select prefab file)
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
