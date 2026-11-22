// The editor's docked layout: with it open the game is drawn into the Game panel (its rectangle is the game view for
// picking, the camera's aspect, the render size and the mouse position), closed or maximized it fills the window.

#include "describe.h"
#include "foundation/core/engine.h"
#include "rendering/renderer.h"
#include "systems/cameraSystem.h"
#include "systems/editorUISystem.h"
#include "systems/inputSystem.h"
#include "systems/stateSystem.h"
#include "testing.h"
#include "uiHelpers.h"
#include <ImGuizmo.h>
#include <imgui/imgui_internal.h>
#include <memory>

using namespace testing::ui;

namespace {
void checkRect(const glm::vec4 &actual, const glm::vec4 &expected, float epsilon) {
  CHECK_NEAR(actual.x, expected.x, epsilon);
  CHECK_NEAR(actual.y, expected.y, epsilon);
  CHECK_NEAR(actual.z, expected.z, epsilon);
  CHECK_NEAR(actual.w, expected.w, epsilon);
}

void showEditor(Engine &engine, bool shown) {
  engine.getSystemManager().getSystem<StateSystem>().setToggle(Toggle::ShowUI, shown);
}

EditorUISystem &editorOf(Engine &engine) { return engine.getSystemManager().getSystem<EditorUISystem>(); }

glm::vec2 windowOf(Engine &engine) { return glm::vec2(engine.windowSize()); }

// The window's framebuffer, which is larger than its size on a high density display
glm::ivec2 pixelsOf() {
  int width = 0, height = 0;
  SDL_GetWindowSizeInPixels(SDL_GetWindowFromID(windowId()), &width, &height);
  return {width, height};
}

// A key with Shift held, as ImGui reads the modifier from the event
void pressWithShift(SDL_Scancode key, bool down) {
  SDL_Event event{};
  event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
  event.key.windowID = windowId();
  event.key.scancode = key;
  event.key.key = SDL_GetKeyFromScancode(key, SDL_KMOD_NONE, false); // ImGui maps keys by their keycode
  event.key.down = down;
  event.key.mod = down ? SDL_KMOD_LSHIFT : SDL_KMOD_NONE;
  SDL_PushEvent(&event);
}
} // namespace

TEST_CASE(docked_editor_game_view_follows_the_game_panel) {
  GameEntity target = engine.create("Target", Desc("box").at(0, 2, 0)); // in front of the test camera, at its center
  showEditor(engine, true);
  steps.step(6, [&engine, target]() {
    auto &editor = editorOf(engine);
    const glm::vec4 rect = editor.gameViewRect();
    const glm::vec2 window = windowOf(engine);
    CHECK(editor.gameViewPixels().active);
    // Inside the window and smaller than it: the panels take the rest
    CHECK(rect.x >= 0.0f && rect.y >= 0.0f);
    CHECK(rect.x + rect.z <= window.x + 0.01f && rect.y + rect.w <= window.y + 0.01f);
    CHECK(rect.z < window.x && rect.w < window.y);
    CHECK(rect.z > 8.0f && rect.w > 8.0f);

    // It is the content of the Game panel
    const ImGuiWindow *panel = ImGui::FindWindowByName("Game");
    CHECK(panel != nullptr);
    if (panel) {
      CHECK_NEAR(rect.x, panel->InnerRect.Min.x, 1.0f);
      CHECK_NEAR(rect.y, panel->InnerRect.Min.y, 1.0f);
      CHECK_NEAR(rect.z, panel->InnerRect.GetWidth(), 1.0f);
      CHECK_NEAR(rect.w, panel->InnerRect.GetHeight(), 1.0f);
    }

    // The picture is made at the panel's size, and the camera's aspect follows it
    const ViewRect &pixels = editor.gameViewPixels();
    CHECK_EQ(engine.renderResolution().x, pixels.width);
    CHECK_EQ(engine.renderResolution().y, pixels.height);
    auto &cameras = engine.getSystemManager().getSystem<CameraSystem>();
    CHECK_NEAR(cameras.viewAspect(), rect.z / rect.w, 0.02f);

    // A ray through the panel's center (positions are measured from its corner) hits what is at the camera's center
    const glm::vec2 center(rect.z * 0.5f, rect.w * 0.5f);
    const auto hit = engine.pickModel(center);
    CHECK(hit.has_value());
    if (hit)
      CHECK_EQ(hit->entity, target.id());
    // ... and projecting that entity lands at the panel's center
    const Camera *camera = engine.getActiveCamera();
    CHECK(camera != nullptr);
    if (camera) {
      const glm::vec4 clip = cameras.getProjectionMatrix(*camera) * cameras.getViewMatrix(*camera) *
                             glm::vec4(target.position(), 1.0f);
      CHECK_NEAR(clip.x / clip.w, 0.0f, 1e-3f);
      CHECK_NEAR(clip.y / clip.w, 0.0f, 1e-3f);
    }
    // A point off the center: the ray's direction follows the panel's shape, not the window's
    const auto corner = engine.screenRay(glm::vec2(rect.z, rect.w * 0.5f)); // the middle of the right edge
    CHECK(corner.has_value());
    if (corner && camera) {
      const float tanHalf = std::tan(glm::radians(camera->fov) * 0.5f);
      const glm::vec3 forward = camera->front;
      const glm::vec3 right = glm::normalize(glm::cross(forward, camera->up));
      const float across = glm::dot(corner->direction, right) / glm::dot(corner->direction, forward);
      CHECK_NEAR(across, tanHalf * rect.z / rect.w, 0.02f);
    }
  });
}

