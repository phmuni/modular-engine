// Lua bindings: the ui table (documents, pause menu, menus, loading screen).
// A binding only converts types and calls the C++ game API: see the rule at the top of luaBindings.h.

#include <sol/sol.hpp> // SOL_ALL_SAFETIES_ON comes from the build, the same for every file that includes sol2

#include "systems/lua/luaBindings.h"

#include "components/animator.h"
#include "components/camera.h"
#include "components/characterController.h"
#include "components/hidden.h"
#include "components/light.h"
#include "components/model.h"
#include "components/name.h"
#include "components/rigidBody.h"
#include "components/script.h"
#include "components/transform.h"
#include "foundation/core/config.h"
#include "foundation/core/engine.h"
#include "foundation/core/gameEntity.h"
#include "foundation/core/logger.h"
#include "systems/inputSystem.h"
#include "systems/navigationSystem.h"
#include "systems/physicsSystem.h"
#include "systems/playerSettings.h"
#include "systems/renderSystem.h"
#include "systems/saveData.h"
#include <RmlUi/Core.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <stdexcept>
#include <optional>
#include <tuple>
#include <nlohmann/json.hpp>
#include <unordered_map>
#include <variant>
#include <vector>

void LuaBindings::bindUi() {
  // ui: RmlUi documents, changed by element id
  sol::table ui = lua.create_named_table("ui");
  ui.set_function("load", &GameUI::load, &engine.ui());
  // ui.loadFont("fonts/Pixel.ttf"): documents use it by the family name inside the file; with fallback = true it
  // also supplies glyphs the other fonts lack
  ui.set_function("loadFont", [this](const std::string &path, sol::optional<bool> fallback) {
    return engine.ui().loadFont(path, fallback.value_or(false));
  });
  // The Menu a script holds: what ui.menu and ui.pauseMenu return
  lua.new_usertype<Menu>(
      "Menu", sol::no_constructor, "open",
      [](Menu &menu, sol::optional<std::string> screen) { menu.open(screen.value_or("")); }, "close", &Menu::close,
      "isOpen", &Menu::isOpen, "show", &Menu::show, "name", [](Menu &menu) { return menu.name(); });
  // The pause menu: ui.pauseMenu() returns its Menu (ui.pauseMenu():open("options")) and changes nothing. Escape opens it
  // once enabled (see systems/pauseMenu.h): ui.pauseMenu(true) or a table of options, which return the Menu as well. The
  // game script's onMenuOpened, onMenuClosed and onMenuAction are called from C++ with the name "pause".
  ui.set_function("pauseMenu", [this](sol::optional<sol::object> arg) -> Menu & {
    if (!arg)
      return engine.ui().pauseMenu();
    if (arg->is<sol::table>()) {
      const sol::table table = arg->as<sol::table>();
      PauseMenuOptions options;
      options.enabled = table.get_or("enabled", options.enabled);
      options.document = table.get_or("document", options.document);
      options.pause = table.get_or("pause", options.pause);
      options.freeCursor = table.get_or("freeCursor", options.freeCursor);
      options.blockInput = table.get_or("blockInput", options.blockInput);
      options.escape = table.get_or("escape", options.escape);
      return engine.ui().pauseMenu(options);
    }
    return engine.ui().pauseMenu(arg->is<bool>() ? arg->as<bool>() : true);
  });
  // Menus (see systems/menu.h): ui.menu("ui/mainMenu.rml", { pause = true, escape = "none" }) makes the menu for that
  // document, opens it and returns it; the game script's onMenuOpened(menu), onMenuClosed(menu) and
  // onMenuAction(menu, name) are called from C++. `escape` is "none", "back", "close" or "quit" (true is "close",
  // false is "none").
  ui.set_function("menu", [this](const std::string &path, sol::optional<sol::table> table) -> Menu & {
    MenuOptions options;
    if (table) {
      options.name = table->get_or("name", options.name);
      options.pause = table->get_or("pause", options.pause);
      options.freeCursor = table->get_or("freeCursor", options.freeCursor);
      options.blockInput = table->get_or("blockInput", options.blockInput);
      const sol::object escape = table->get<sol::object>("escape");
      if (escape.is<bool>())
        options.escape = escape.as<bool>() ? MenuEscape::Close : MenuEscape::None;
      else if (escape.is<std::string>()) {
        const std::string mode = escape.as<std::string>();
        if (mode == "none")
          options.escape = MenuEscape::None;
        else if (mode == "back")
          options.escape = MenuEscape::Back;
        else if (mode == "close")
          options.escape = MenuEscape::Close;
        else if (mode == "quit")
          options.escape = MenuEscape::Quit;
        else
          report("ui.menu", "ui.menu: escape is \"none\", \"back\", \"close\" or \"quit\", not \"" + mode + "\"");
      }
    }
    return engine.ui().menu(path, options);
  });
  // The loading screen: ui.loadingScreen{ document = "ui/loading.rml", delay = 0.2, minimumTime = 0.5 } shows that
  // document when a loadScene takes more than a moment (ui.loadingScreen(true) takes the defaults, false turns it
  // off). Its data model "loading" has progress (0 to 1) and percent (0 to 100).
  ui.set_function("loadingScreen", [this](sol::optional<sol::object> arg) {
    if (arg && arg->is<sol::table>()) {
      const sol::table table = arg->as<sol::table>();
      LoadingScreenOptions options;
      options.document = table.get_or("document", options.document);
      options.delay = table.get_or("delay", options.delay);
      options.minimumTime = table.get_or("minimumTime", options.minimumTime);
      engine.ui().loadingScreen(options);
    } else {
      engine.ui().loadingScreen(!arg || !arg->is<bool>() || arg->as<bool>());
    }
  });
  // Any value, written as Lua's tostring does (12, 1.5, true)
  ui.set_function("text", [this](const std::string &id, sol::object value) {
    engine.ui().text(id, lua["tostring"](value).get<std::string>());
  });
  ui.set_function("show", &GameUI::show, &engine.ui());
  ui.set_function("class", &GameUI::setClass, &engine.ui());
  // ui.style("panel", "background-color", "#204060"): one RCSS property, "" to go back to the stylesheet's
  ui.set_function("style", &GameUI::style, &engine.ui());
  ui.set_function("attribute", &GameUI::attribute, &engine.ui());
  ui.set_function("onClick", [this](const std::string &id, sol::protected_function handler) {
    return engine.ui().onClick(id, callback(std::move(handler), "ui.onClick"));
  });
}
