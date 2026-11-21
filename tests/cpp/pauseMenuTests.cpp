// The pause menu and the player's settings: a document using the generic data model (clicked through RmlUi), the
// options (pause, input, cursor, Escape), the callbacks, which document is used, and the saved choices that are applied
// when the game starts without any menu. The Lua side is in tests/lua/pause_menu.lua and settings.lua.

#include "foundation/core/config.h"
#include "foundation/core/engine.h"
#include "systems/inputSystem.h"
#include "systems/pauseMenu.h"
#include "systems/playerSettings.h"
#include "systems/saveData.h"
#include "systems/scriptSystem.h"
#include "testing.h"
#include "uiHelpers.h"
#include <RmlUi/Core.h>
#include <SDL3/SDL.h>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sol/sol.hpp>

using namespace testing::ui;

namespace {
const char *kDocument = "../tests/fixtures/ui/pause_menu.rml";
const char *kGameScript = "../tests/fixtures/pause_menu_game.lua";

// Whether the element is in the document and shown (data-if hides what is not on the screen)
bool shown(Engine &engine, const char *id) {
  Rml::Element *found = element(engine, id);
  return found && found->IsVisible();
}

int gameNumber(Engine &engine, const char *field) { return engine.scripts().lua()["game"][field].get_or(0); }

PauseMenuOptions withDocument(const char *document) {
  PauseMenuOptions options;
  options.document = document;
  return options;
}
} // namespace

// Every name of the data model, through the buttons of a document: set, step, toggle, show and back, action, resume
TEST_CASE(pause_menu_events) {
  CHECK(engine.scripts().runGameScript(EngineConfig::resolvePath(kGameScript))); // absolute: kept as it is
  engine.pauseMenu().configure(withDocument(kDocument));
  CHECK_EQ(engine.pauseMenu().documentPath(), std::string(kDocument));
  auto actions = std::make_shared<std::string>();
  auto opened = std::make_shared<int>(0);
  auto closed = std::make_shared<int>(0);
  engine.ui().onPauseMenuAction([actions](const std::string &name) { *actions += name; });
  engine.ui().onPauseMenuOpened([opened]() { ++*opened; });
  engine.ui().onPauseMenuClosed([closed]() { ++*closed; });
  engine.settings().set("fxaa", false);
  engine.settings().set("ssaoQuality", std::string("low"));
  engine.pauseMenu().open();

  Engine *game = &engine;
  steps.step(2, [game, opened] {
    CHECK(game->pauseMenu().isOpen());
    CHECK(game->getState(Toggle::Paused)); // opening pauses
    CHECK_EQ(*opened, 1);
    CHECK_EQ(gameNumber(*game, "opened"), 1); // the game script's onMenuOpened
    Rml::Element *label = element(*game, "msaa_label");
    CHECK(label != nullptr);
    if (label)
      CHECK_EQ(label->GetInnerRML(), std::string("MSAA 2x")); // labels.msaa
    click(*game, "set_msaa");
  });
  steps.step(2, [game] {
    CHECK_EQ(game->msaa(), 4); // set('msaa', 4)
    CHECK_NEAR(static_cast<float>(game->saveData().getNumber("engine.msaa")), 4.0f, 1e-6f);
    Rml::Element *label = element(*game, "msaa_label");
    if (label)
      CHECK_EQ(label->GetInnerRML(), std::string("MSAA 4x")); // the label follows
    click(*game, "step_msaa");
  });
  steps.step(2, [game] {
    CHECK(game->msaa() == 8 || game->msaa() == 0); // step('msaa', 1): the next level (Off if the GPU stops at 4)
    click(*game, "toggle_fxaa");
  });
  steps.step(2, [game] {
    CHECK(game->fxaa()); // toggle('fxaa')
    CHECK(game->saveData().getBool("engine.fxaa"));
    click(*game, "set_quality");
  });
  steps.step(2, [game] {
    const auto quality = game->settings().get("ssaoQuality");
    CHECK_EQ(std::get<std::string>(quality), std::string("high")); // a text value
    // The slider: the document writes its value as text, like RmlUi's range input does
    Rml::Element *slider = element(*game, "master");
    CHECK(slider != nullptr);
    if (slider) {
      slider->SetAttribute("value", Rml::String("0.35"));
      slider->DispatchEvent("change", Rml::Dictionary{{"value", Rml::Variant(Rml::String("0.35"))}});
    }
  });
  steps.step(2, [game] {
    CHECK_NEAR(static_cast<float>(std::get<double>(game->settings().get("masterVolume"))), 0.35f, 1e-6f);
    CHECK_NEAR(static_cast<float>(game->saveData().getNumber("engine.volume.master")), 0.35f, 1e-6f);
    Rml::Element *label = element(*game, "master_label");
    if (label)
      CHECK_EQ(label->GetInnerRML(), std::string("35%"));
    // And the other way: the game changes it, the document shows it
    game->settings().set("masterVolume", 0.6);
  });
  steps.step(2, [game] {
    Rml::Element *label = element(*game, "master_label");
    if (label)
      CHECK_EQ(label->GetInnerRML(), std::string("60%"));
    Rml::Element *slider = element(*game, "master");
    if (slider)
      CHECK_NEAR(slider->GetAttribute<float>("value", -1.0f), 0.6f, 1e-5f);
    click(*game, "action");
  });
  steps.step(2, [game, actions] {
    CHECK_EQ(*actions, std::string("hello")); // action('hello') reaches C++...
    CHECK_EQ(gameString(*game, "action"), std::string("hello")); // ...and the game script's onMenuAction
    click(*game, "show_second");
  });
  steps.step(2, [game] {
    CHECK(!shown(*game, "first")); // screen == 'second'
    CHECK(shown(*game, "second"));
    click(*game, "back");
  });
  steps.step(2, [game] {
    CHECK(game->pauseMenu().isOpen()); // back() from a second screen goes to the first
    CHECK(shown(*game, "first"));
    click(*game, "resume");
  });
  steps.step(2, [game, closed] {
    CHECK(!game->pauseMenu().isOpen());
    CHECK(!game->getState(Toggle::Paused));
    CHECK_EQ(*closed, 1);
    CHECK_EQ(gameNumber(*game, "closed"), 1);
    // The choices were kept in the save data when the menu closed
    CHECK(game->saveData().has("engine.fxaa"));
  });
}

