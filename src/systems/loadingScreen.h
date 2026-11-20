#pragma once
// The loading screen: a document of the project (ui/loading.rml) shown while a scene loads, so a level that takes a
// moment to arrive does not look like a frozen game. Turned on by the game (ui.loadingScreen{...} in Lua,
// engine.ui().loadingScreen(options) in C++); off otherwise.
//
// When. Engine::loadScene reads the scene's models on worker threads and switches at the end of the frame they are all
// ready, so the old scene keeps running meanwhile. The screen appears when that takes longer than `delay` (a load that
// is over by then never shows it), and stays at least `minimumTime` once shown, so it does not flash.
//
// The data model is named "loading". A document can use:
//   progress    how far the load is, from 0 to 1. It is real but coarse: the share of the scene's models that are read.
//               The scene's entities are made in one step at the end, and textures upload as they are first drawn;
//               neither is counted, so a scene with few models jumps from 0 to 1. A document that only animates
//               (RCSS @keyframes) loses nothing.
//   percent     progress as a whole number from 0 to 100, for text and widths (data-style-width="percent + '%'")
// The screen takes every click while it shows (the body should have pointer-events: auto).

#include "foundation/ecs/systemManager.h"
#include <memory>
#include <string>

class Engine;

struct LoadingScreenOptions {
  // The document, relative to assets/. Empty: ui/loading.rml
  std::string document;
  // Seconds a load takes before the screen appears. 0: any load that is not over in its first frame shows it.
  float delay = 0.2f;
  // Seconds it stays once shown, even if the scene arrived sooner
  float minimumTime = 0.5f;
};

class LoadingScreen : public BaseSystem {
public:
  explicit LoadingScreen(Engine &engine);
  ~LoadingScreen() override;

  // Turns the screen on with these options (its document loads now, hidden); a document that does not exist is a
  // warning and leaves it off
  void configure(const LoadingScreenOptions &options);
  // Off: no screen shows for the loads that follow
  void disable();
  bool isEnabled() const;
  // Whether the document is on screen now
  bool isShown() const;
  const LoadingScreenOptions &options() const;

  // Once per frame with the real time that passed, and what Engine says about the load
  void update(float deltaTime, bool loading, float progress);
  // Hides it and forgets the load in progress (a play session ending)
  void stop();

private:
  struct Impl;
  std::unique_ptr<Impl> m_impl;
};