TEST_CASE(docked_editor_closed_is_the_whole_window) {
  GameEntity target = engine.create("Target", Desc("box").at(0, 2, 0));
  showEditor(engine, true);
  steps.step(4, [&engine]() { CHECK(editorOf(engine).gameViewPixels().active); });
  steps.step(1, [&engine]() { showEditor(engine, false); });
  steps.step(4, [&engine, target]() {
    auto &editor = editorOf(engine);
    const glm::vec2 window = windowOf(engine);
    const glm::vec4 rect = editor.gameViewRect();
    CHECK(!editor.gameViewPixels().active);
    checkRect(rect, glm::vec4(0.0f, 0.0f, window.x, window.y), 0.01f);
    CHECK_NEAR(engine.getSystemManager().getSystem<CameraSystem>().viewAspect(), 0.0f, 1e-6f);
    CHECK_EQ(engine.renderResolution().x, pixelsOf().x); // render scale 1: the whole framebuffer
    CHECK_EQ(engine.renderResolution().y, pixelsOf().y);
    const auto hit = engine.pickModel(window * 0.5f);
    CHECK(hit.has_value());
    if (hit)
      CHECK_EQ(hit->entity, target.id());
  });
}

TEST_CASE(docked_editor_maximize_and_restore) {
  showEditor(engine, true);
  auto before = std::make_shared<glm::vec4>(0.0f);
  steps.step(4, [&engine, before]() {
    *before = editorOf(engine).gameViewRect();
    CHECK(editorOf(engine).gameViewPixels().active);
    editorOf(engine).setMaximized(true);
  });
  steps.step(3, [&engine]() {
    auto &editor = editorOf(engine);
    const glm::vec2 window = windowOf(engine);
    CHECK(editor.isMaximized());
    CHECK(editor.m_renderUI); // the editor is still on
    CHECK(!editor.gameViewPixels().active);
    checkRect(editor.gameViewRect(), glm::vec4(0.0f, 0.0f, window.x, window.y), 0.01f);
    editor.setMaximized(false);
  });
  steps.step(4, [&engine, before]() {
    auto &editor = editorOf(engine);
    CHECK(!editor.isMaximized());
    CHECK(editor.gameViewPixels().active);
    // The layout came back as it was
    checkRect(editor.gameViewRect(), *before, 0.01f);
  });
}

TEST_CASE(docked_editor_shift_space_toggles_the_maximized_view) {
  showEditor(engine, true);
  steps.step(4, []() {
    pressWithShift(SDL_SCANCODE_SPACE, true);
  });
  steps.step(3, [&engine]() {
    CHECK(editorOf(engine).isMaximized());
    pressWithShift(SDL_SCANCODE_SPACE, false);
  });
  steps.step(3, [&engine]() {
    CHECK(editorOf(engine).isMaximized()); // released: still maximized
    pressWithShift(SDL_SCANCODE_SPACE, true);
  });
  steps.step(3, [&engine]() {
    CHECK(!editorOf(engine).isMaximized());
    pressWithShift(SDL_SCANCODE_SPACE, false);
  });
}

TEST_CASE(docked_editor_mouse_position_is_measured_from_the_panel) {
  showEditor(engine, true);
  steps.step(4, [&engine]() {
    const glm::vec4 rect = editorOf(engine).gameViewRect();
    SDL_Event event{};
    event.type = SDL_EVENT_MOUSE_MOTION;
    event.motion.windowID = windowId();
    event.motion.x = rect.x + 30.0f;
    event.motion.y = rect.y + 20.0f;
    SDL_PushEvent(&event);
  });
  steps.step(2, [&engine]() {
    CHECK_NEAR(engine.input().getMousePosition(), glm::vec2(30.0f, 20.0f), 0.5f);
  });
}

