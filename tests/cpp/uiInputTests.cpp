// The UI helpers on a test document (checked by reading the elements back), and input through SDL events pushed by the
// test: text entry, Backspace, the mouse delta.

#include "foundation/core/engine.h"
#include "systems/inputSystem.h"
#include "testing.h"
#include "uiHelpers.h"
#include <RmlUi/Core.h>
#include <SDL3/SDL.h>

using namespace testing::ui;

namespace {
const char *kDocument = "../tests/fixtures/ui/test.rml";

void push(const SDL_Event &event) { SDL_PushEvent(const_cast<SDL_Event *>(&event)); }

void typeText(const char *text) {
  SDL_Event event{};
  event.type = SDL_EVENT_TEXT_INPUT;
  event.text.windowID = windowId();
  event.text.text = text; // SDL copies it when the event is pushed
  push(event);
}
} // namespace

TEST_CASE(ui_helpers) {
  GameUI &ui = engine.ui();
  CHECK(ui.load(kDocument));
  CHECK(ui.load(kDocument)); // again: shown, not loaded twice
  CHECK(!ui.load("../tests/fixtures/ui/missing.rml"));
  CHECK(!ui.pauseMenu().isOpen());

  Rml::Element *score = element(engine, "score");
  Rml::Element *banner = element(engine, "banner");
  CHECK(score != nullptr);
  CHECK(banner != nullptr);
  CHECK(element(engine, "no_such_element") == nullptr);
  if (!score || !banner)
    return;
  CHECK_EQ(score->GetInnerRML(), std::string("0"));

  ui.text("score", 42);
  CHECK_EQ(score->GetInnerRML(), std::string("42"));
  ui.text("score", 1.5);
  CHECK_EQ(score->GetInnerRML(), std::string("1.5"));
  ui.text("banner", std::string("Level <b>2</b>")); // RML: markup allowed
  CHECK_EQ(banner->GetInnerRML(), std::string("Level <b>2</b>"));

  ui.show("banner", false);
  CHECK(banner->GetProperty("display") != nullptr);
  CHECK_EQ(banner->GetProperty("display")->ToString(), std::string("none"));
  ui.show("banner", true);
  CHECK(banner->GetLocalProperty("display") == nullptr); // back to the stylesheet's

  CHECK(!banner->IsClassSet("hot"));
  ui.setClass("banner", "hot", true);
  CHECK(banner->IsClassSet("hot"));
  ui.setClass("banner", "hot", false);
  CHECK(!banner->IsClassSet("hot"));

  ui.style("banner", "color", "#ff0000");
  CHECK(banner->GetLocalProperty("color") != nullptr);
  ui.style("banner", "color", "");
  CHECK(banner->GetLocalProperty("color") == nullptr);
  ui.attribute("banner", "title", "tip");
  CHECK_EQ(banner->GetAttribute<Rml::String>("title", ""), std::string("tip"));

  // Elements that are not there are ignored
  ui.text("ghost", "x");
  ui.show("ghost", false);
  ui.setClass("ghost", "hot", true);
  ui.style("ghost", "color", "red");
  ui.attribute("ghost", "title", "x");
  CHECK(!ui.onClick("ghost", []() {}));

  // The helpers still work after some frames drew the document
  steps.step(5, [&engine, score]() {
    engine.ui().text("score", 7);
    CHECK_EQ(score->GetInnerRML(), std::string("7"));
  });
}

TEST_CASE(input_text) {
  InputSystem &input = engine.input();
  CHECK(!input.isTextInputActive());
  input.startTextInput("ab", 6);
  CHECK(input.isTextInputActive());
  CHECK_EQ(input.getText(), std::string("ab"));

  // Events are read at the start of the next frame
  steps.step(1, []() { typeText("c"); });
  steps.step(1, [&input]() {
    CHECK_EQ(input.getText(), std::string("abc"));
    CHECK_EQ(input.getTypedText(), std::string("c")); // this frame's characters only
    typeText("d\xC3\xA9"); // "dé": two characters, three bytes
  });
  steps.step(1, [&input]() {
    CHECK_EQ(input.getText(), std::string("abcd\xC3\xA9"));
    CHECK_EQ(input.getTypedText(), std::string("d\xC3\xA9"));
    pressKey(SDL_SCANCODE_BACKSPACE, true);
    pressKey(SDL_SCANCODE_BACKSPACE, false);
  });
  steps.step(1, [&input]() {
    CHECK_EQ(input.getText(), std::string("abcd")); // Backspace removes a whole character, not a byte
    CHECK_EQ(input.getTypedText(), std::string());
    typeText("efghij"); // past the limit of 6 characters
  });
  steps.step(1, [&input]() {
    CHECK_EQ(input.getText(), std::string("abcdef"));
    // While typing, letters are not the game's keys
    pressKey(SDL_SCANCODE_W, true);
  });
  steps.step(1, [&input]() {
    CHECK(!input.isKeyDown(SDL_SCANCODE_W));
    pressKey(SDL_SCANCODE_W, false);
    input.stopTextInput();
    CHECK(!input.isTextInputActive());
    typeText("x");
  });
  steps.step(1, [&input]() { CHECK_EQ(input.getText(), std::string("abcdef")); }); // not typing: ignored
  steps.step(1, []() { pressKey(SDL_SCANCODE_W, true); });
  steps.step(1, [&input]() {
    CHECK(input.isKeyDown(SDL_SCANCODE_W)); // and the game gets its keys back
    pressKey(SDL_SCANCODE_W, false);
  });
  steps.step(1, [&input]() { CHECK(!input.isKeyDown(SDL_SCANCODE_W)); });
}