// Escape: opens, goes back from a screen, closes; with escape = false it does nothing
TEST_CASE(pause_menu_escape) {
  PauseMenuOptions options = withDocument(kDocument);
  engine.pauseMenu().configure(options);
  Engine *game = &engine;
  steps.step(2, [game] {
    CHECK(!game->pauseMenu().isOpen());
    pressEscape();
  });
  steps.step(3, [game] {
    CHECK(game->pauseMenu().isOpen()); // Escape opened it
    click(*game, "show_second");
  });
  steps.step(2, [game] {
    pressEscape();
  });
  steps.step(3, [game] {
    CHECK(game->pauseMenu().isOpen()); // from the second screen: back to the first
    CHECK(shown(*game, "first"));
    pressEscape();
  });
  steps.step(3, [game] {
    CHECK(!game->pauseMenu().isOpen()); // from the first: out of the menu
    PauseMenuOptions options = withDocument(kDocument);
    options.escape = false;
    game->pauseMenu().configure(options);
    pressEscape();
  });
  steps.step(3, [game] {
    CHECK(!game->pauseMenu().isOpen()); // escape = false: only code opens it
    game->pauseMenu().open();
  });
  steps.step(2, [game] {
    CHECK(game->pauseMenu().isOpen());
    pressEscape();
  });
  steps.step(3, [game] {
    CHECK(game->pauseMenu().isOpen()); // and Escape does not close it either
    game->pauseMenu().close();
    PauseMenuOptions options = withDocument(kDocument);
    options.enabled = false;
    game->pauseMenu().configure(options);
    pressEscape();
  });
  steps.step(3, [game] {
    CHECK(!game->pauseMenu().isOpen()); // not enabled: Escape does nothing
  });
}

