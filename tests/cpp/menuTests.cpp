// Menus (src/systems/menu.h) and the loading screen (src/systems/loadingScreen.h): options, actions reaching the game,
// the stack of menus and back, Escape in each mode, and the screen shown while a scene loads. The pause menu's own tests
// are in pauseMenuTests.cpp; the Lua side is in tests/lua/menu.lua.

#include "foundation/core/config.h"
#include "foundation/core/engine.h"
#include "systems/inputSystem.h"
#include "systems/loadingScreen.h"
#include "systems/menu.h"
#include "systems/pauseMenu.h"
#include "systems/scriptSystem.h"
#include "testing.h"
#include "uiHelpers.h"
#include <RmlUi/Core.h>
#include <SDL3/SDL.h>
#include <memory>
#include <sol/sol.hpp>

using namespace testing::ui;

namespace {
const char *kMain = "../tests/fixtures/ui/menu_main.rml";
const char *kOptions = "../tests/fixtures/ui/menu_options.rml";
const char *kPauseDocument = "../tests/fixtures/ui/pause_menu.rml";
const char *kLoading = "../tests/fixtures/ui/loading.rml";
const char *kScene = "../tests/fixtures/loading_scene.json";
const char *kGameScript = "../tests/fixtures/menu_game.lua";

// Whether the document that holds the element is shown (menus below the top one are hidden)
bool documentShown(Engine &engine, const char *id) {
  Rml::Element *found = element(engine, id);
  return found && found->GetOwnerDocument()->IsVisible();
}

MenuOptions named(const char *name) {
  MenuOptions options;
  options.name = name;
  return options;
}
} // namespace

// pause, freeCursor and blockInput each decide on their own, for a menu that is not the pause menu
TEST_CASE(menu_options) {
  Engine *game = &engine;
  game->setState(Toggle::CursorLock, true);
  MenuOptions all = named("all");
  Menu *menu = &game->ui().menu(kMain, all); // opens it
  CHECK_EQ(menu->name(), std::string("all"));
  steps.step(2, [game, menu] {
    CHECK(menu->isOpen());
    CHECK_EQ(menu->documentPath(), std::string(kMain));
    CHECK(game->getState(Toggle::Paused));
    CHECK(!game->getState(Toggle::CursorLock)); // freed for the menu
    CHECK(gameInputBlocked(*game));
    CHECK(documentShown(*game, "main_play"));
    CHECK(game->menus().showsCursor());
    menu->close();
  });
  steps.step(2, [game, menu] {
    CHECK(!menu->isOpen());
    CHECK(!game->getState(Toggle::Paused));
    CHECK(game->getState(Toggle::CursorLock)); // given back as it was
    CHECK(!gameInputBlocked(*game));
    CHECK(!documentShown(*game, "main_play"));
    // None of the three: the game goes on under it
    MenuOptions none = named("all");
    none.pause = false;
    none.freeCursor = false;
    none.blockInput = false;
    game->ui().menu(kMain, none); // the same menu again, with other options
    CHECK(menu->options().pause == false);
  });
  steps.step(2, [game, menu] {
    CHECK(menu->isOpen());
    CHECK(!game->getState(Toggle::Paused));
    CHECK(game->getState(Toggle::CursorLock));
    CHECK(!gameInputBlocked(*game));
    CHECK(!game->menus().showsCursor());
    game->ui().menu(kMain, named("all")); // already open: still one opening
    menu->close();
  });
  steps.step(2, [menu] { CHECK(!menu->isOpen()); });
}

// A document that is not there: a warning, and the menu does not open
TEST_CASE(menu_missing_document) {
  Menu *menu = &engine.ui().menu("ui/no_such_menu.rml");
  steps.step(2, [menu] {
    CHECK(!menu->isOpen());
    CHECK(menu->documentPath().empty());
  });
}