TEST_CASE(input_mouse) {
  InputSystem &input = engine.input();
  // The system reports where the real pointer is when the window shows (a Windows runner does): let that frame pass
  steps.step(2, []() {});
  steps.step(1, [&input]() {
    CHECK_NEAR(input.getMouseDelta(), glm::vec2(0), 1e-6f);
    SDL_Event event{};
    event.type = SDL_EVENT_MOUSE_MOTION;
    event.motion.windowID = windowId();
    event.motion.x = 100.0f;
    event.motion.y = 50.0f;
    event.motion.xrel = 5.0f;
    event.motion.yrel = -3.0f;
    push(event);
    event.motion.xrel = 2.0f; // two events in a frame add up
    event.motion.yrel = 1.0f;
    push(event);
  });
  steps.step(1, [&input]() {
    CHECK_NEAR(input.getMouseDelta(), glm::vec2(7, -2), 1e-5f);
    CHECK_NEAR(input.getMousePosition(), glm::vec2(100, 50), 1e-5f);
  });
  steps.step(1, [&input]() {
    CHECK_NEAR(input.getMouseDelta(), glm::vec2(0), 1e-6f); // per frame: gone the frame after
  });
  // Buttons and the wheel
  steps.step(1, []() {
    SDL_Event down{};
    down.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    down.button.windowID = windowId();
    down.button.button = SDL_BUTTON_LEFT;
    down.button.down = true;
    down.button.x = 10;
    down.button.y = 10;
    push(down);
    SDL_Event wheel{};
    wheel.type = SDL_EVENT_MOUSE_WHEEL;
    wheel.wheel.windowID = windowId();
    wheel.wheel.y = 2.0f;
    push(wheel);
  });
  steps.step(1, [&input]() {
    CHECK(input.wasMouseButtonPressed(MouseButton::Left));
    CHECK(input.isMouseButtonDown(MouseButton::Left));
    CHECK(!input.isMouseButtonDown(MouseButton::Right));
    CHECK_NEAR(input.getMouseWheel(), 2.0f, 1e-6f);
    SDL_Event up{};
    up.type = SDL_EVENT_MOUSE_BUTTON_UP;
    up.button.windowID = windowId();
    up.button.button = SDL_BUTTON_LEFT;
    up.button.down = false;
    push(up);
  });
  steps.step(1, [&input]() {
    CHECK(!input.wasMouseButtonPressed(MouseButton::Left)); // pressed is one frame
    CHECK(input.wasMouseButtonReleased(MouseButton::Left));
    CHECK(!input.wasMouseButtonReleased(MouseButton::Right));
    CHECK(!input.isMouseButtonDown(MouseButton::Left));
    CHECK_NEAR(input.getMouseWheel(), 0.0f, 1e-6f);
  });
  steps.step(1, [&input]() { CHECK(!input.wasMouseButtonReleased(MouseButton::Left)); }); // released is one frame
}

TEST_CASE(input_key_pressed_and_released_by_name) {
  InputSystem &input = engine.input();
  auto key = [](SDL_Scancode scancode, bool down) {
    SDL_Event event{};
    event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    event.key.windowID = windowId();
    event.key.scancode = scancode;
    event.key.down = down;
    push(event);
  };
  steps.step(1, [&input, key]() {
    CHECK(!input.wasKeyReleased("Space"));
    CHECK(!input.wasKeyReleased("No Such Key")); // a name SDL does not know
    key(SDL_SCANCODE_SPACE, true);
  });
  steps.step(1, [&input, key]() {
    CHECK(input.wasKeyPressed("Space"));
    CHECK(!input.wasKeyReleased("Space")); // down is not up
    key(SDL_SCANCODE_SPACE, false);
  });
  steps.step(1, [&input]() {
    CHECK(input.wasKeyReleased("Space"));
    CHECK(!input.wasKeyPressed("Space"));
    CHECK(!input.isKeyDown("Space"));
  });
  steps.step(1, [&input]() { CHECK(!input.wasKeyReleased("Space")); }); // one frame, like a press
}

TEST_CASE(input_mouse_capture_and_cursor) {
  engine.setMouseCaptured(true);
  CHECK(engine.isMouseCaptured());
  engine.setMouseCaptured(false);
  CHECK(!engine.isMouseCaptured());
  CHECK(engine.isCursorVisible());
  engine.setCursorVisible(false);
  CHECK(!engine.isCursorVisible());
  engine.setCursorVisible(true);
  CHECK(engine.isCursorVisible());
}