// pause, freeCursor and blockInput, each on its own
TEST_CASE(pause_menu_behavior) {
  Engine *game = &engine;
  game->setState(Toggle::CursorLock, true);
  // All of it (the defaults)
  game->pauseMenu().configure(withDocument(kDocument));
  game->pauseMenu().open();
  steps.step(2, [game] {
    CHECK(game->getState(Toggle::Paused));
    CHECK(!game->getState(Toggle::CursorLock)); // the cursor is the menu's
    CHECK(gameInputBlocked(*game));
    game->setMouseCaptured(true); // the game asks for the mouse while the menu has it: it gets it after
    CHECK(!game->getState(Toggle::CursorLock));
    game->pauseMenu().close();
  });
  steps.step(2, [game] {
    CHECK(!game->getState(Toggle::Paused));
    CHECK(game->getState(Toggle::CursorLock)); // given back as the game asked for
    CHECK(!gameInputBlocked(*game));

    // None of it: the game runs, keeps the mouse and its input under the menu
    PauseMenuOptions options = withDocument(kDocument);
    options.pause = false;
    options.freeCursor = false;
    options.blockInput = false;
    game->pauseMenu().configure(options);
    game->pauseMenu().open();
  });
  steps.step(2, [game] {
    CHECK(game->pauseMenu().isOpen());
    CHECK(!game->getState(Toggle::Paused));
    CHECK(game->getState(Toggle::CursorLock));
    CHECK(!gameInputBlocked(*game));
    CHECK(!game->pauseMenu().blocksInput());
    CHECK(!game->pauseMenu().showsCursor());
    game->pauseMenu().close();
  });
  steps.step(2, [game] {
    CHECK(game->getState(Toggle::CursorLock));
    CHECK(!game->getState(Toggle::Paused));

    // Only the input blocked
    PauseMenuOptions options = withDocument(kDocument);
    options.pause = false;
    options.freeCursor = false;
    game->pauseMenu().configure(options);
    game->pauseMenu().open();
  });
  steps.step(2, [game] {
    CHECK(gameInputBlocked(*game));
    CHECK(!game->getState(Toggle::Paused));
    CHECK(game->pauseMenu().blocksInput());
    game->pauseMenu().close();
  });
  steps.step(2, [game] { CHECK(!gameInputBlocked(*game)); });
}

// pause = false: game time keeps advancing under the menu; with it on, it stands still
TEST_CASE(pause_menu_game_time) {
  Engine *game = &engine;
  auto start = std::make_shared<float>(0.0f);
  PauseMenuOptions options = withDocument(kDocument);
  options.pause = false;
  game->pauseMenu().configure(options);
  steps.step(2, [game, start] {
    *start = game->time().elapsed();
    game->pauseMenu().open();
  });
  steps.step(30, [game, start] {
    CHECK(game->pauseMenu().isOpen());
    CHECK_NEAR(game->time().elapsed() - *start, 0.5f, 0.05f);
    game->pauseMenu().close();
    game->pauseMenu().configure(withDocument(kDocument));
    game->pauseMenu().open();
    *start = game->time().elapsed();
  });
  steps.step(30, [game, start] {
    CHECK(game->pauseMenu().isOpen());
    CHECK_NEAR(game->time().elapsed() - *start, 0.0f, 0.02f);
    game->pauseMenu().close();
  });
}

namespace {
// A project's own pause menu document, as a new project has in assets/ui. It is written to a temporary folder that
// stands in front of the game's assets/ (EngineConfig::assetsOverlay): nothing is written into the repository, so
// tests running at the same time never see it.
void createProjectDocument() {
  const std::string folder = testing::tempDir("project_assets");
  std::filesystem::create_directories(folder + "/ui");
  std::ofstream(folder + "/ui/pauseMenu.rml")
      << "<rml><head><title>Project</title></head>"
         "<body data-model=\"pause_menu\"><div id=\"project_copy\">mine</div></body></rml>";
  EngineConfig::assetsOverlay() = folder;
}
} // namespace

// Which document: the option's, else the project's ui/pauseMenu.rml, else the engine's own copy
TEST_CASE(pause_menu_document_lookup) {
  PauseMenu &menu = engine.pauseMenu();
  const std::string engineCopy = "../assets/ui/engine/pauseMenu.rml";
  // Nothing configured and no project file: the engine's
  CHECK(!std::filesystem::exists(EngineConfig::resolveAsset("ui/pauseMenu.rml")));
  menu.configure(PauseMenuOptions{});
  CHECK_EQ(menu.documentPath(), engineCopy);
  // A missing custom document: a warning, and the same lookup
  menu.configure(withDocument("ui/no_such_pause_menu.rml"));
  CHECK_EQ(menu.documentPath(), engineCopy);
  // The option's document, when it exists
  menu.configure(withDocument(kDocument));
  CHECK_EQ(menu.documentPath(), std::string(kDocument));
  // The project's file beats the engine's copy (and the option still beats the project's)
  createProjectDocument();
  menu.configure(withDocument("ui/no_such_pause_menu.rml"));
  CHECK_EQ(menu.documentPath(), std::string("ui/pauseMenu.rml"));
  menu.configure(withDocument(kDocument));
  CHECK_EQ(menu.documentPath(), std::string(kDocument));
  menu.configure(PauseMenuOptions{});
  CHECK_EQ(menu.documentPath(), std::string("ui/pauseMenu.rml"));
  CHECK(element(engine, "project_copy") != nullptr);
  menu.open();
  CHECK(menu.isOpen());
  menu.close();
}