TEST_CASE(docked_editor_reset_layout_rebuilds_the_default) {
  showEditor(engine, true);
  auto first = std::make_shared<glm::vec4>(0.0f);
  steps.step(4, [&engine, first]() {
    *first = editorOf(engine).gameViewRect();
    editorOf(engine).resetLayout();
  });
  steps.step(4, [&engine, first]() {
    CHECK(editorOf(engine).gameViewPixels().active);
    checkRect(editorOf(engine).gameViewRect(), *first, 0.5f);
  });
}

// Clicks in the middle of the Game panel, where a box is drawn: while the game is paused (or being edited) they select it (measured from the panel's
// corner, as pickModel takes it: with the window's coordinates the click would land a panel's width off); while the
// game plays they are the game's; with Alt held they select and the game does not see them. "input": run alone.
namespace {
void mouseEvent(glm::vec2 at, Uint32 type) {
  SDL_Event event{};
  event.type = type;
  if (type == SDL_EVENT_MOUSE_MOTION) {
    event.motion.windowID = windowId();
    event.motion.x = at.x;
    event.motion.y = at.y;
  } else {
    event.button.windowID = windowId();
    event.button.button = SDL_BUTTON_LEFT;
    event.button.down = type == SDL_EVENT_MOUSE_BUTTON_DOWN;
    event.button.clicks = 1;
    event.button.x = at.x;
    event.button.y = at.y;
  }
  SDL_PushEvent(&event);
}

void altKey(bool down) {
  SDL_Event event{};
  event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
  event.key.windowID = windowId();
  event.key.scancode = SDL_SCANCODE_LALT;
  event.key.key = SDLK_LALT;
  event.key.down = down;
  event.key.mod = down ? SDL_KMOD_LALT : SDL_KMOD_NONE;
  SDL_PushEvent(&event);
}

// Clicks the panel's middle; `selects`: whether the box ends up selected, `gameSees`: whether the game saw the press
void clickTheBox(Engine &engine, testing::Steps &steps, bool paused, bool alt, bool selects, bool gameSees) {
  GameEntity target = engine.create("Target", Desc("box").at(0, 2, 0)); // at the test camera's center
  showEditor(engine, true);
  engine.time().setPaused(paused); // Editing works the same, but a test's frames do not run in it
  auto center = std::make_shared<glm::vec2>(0.0f);
  steps.step(6, [&engine, center, alt]() {
    const glm::vec4 rect = editorOf(engine).gameViewRect();
    *center = glm::vec2(rect.x + rect.z * 0.5f, rect.y + rect.w * 0.5f);
    CHECK(rect.x > 1.0f); // the left panel is there: the two ways of measuring differ
    mouseEvent(*center, SDL_EVENT_MOUSE_MOTION);
    if (alt)
      altKey(true);
  });
  steps.step(2, [center]() { mouseEvent(*center, SDL_EVENT_MOUSE_BUTTON_DOWN); });
  steps.step(2, [&engine, gameSees, paused]() {
    if (!paused) // paused, the game sees it too: its UI (a pause menu) still takes clicks
      CHECK_EQ(engine.input().isMouseButtonDown(MouseButton::Left), gameSees);
  });
  steps.step(1, [center, alt]() {
    mouseEvent(*center, SDL_EVENT_MOUSE_BUTTON_UP);
    if (alt)
      altKey(false);
  });
  steps.step(2, [&engine, target, selects]() {
    CHECK_EQ(editorOf(engine).selectedEntity == target.id(), selects);
  });
}
} // namespace

TEST_CASE(docked_editor_click_selects_while_paused_input) { clickTheBox(engine, steps, true, false, true, false); }
TEST_CASE(docked_editor_click_is_the_games_while_playing_input) { clickTheBox(engine, steps, false, false, false, true); }
TEST_CASE(docked_editor_alt_click_selects_while_playing_input) { clickTheBox(engine, steps, false, true, true, false); }