// action('name') reaches the game: Lua's onMenuAction(menu, name), and the C++ callback; the pause menu's too (and its
// own onPauseMenuAction); onMenuOpened and onMenuClosed with the name
TEST_CASE(menu_actions_and_callbacks) {
  CHECK(engine.scripts().runGameScript(EngineConfig::resolvePath(kGameScript)));
  auto actions = std::make_shared<std::string>();
  auto events = std::make_shared<std::string>();
  engine.ui().onMenuAction([actions](const std::string &menu, const std::string &name) { *actions += menu + ":" + name + ";"; });
  engine.ui().onMenuOpened([events](const std::string &menu) { *events += "+" + menu; });
  engine.ui().onMenuClosed([events](const std::string &menu) { *events += "-" + menu; });
  Menu *menu = &engine.ui().menu(kMain); // named after the file: menu_main
  CHECK_EQ(menu->name(), std::string("menu_main"));
  auto own = std::make_shared<int>(0);
  menu->onAction([own](const std::string &) { ++*own; });

  Engine *game = &engine;
  steps.step(2, [game] {
    CHECK_EQ(gameString(*game, "opened"), std::string("menu_main;"));
    click(*game, "main_play");
  });
  steps.step(2, [game, actions, own] {
    CHECK_EQ(gameString(*game, "action"), std::string("menu_main:play"));
    CHECK_EQ(*actions, std::string("menu_main:play;"));
    CHECK_EQ(*own, 1);
    click(*game, "main_close");
  });
  steps.step(2, [game, events] {
    CHECK_EQ(gameString(*game, "closed"), std::string("menu_main;"));
    CHECK_EQ(*events, std::string("+menu_main-menu_main"));
    // The pause menu: its action reaches the generic callback with the name "pause", and onPauseMenuAction as before
    PauseMenuOptions options;
    options.document = kPauseDocument;
    game->pauseMenu().configure(options);
    game->ui().openPauseMenu();
  });
  steps.step(2, [game] { click(*game, "action"); });
  steps.step(2, [game, actions] {
    CHECK_EQ(gameString(*game, "action"), std::string("pause:hello"));
    CHECK_EQ(gameString(*game, "pauseAction"), std::string("hello"));
    CHECK_EQ(*actions, std::string("menu_main:play;pause:hello;"));
    game->closePauseMenu();
  });
}

// A menu opened over another takes the input; closing it shows the one below again; the game's state is given back
// in order
TEST_CASE(menu_stack_and_back) {
  Engine *game = &engine;
  game->setState(Toggle::CursorLock, true);
  Menu *main = &game->ui().menu(kMain);
  steps.step(2, [game] {
    CHECK(documentShown(*game, "main_play"));
    click(*game, "main_options"); // open('...menu_options.rml')
  });
  steps.step(2, [game, main] {
    Menu *options = game->menus().find("menu_options");
    CHECK(options != nullptr);
    CHECK(options && options->isOpen());
    CHECK(main->isOpen()); // still on the stack...
    CHECK(!documentShown(*game, "main_play")); // ...but out of sight
    CHECK(documentShown(*game, "options_screen"));
    CHECK_EQ(game->menus().topName(), std::string("menu_options"));
    CHECK(game->getState(Toggle::Paused)); // the one below pauses; the one on top leaves it alone
    click(*game, "options_action"); // no game script here: nothing to call, and no failure
  });
  steps.step(2, [game] {
    click(*game, "options_back"); // back() on the first screen closes it
  });
  steps.step(2, [game, main] {
    Menu *options = game->menus().find("menu_options");
    CHECK(options && !options->isOpen());
    CHECK(main->isOpen());
    CHECK(documentShown(*game, "main_play")); // shown again
    CHECK(!documentShown(*game, "options_screen"));
    CHECK_EQ(game->menus().topName(), std::string("menu_main"));
    CHECK(game->getState(Toggle::Paused));
    CHECK(!game->getState(Toggle::CursorLock));
    click(*game, "main_options");
  });
  steps.step(2, [game, main] {
    // Closing the one below closes what is over it, and gives the state back
    main->close();
    Menu *options = game->menus().find("menu_options");
    CHECK(options && !options->isOpen());
    CHECK(!main->isOpen());
    CHECK(!game->menus().anyOpen());
  });
  steps.step(2, [game] {
    CHECK(!game->getState(Toggle::Paused));
    CHECK(game->getState(Toggle::CursorLock));
    CHECK(!gameInputBlocked(*game));
  });
}

// Escape goes to the menu on top only, and does what that menu's option says
TEST_CASE(menu_escape_modes) {
  Engine *game = &engine;
  // none: a main menu stays; what is opened over it closes with Escape
  MenuOptions none = named("main");
  none.escape = MenuEscape::None;
  Menu *main = &game->ui().menu(kMain, none);
  steps.step(2, [game] { click(*game, "main_options"); });
  steps.step(2, [game] { pressEscape(); });
  steps.step(3, [game, main] {
    Menu *options = game->menus().find("menu_options");
    CHECK(options && !options->isOpen()); // Escape closed the menu on top
    CHECK(main->isOpen());
    pressEscape();
  });
  steps.step(3, [game, main] {
    CHECK(main->isOpen()); // escape = none: nothing
    main->close();
    MenuOptions back = named("main");
    back.escape = MenuEscape::Back;
    game->ui().menu(kMain, back);
  });
  steps.step(2, [game] { click(*game, "main_second"); });
  steps.step(2, [game] {
    CHECK(documentShown(*game, "main_back"));
    pressEscape();
  });
  steps.step(3, [game, main] {
    CHECK(main->isOpen());
    CHECK(documentShown(*game, "main_play")); // back: the first screen
    pressEscape();
  });
  steps.step(3, [game, main] {
    CHECK(main->isOpen()); // back never closes on the first screen
    main->close();
    game->ui().menu(kMain, named("main")); // close, the default
  });
  steps.step(2, [game] { pressEscape(); });
  steps.step(3, [game, main] { CHECK(!main->isOpen()); });
}

