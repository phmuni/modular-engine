#pragma once
// Game UI (menus, HUD) with RmlUi: RML/RCSS documents drawn over the scene and under the ImGui debug panels.
//
// Documents live in one Rml::Context the size of the window; build data models on it (getContext()->CreateDataModel)
// before loading the documents that use them. Clicks and key presses the UI handles do not reach the game.

#include "foundation/ecs/systemManager.h"
#include <SDL3/SDL.h>
#include <memory>
#include <string>

namespace Rml {
class Context;
class ElementDocument;
} // namespace Rml

class GameUISystem : public BaseSystem {
public:
  explicit GameUISystem(SDL_Window *window);
  ~GameUISystem() override;

  bool isAvailable() const;
  Rml::Context *getContext() const;

  // Fonts are found by the family name inside the file; a fallback font fills in glyphs the others lack
  bool loadFont(const std::string &path, bool fallback = false);
  // Relative paths resolve from the executable's folder; links inside the document resolve from the document
  Rml::ElementDocument *loadDocument(const std::string &path, bool show = true);

  // Feeds an SDL event to the UI. Returns true when the UI handled it, so the game should ignore it.
  // `mouseAllowed`/`keysAllowed` are false while something else owns that input (cursor lock, ImGui).
  bool processEvent(SDL_Event &event, bool mouseAllowed, bool keysAllowed);
  void update(bool showDebugger);
  void render();
  // Frees RmlUi and its GPU resources; must run while the OpenGL context is alive
  void shutdown();

private:
  struct Impl;
  std::unique_ptr<Impl> m_impl;
};