// ui().pauseMenu() with no argument only gives the Menu: it neither enables the pause menu nor changes its options; the
// overloads with an argument configure it and give the same Menu back
TEST_CASE(pause_menu_accessor_changes_nothing) {
  PauseMenu &settings = engine.pauseMenu();
  settings.configure(withDocument(kDocument));
  settings.setEnabled(false);
  Menu &menu = engine.ui().pauseMenu();
  CHECK_EQ(menu.name(), std::string("pause"));
  CHECK(!settings.isEnabled());
  CHECK_EQ(settings.documentPath(), std::string(kDocument));
  CHECK(&engine.ui().pauseMenu(true) == &menu);
  CHECK(settings.isEnabled());
  PauseMenuOptions options = withDocument(kDocument);
  options.pause = false;
  CHECK(&engine.ui().pauseMenu(options) == &menu);
  CHECK(!settings.options().pause);
  CHECK(&engine.ui().pauseMenu() == &menu);
  CHECK(!settings.options().pause); // still as configured
  menu.open();
  CHECK(menu.isOpen());
  menu.close();
  settings.configure(PauseMenuOptions{});
}

// Saved choices are applied at start-up, with no menu enabled, over the game's own defaults and only where there is a
// saved choice (the STARTUP function runs before Engine::run, as the game's configuration and the save file would)
STARTUP(settings_at_startup) {
  // The game's defaults (project.json's "graphics")
  engine.setMsaa(4);
  engine.setFsr(true);
  engine.setFxaa(false);
  // What the player chose, under the keys saves have always used
  save.set("engine.msaa", 2);
  save.set("engine.fxaa", true);
  save.set("engine.volume.master", 0.3);
  save.set("engine.volume.music", 0.2);
  save.set("engine.renderScale", 0.75);
  save.set("engine.fsrSharpness", 0.5);
  save.set("engine.ssao", false);
  save.set("engine.ssaoQuality", "high");
}

TEST_CASE(settings_at_startup) {
  CHECK(!engine.pauseMenu().isEnabled()); // no menu involved
  // The saved choice wins over the game's default...
  CHECK_EQ(engine.msaa(), 2);
  CHECK(engine.fxaa());
  CHECK_NEAR(static_cast<float>(engine.renderScale()), 0.75f, 1e-6f);
  CHECK_NEAR(engine.fsrSharpness(), 0.5f, 1e-6f);
  CHECK(!engine.ssao());
  const auto quality = engine.settings().get("ssaoQuality");
  CHECK_EQ(std::get<std::string>(quality), std::string("high"));
  CHECK_NEAR(static_cast<float>(std::get<double>(engine.settings().get("masterVolume"))), 0.3f, 1e-6f);
  CHECK_NEAR(static_cast<float>(std::get<double>(engine.settings().get("musicVolume"))), 0.2f, 1e-6f);
  // ...and the game's default stays where nothing was saved
  CHECK(engine.fsr());

  // Back to the game's defaults, not the engine's
  engine.settings().reset("msaa");
  CHECK_EQ(engine.msaa(), 4);
  CHECK(!engine.saveData().has("engine.msaa"));
  engine.settings().resetAll();
  CHECK(!engine.fxaa());
  CHECK(engine.fsr());
  CHECK_NEAR(static_cast<float>(std::get<double>(engine.settings().get("masterVolume"))), 1.0f, 1e-6f);
  CHECK(!engine.saveData().has("engine.volume.master"));
  // resetAll turned VSync back on (its default): the test host keeps it off, or a covered window waits for the
  // display at every frame (macOS) and the test runs out of time
  engine.setState(Toggle::VSync, false);
}

// A game that names itself in setup has its own save applied then; what another save had applied goes back to the
// defaults where the new one has no choice
STARTUP(settings_game_name) {
  engine.setMsaa(4);
  save.set("engine.msaa", 2);
  save.set("engine.fxaa", true);
}

TEST_CASE(settings_game_name) {
  CHECK_EQ(engine.msaa(), 2);
  CHECK(engine.fxaa());
  engine.setGameName("OtherEngineTestGame"); // no saved choices: the game's defaults
  CHECK_EQ(engine.msaa(), 4);
  CHECK(!engine.fxaa());
  engine.saveData().set("engine.fxaa", true); // a choice in the new save, applied by the next apply
  engine.settings().applySaved();
  CHECK(engine.fxaa());
  CHECK_EQ(engine.msaa(), 4);
}
