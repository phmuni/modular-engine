#pragma once
// What the tests of UI documents and menus share: finding and clicking elements, SDL key events pushed at the test's
// window, and two reads of the game's state. In testing::ui; a test file takes it with `using namespace testing::ui;`.

#include "foundation/core/engine.h"
#include "systems/inputSystem.h"
#include "systems/scriptSystem.h"
#include "testing.h"
#include <RmlUi/Core.h>
#include <SDL3/SDL.h>
#include <sol/sol.hpp>
#include <string>

namespace testing::ui {

// The element with this id in any document of the UI context, or null
inline Rml::Element *element(Engine &engine, const char *id) {
  Rml::Context *context = engine.getUIContext();
  if (!context)
    return nullptr;
  for (int i = 0; i < context->GetNumDocuments(); ++i)
    if (Rml::Element *found = context->GetDocument(i)->GetElementById(id))
      return found;
  return nullptr;
}

inline void click(Engine &engine, const char *id) {
  Rml::Element *button = element(engine, id);
  CHECK(button != nullptr);
  if (button)
    button->Click();
}

inline SDL_WindowID windowId() {
  int count = 0;
  SDL_Window **windows = SDL_GetWindows(&count);
  const SDL_WindowID id = windows && count > 0 ? SDL_GetWindowID(windows[0]) : 0;
  SDL_free(windows);
  return id;
}

// A key event for the next frame
inline void pressKey(SDL_Scancode key, bool down) {
  SDL_Event event{};
  event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
  event.key.windowID = windowId();
  event.key.scancode = key;
  event.key.down = down;
  SDL_PushEvent(&event);
}

inline void pressEscape() {
  pressKey(SDL_SCANCODE_ESCAPE, true);
  pressKey(SDL_SCANCODE_ESCAPE, false);
}

inline bool gameInputBlocked(Engine &engine) {
  return engine.getSystemManager().getSystem<InputSystem>().isGameInputBlocked();
}

// A string field of the game script's global `game` table
inline std::string gameString(Engine &engine, const char *field) {
  return engine.scripts().lua()["game"][field].get_or<std::string>("");
}

} // namespace testing::ui