// Escape with the pause menu enabled does not open it over another menu
TEST_CASE(menu_escape_does_not_open_pause_over_a_menu) {
  Engine *game = &engine;
  PauseMenuOptions options;
  options.document = kPauseDocument;
  game->pauseMenu().configure(options);
  MenuOptions none = named("main");
  none.escape = MenuEscape::None;
  game->ui().menu(kMain, none);
  steps.step(2, [] { pressEscape(); });
  steps.step(3, [game] {
    CHECK(!game->isPauseMenuOpen());
    CHECK_EQ(game->menus().topName(), std::string("main"));
    game->menus().find("main")->close();
    pressEscape();
  });
  steps.step(3, [game] { CHECK(game->isPauseMenuOpen()); }); // and it opens when no menu is
}

// What the screen does as a load goes on, fed the way Engine feeds it every frame. Tests run deterministically, where a
// scene switches in the frame it was asked for and never stays pending, so the load is simulated here (the whole
// sequence in one step, before the engine's own call for that frame, which sees no load).
TEST_CASE(loading_screen_during_a_load) {
  LoadingScreenOptions options;
  options.document = kLoading;
  options.delay = 0.25f;
  options.minimumTime = 0.3f;
  engine.ui().loadingScreen(options);
  Engine *game = &engine;
  LoadingScreen *screen = &engine.loadingScreen();
  CHECK(screen->isEnabled());
  CHECK(!screen->isShown());
  CHECK(!documentShown(*game, "loading_percent"));
  auto percent = [game]() {
    game->getUIContext()->Update(); // the data model writes its change into the document
    Rml::Element *text = element(*game, "loading_percent");
    return text ? text->GetInnerRML() : std::string("none");
  };
  steps.step(1, [game, screen, percent] {
    screen->update(0.1f, true, 0.0f);
    CHECK(!screen->isShown()); // less than `delay` into the load
    screen->update(0.1f, true, 0.0f);
    CHECK(!screen->isShown());
    screen->update(0.1f, true, 0.25f); // 0.3 seconds: due
    CHECK(screen->isShown());
    CHECK(documentShown(*game, "loading_percent"));
    CHECK_EQ(percent(), std::string("25"));
    screen->update(0.1f, true, 0.6f);
    CHECK_EQ(percent(), std::string("60")); // the progress follows
    screen->update(0.1f, false, 1.0f);      // the scene arrived...
    CHECK(screen->isShown());               // ...but the screen stays for its minimum time (0.2 so far)
    CHECK_EQ(percent(), std::string("100"));
    screen->update(0.1f, false, 1.0f);
    CHECK(!screen->isShown()); // 0.3: closed
    CHECK(!documentShown(*game, "loading_percent"));
    // A load over before `delay` never shows it
    screen->update(0.1f, true, 0.0f);
    screen->update(0.1f, false, 1.0f);
    CHECK(!screen->isShown());
    // stop() ends one in progress (a play session ending)
    screen->update(0.3f, true, 0.5f);
    CHECK(screen->isShown());
    screen->stop();
    CHECK(!screen->isShown());
  });
}

// A load that is over before `delay` never shows it; turned off, it does not show at all
TEST_CASE(loading_screen_delay_and_off) {
  LoadingScreenOptions options;
  options.document = kLoading;
  options.delay = 5.0f;
  engine.ui().loadingScreen(options);
  Engine *game = &engine;
  LoadingScreen *screen = &engine.loadingScreen();
  steps.step(1, [game] { game->loadScene(kScene); });
  steps.until([game]() { return !game->isLoadingScene(); }, "the scene to load", 300);
  steps.step(2, [game, screen] {
    CHECK(!screen->isShown()); // done long before 5 seconds
    game->ui().loadingScreen(false);
    CHECK(!screen->isEnabled());
    game->loadScene(kScene);
  });
  steps.until([game]() { return !game->isLoadingScene(); }, "the scene to load again", 300);
  steps.step(2, [screen] { CHECK(!screen->isShown()); });
}

// A loading document that is not there leaves the screen off, with a warning
TEST_CASE(loading_screen_missing_document) {
  LoadingScreenOptions options;
  options.document = "ui/no_such_loading.rml";
  engine.ui().loadingScreen(options);
  CHECK(!engine.loadingScreen().isEnabled());
}