// The gizmo takes the mouse over the Game panel (ImGuizmo only does over its draw list's window, or one it is told of)
TEST_CASE(docked_editor_gizmo_is_grabbable_input) {
  GameEntity target = engine.create("Target", Desc("box").at(0, 2, 0)); // at the test camera's center
  showEditor(engine, true);
  engine.time().setPaused(true);
  auto center = std::make_shared<glm::vec2>(0.0f);
  steps.step(6, [&engine, center]() {
    const glm::vec4 rect = editorOf(engine).gameViewRect();
    *center = glm::vec2(rect.x + rect.z * 0.5f, rect.y + rect.w * 0.5f);
    mouseEvent(*center, SDL_EVENT_MOUSE_MOTION);
  });
  steps.step(2, [center]() { mouseEvent(*center, SDL_EVENT_MOUSE_BUTTON_DOWN); }); // selects it: the gizmo appears
  steps.step(1, [center]() { mouseEvent(*center, SDL_EVENT_MOUSE_BUTTON_UP); });
  steps.step(3, [&engine, target]() {
    CHECK_EQ(editorOf(engine).selectedEntity, target.id());
    CHECK(ImGuizmo::IsOver()); // its center handle is under the cursor
  });
}

// Dragging a model by its body: the spot grabbed stays under the cursor, the model resting on the surface the cursor
// points at (a tall pillar grabbed near its base keeps to the floor, not jumping onto the wall behind it). With Align to
// surface off it moves level instead, keeping its height and rotation.
namespace {
// Where a world point is on the screen (window units), through the active camera and the Game panel
glm::vec2 screenOf(Engine &engine, const glm::vec3 &world) {
  auto &cameraSystem = engine.getSystemManager().getSystem<CameraSystem>();
  const Camera *camera = engine.tryGet<Camera>(cameraSystem.getActiveCamera());
  const glm::vec4 clip = cameraSystem.getProjectionMatrix(*camera) * cameraSystem.getViewMatrix(*camera) * glm::vec4(world, 1.0f);
  const glm::vec4 rect = editorOf(engine).gameViewRect();
  return glm::vec2(rect.x + (clip.x / clip.w * 0.5f + 0.5f) * rect.z, rect.y + (0.5f - clip.y / clip.w * 0.5f) * rect.w);
}

// A floor (top at y = 0), a wall behind it (front face at z = -2.5) and a pillar 3 high on the floor at (0, 1.5, 1)
struct Pillar {
  GameEntity pillar;
};
Pillar pillarScene(Engine &engine, bool align = true) {
  engine.create("Floor", Desc("box").at(0, -0.5f, 0).size(20, 1, 20));
  engine.create("Wall", Desc("box").at(0, 2.5f, -3).size(20, 5, 1));
  Pillar scene{engine.create("Pillar", Desc("box").at(0, 1.5f, 1).size(1, 3, 1))};
  // From above and in front, looking down at the floor: from the test camera's own height the floor is a few pixels
  // of the view and a drag across it hardly moves the cursor
  engine.tryGet<Camera>(engine.getSystemManager().getSystem<CameraSystem>().getActiveCamera())
      ->lookAt(glm::vec3(0, 7, 7), glm::vec3(0, 0, -1));
  showEditor(engine, true);
  engine.time().setPaused(true);
  editorOf(engine).setAlignToSurface(align);
  return scene;
}

// Presses on `grab` (a world point of the pillar), moves the cursor in steps to where `to` is on the screen, releases
void dragFrom(Engine &engine, testing::Steps &steps, const glm::vec3 &grab, const glm::vec3 &to) {
  steps.step(6, [&engine, grab]() { mouseEvent(screenOf(engine, grab), SDL_EVENT_MOUSE_MOTION); });
  steps.step(2, [&engine, grab]() { mouseEvent(screenOf(engine, grab), SDL_EVENT_MOUSE_BUTTON_DOWN); });
  // Out to the side first: the view is small, and the way from the grab to the target alone may be under the few
  // pixels that make a press a drag
  const glm::vec3 aside = to + glm::vec3(2.0f, 0.0f, 1.0f);
  for (int part = 1; part <= 3; ++part)
    steps.step(2, [&engine, grab, aside, part]() {
      mouseEvent(glm::mix(screenOf(engine, grab), screenOf(engine, aside), part / 3.0f), SDL_EVENT_MOUSE_MOTION);
    });
  for (int part = 1; part <= 3; ++part)
    steps.step(2, [&engine, aside, to, part]() {
      mouseEvent(glm::mix(screenOf(engine, aside), screenOf(engine, to), part / 3.0f), SDL_EVENT_MOUSE_MOTION);
    });
  steps.step(2, [&engine, to]() { mouseEvent(screenOf(engine, to), SDL_EVENT_MOUSE_BUTTON_UP); });
}
} // namespace

