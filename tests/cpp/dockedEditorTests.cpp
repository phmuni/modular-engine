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