TEST_CASE(docked_editor_body_drag_base_grab_stays_on_the_floor_input) {
  Pillar scene = pillarScene(engine);
  // The cursor goes to where the spot grabbed (0.3 above the floor) would be at z = -1
  dragFrom(engine, steps, glm::vec3(0, 0.3f, 1.5f), glm::vec3(0, 0.3f, -1));
  steps.step(3, [&engine, scene]() {
    CHECK_EQ(editorOf(engine).selectedEntity, scene.pillar.id());
    const glm::vec3 at = scene.pillar.position();
    CHECK_NEAR(at.y, 1.5f, 0.05f);        // still on the floor
    CHECK_NEAR(scene.pillar.rotation(), glm::vec3(0.0f), 0.5f); // upright
    CHECK(at.z < 0.0f);                   // it went towards the wall
    CHECK(at.z > -2.0f);                  // and is not on it
    CHECK_NEAR(at.z, -1.5f, 0.15f);       // the grabbed spot (0.5 in front of the center) went to z = -1
  });
}

TEST_CASE(docked_editor_body_drag_is_one_undo_step_input) {
  Pillar scene = pillarScene(engine);
  dragFrom(engine, steps, glm::vec3(0, 0.3f, 1.5f), glm::vec3(0, 0.3f, -1));
  steps.step(3, [&engine, scene]() {
    CHECK(scene.pillar.position().z < 0.0f);
    engine.undo();
  });
  steps.step(2, [&engine]() {
    GameEntity pillar = engine.find("Pillar");
    CHECK_NEAR(pillar.position(), glm::vec3(0, 1.5f, 1), 1e-3f); // the whole drag undone at once
  });
}

TEST_CASE(docked_editor_body_drag_top_grab_holds_the_spot_in_the_air_input) {
  // The trade-off of grabbing high: the cursor holds a spot 2.8 above the floor, so for the same cursor as the base
  // grab (over the floor at z = -1) the pillar comes out nearer the camera, where the ray is that high; still on the floor
  Pillar scene = pillarScene(engine);
  dragFrom(engine, steps, glm::vec3(0, 2.8f, 1.5f), glm::vec3(0, 0.3f, -1));
  steps.step(3, [&engine, scene]() {
    const glm::vec3 at = scene.pillar.position();
    CHECK_NEAR(at.y, 1.5f, 0.05f);
    CHECK(at.z > 1.4f); // it moved (it was at 1)
  });
}

TEST_CASE(docked_editor_press_without_a_drag_only_selects_input) {
  Pillar scene = pillarScene(engine);
  steps.step(6, [&engine]() { mouseEvent(screenOf(engine, glm::vec3(0, 0.3f, 1.5f)), SDL_EVENT_MOUSE_MOTION); });
  steps.step(2, [&engine]() { mouseEvent(screenOf(engine, glm::vec3(0, 0.3f, 1.5f)), SDL_EVENT_MOUSE_BUTTON_DOWN); });
  steps.step(2, [&engine]() { mouseEvent(screenOf(engine, glm::vec3(0, 0.3f, 1.5f)), SDL_EVENT_MOUSE_BUTTON_UP); });
  steps.step(2, [&engine, scene]() {
    CHECK_EQ(editorOf(engine).selectedEntity, scene.pillar.id());
    CHECK_NEAR(scene.pillar.position(), glm::vec3(0, 1.5f, 1), 1e-4f);
  });
}

TEST_CASE(docked_editor_body_drag_without_align_moves_level_input) {
  // Align off: the pillar keeps its height and rotation, the grabbed spot sliding on the level plane through it, past
  // where the floor would have it rest: the spot goes to z = -2.2, so the pillar's center to -2.7, into the wall
  Pillar scene = pillarScene(engine, false);
  dragFrom(engine, steps, glm::vec3(0, 0.3f, 1.5f), glm::vec3(0, 0.3f, -2.2f));
  steps.step(3, [&engine, scene]() {
    CHECK_EQ(editorOf(engine).selectedEntity, scene.pillar.id());
    const glm::vec3 at = scene.pillar.position();
    CHECK_NEAR(at.y, 1.5f, 1e-3f);                               // the same height
    CHECK_NEAR(scene.pillar.rotation(), glm::vec3(0.0f), 1e-3f); // and rotation
    CHECK_NEAR(at.x, 0.0f, 0.15f);
    CHECK_NEAR(at.z, -2.7f, 0.15f);
  });
}
