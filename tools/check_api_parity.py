#!/usr/bin/env python3
"""Fails when the C++ game API gets ahead of Lua: every public method of the facade classes (GameEntity, GameUI,
GameCamera, GameTime, GameTween, PlayerSettings) and of Engine (src/foundation/core/engine.h) must be bound in src/systems/scriptSystem.cpp, or listed below with the reason it is C++ only.
Needs no build.

    python3 tools/check_api_parity.py

Together with tools/check_lua_api.py (every binding is described in templates/common/lua/types/engine.lua) this makes
a new C++ method fail CI until Lua has it and the editor's definitions describe it.

A method counts as bound when scriptSystem.cpp takes its address (`&GameUI::load`), or binds a Lua name made from it:
the same name, or `setX` / `isX` for the property or function `x`. Engine's methods live in several Lua tables (engine,
sound, input, physics...) or on Entity, so ENGINE_ALIASES says where a method went when its name differs.
"""
import argparse
import re
import sys
from pathlib import Path

import check_lua_api

ENGINE_DIR = Path(__file__).resolve().parent.parent
BINDINGS = ENGINE_DIR / "src" / "systems" / "scriptSystem.cpp"

# Facade class -> (header, what it is called in Lua: a usertype or a table of functions)
FACADES = {
    "GameEntity": ("src/foundation/core/gameEntity.h", "Entity"),
    "GameUI": ("src/systems/gameUI.h", "ui"),
    "GameCamera": ("src/systems/gameCamera.h", "camera"),
    "GameTime": ("src/systems/gameTime.h", "time"),
    "GameTween": ("src/systems/gameTween.h", "tween"),
    "PlayerSettings": ("src/systems/playerSettings.h", "settings"),
    "Engine": ("src/foundation/core/engine.h", "engine"),
}

# Classes named Game... that are not facades of the game API
NOT_FACADES = {"GameUISystem"}

# Bound under another Lua name: (class, method) -> the Lua name
ALIASES = {
    ("GameEntity", "hasScript"): "script",  # Lua's `script` is the entity itself or nil, a different type
    ("GameEntity", "color"): "getColor",  # not `color`: a script may keep its own field of that name
}

# Engine methods bound under another name or in another Lua table: method -> "table.name" (Entity.name for a method
# of the entity handle). An engine function that is only a different name in the `engine` table needs no table.
ENGINE_ALIASES = {
    "destroyEntity": "destroy",
    "setParent": "Entity.setParent",
    "getParent": "Entity.parent",
    "getChildren": "Entity.children",
    "findEntity": "find",
    "isAlive": "Entity.valid",
    "hasComponent": "Entity.has",
    "instantiate": "spawn",  # spawn is the same call for the game API, returning a handle
    "playAnimation": "Entity.play",
    "getAnimationClips": "Entity.clips",
    "setAnimator": "Entity.setAnimator",
    "setAnimationParam": "Entity.setParam",
    "triggerAnimation": "Entity.trigger",
    "getAnimationParam": "Entity.param",
    "getAnimationState": "Entity.animationState",
    "raycast": "physics.raycastBoxes",
    "raycastModels": "physics.raycastModels",
    "pickModel": "pick",
    "isColliding": "Entity.isTouching",
    "fitCollisionToModel": "Entity.fitCollision",
    "setEmission": "Entity.setEmission",
    "setTexture": "Entity.setTexture",
    "setShininess": "Entity.setShininess",
    "setSolidColor": "Entity.setColor",
    "setMouseCaptured": "input.captureMouse",
    "isMouseCaptured": "input.isMouseCaptured",
    "setCursorVisible": "input.showCursor",
    "isCursorVisible": "input.isCursorVisible",
    "preloadModel": "preload",
    "setState": "setToggle",
    "getState": "isToggled",
    "preloadSound": "sound.preload",
    "playSound": "sound.play",
    "playSoundAt": "sound.playAt",
    "stopSound": "sound.stop",
    "setSoundVolume": "sound.setVolume",
    "isSoundPlaying": "sound.isPlaying",
    "playMusic": "sound.music",
    "stopMusic": "sound.stopMusic",
    "setMusicVolume": "sound.musicVolume",
    "isMusicPlaying": "sound.isMusicPlaying",
    "getScenePath": "scenePath",
    "loadUIDocument": "ui.load",
    "loadUIFont": "ui.loadFont",
    "enablePauseMenu": "ui.pauseMenu",
    "openPauseMenu": "ui.openPauseMenu",
    "closePauseMenu": "ui.closePauseMenu",
    "isPauseMenuOpen": "ui.isPaused",
    "captureScreenshot": "screenshot",
    "setWindowTitle": "setTitle",
    "getAmbientColor": "getAmbient",
    "getAmbientIntensity": "getAmbient",
}

# Not bound on purpose: (class, method) -> why
C_PLUS_PLUS_ONLY = {
    ("GameTime", "tick"): "the engine calls it once per frame",
    ("GameTime", "clear"): "the engine calls it when a play session ends",
    ("GameTween", "tick"): "the engine calls it once per frame",
    ("GameTween", "clear"): "the engine calls it when a play session ends",
    ("GameUI", "onPauseMenuOpened"): "C++ callback; Lua scripts define onPauseMenuOpened() in the game script",
    ("GameUI", "onPauseMenuClosed"): "C++ callback; Lua scripts define onPauseMenuClosed() in the game script",
    ("GameUI", "onPauseMenuAction"): "C++ callback; Lua scripts define onPauseMenuAction(name) in the game script",
    ("GameUI", "onMenuOpened"): "C++ callback; Lua scripts define onMenuOpened(menu) in the game script",
    ("GameUI", "onMenuClosed"): "C++ callback; Lua scripts define onMenuClosed(menu) in the game script",
    ("GameUI", "onMenuAction"): "C++ callback; Lua scripts define onMenuAction(menu, name) in the game script",
    ("PlayerSettings", "captureDefaults"): "the engine calls it once at start-up (Engine::run)",
    ("PlayerSettings", "applySaved"): "the engine calls it at start-up and when the game names itself",
}

# Engine methods that stay C++ only: method -> why. Anything else public on Engine must be bound (or aliased above).
ENGINE_C_PLUS_PLUS_ONLY = {
    # Lifecycle: the game's main() or the Lua runner calls these
    "shutdown": "lifecycle",
    "initialize": "lifecycle",
    "run": "lifecycle (the Lua runner starts the loop)",
    "setGameName": "the Lua runner sets it from project.json's name",
    "configureWindow": "the Lua runner applies project.json's window; scripts use setWindowSize and setFullscreen",
    "configureGraphics": "the Lua runner applies project.json's graphics; scripts use setMsaa, setFxaa, setFsr and setFsrSharpness",
    "setDeterministic": "set before start-up, for tests (--deterministic)",
    "runBenchmark": "command line (--benchmark)",
    "parseCommandLine": "the game's main() reads the command line",
    "commandLineHelp": "the game's main() reads the command line",
    "applyCommandLine": "the game's main() reads the command line",
    "saveSceneAfter": "command line (--save-scene)",
    # Accessors of C++ objects: Lua has a table or handle for each
    "getSystemManager": "engine internals",
    "getComponentManager": "engine internals",
    "getEntityManager": "engine internals",
    "entities": "C++ builder; engine.create takes a table",
    "entity": "a handle from an entity number; Lua gets its handles from find, create and spawn",
    "ui": "accessor; the Lua ui table",
    "camera": "accessor; the Lua camera table",
    "time": "accessor; the Lua time table",
    "tween": "accessor; the Lua tween table",
    "physics": "accessor; the Lua physics table",
    "navigation": "accessor; the Lua nav table",
    "input": "accessor; the Lua input table",
    "scripts": "accessor of the script system",
    "layers": "accessor; physics.addLayer, setLayersCollide and layersCollide",
    "saveData": "accessor; the Lua save table",
    "settings": "accessor; the Lua settings table",
    "pauseMenu": "accessor of the pause menu system; the Lua ui.pauseMenu, ui.openPauseMenu... and the script callbacks",
    "menus": "accessor of the menu system; the Lua ui.menu and the Menu handle it returns",
    "loadingScreen": "accessor of the loading screen; the Lua ui.loadingScreen",
    "getUIContext": "raw RmlUi, for C++ data models",
    "getActiveCamera": "raw Camera; the Lua camera table",
    "getSceneSerializer": "C++ game components in scene files",
    "registerComponent": "C++ game components (Lua games use fields on the entity)",
    "getReflectedComponents": "C++ game components (editor)",
    # Templates and ECS access by type
    "get": "template over C++ component types",
    "tryGet": "template over C++ component types",
    "has": "template over C++ component types; entity:has takes a name",
    "removeComponent": "template over C++ component types",
    "forEachCollision": "template over C++ component types; entity:isTouching",
    "forEachContact": "template over C++ component types; the onContact callback",
    "onAnimationStateChanged": "C++ listener; Lua scripts define onStateChanged(self, old, new)",
    "findCollisions": "C++ iteration of Collision pairs; entity:isTouching asks about one pair",
    # Raw entity creation: Lua has engine.create, which does the same from a description
    "createEntity": "an empty entity; engine.create",
    "createModelEntity": "engine.create takes a model in its table",
    "createLightEntity": "C++ builder; the Lua entity's light",
    "createCameraEntity": "the scene's camera is the active one, driven by the camera table",
    "setLocalTransform": "a C++ editor/hierarchy call; scripts move entities with position, rotation and scale",
    # Editor and tooling
    "getPlayMode": "editor",
    "edit": "editor",
    "resume": "editor",
    "play": "editor",
    "stop": "editor",
    "beginCheckpoint": "editor undo",
    "endCheckpoint": "editor undo",
    "hasOpenCheckpoint": "editor undo",
    "isSceneDirty": "editor",
    "undo": "editor",
    "redo": "editor",
    "canUndo": "editor",
    "canRedo": "editor",
    "saveScene": "tooling: writes scene files, which games author in the editor (save is for player data)",
    "setSsaoDebugView": "editor tuning aid (Settings > Interface); games do not show the raw AO",
    "ssaoDebugView": "editor tuning aid (Settings > Interface); games do not show the raw AO",
    "savePrefab": "tooling: writes prefab files, which games author in the editor",
}


def class_body(code, name):
    """The text between the braces of `class Name { ... }`, or None."""
    match = re.search(rf"\bclass\s+{name}\b[^;{{]*\{{", code)
    if not match:
        return None
    depth = 1
    i = match.end()
    while depth:
        if code[i] == "{":
            depth += 1
        elif code[i] == "}":
            depth -= 1
        i += 1
    return code[match.end():i - 1]


def public_methods(body, class_name):
    """Names of the public member functions declared directly in a class body (constructors, destructors, operators,
    friends and data members left out)."""
    statements = []
    depth = 0
    current = []
    for c in body:
        if c == "{":
            if depth == 0:
                current.append("{}")  # an inline body or initializer: kept as a marker
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0 and "(" in "".join(current):
                statements.append("".join(current))  # a function with an inline body ends here
                current = []
        elif depth == 0:
            current.append(c)
            if c == ";":
                statements.append("".join(current))
                current = []
    names = []
    public = False  # a class starts private
    for statement in statements:
        text = " ".join(statement.split())
        while match := re.match(r"(public|private|protected)\s*:\s*", text):
            public = match.group(1) == "public"
            text = text[match.end():]
        if not public or not text:
            continue
        if text.startswith(("friend ", "using ", "typedef ", "struct ", "class ", "enum ")):
            continue
        before_paren = text.split("(")[0] if "(" in text else ""
        if "(" not in text or "=" in before_paren or "operator" in before_paren:
            continue  # a data member, or an operator
        if "= delete" in text:
            continue
        found = re.search(r"(~?\w+)\s*\(", text)
        name = found.group(1)
        if name == class_name or name == "~" + class_name:
            continue
        if name not in names:
            names.append(name)
    return names


def split_alias(alias, owner):
    """"table.name" as (table, name); a bare name is in `owner`."""
    return tuple(alias.split(".", 1)) if "." in alias else (owner, alias)


def lua_names_for(method):
    names = {method}
    for prefix in ("set", "is"):
        if method.startswith(prefix) and len(method) > len(prefix) and method[len(prefix)].isupper():
            rest = method[len(prefix):]
            names.add(rest[0].lower() + rest[1:])
    return names


def main():
    code = check_lua_api.strip_comments(BINDINGS.read_text(encoding="utf-8"))
    bound = check_lua_api.bound_names(code)
    problems = []
    total = 0

    # A new facade class must be added to FACADES, or it is not checked at all
    for header in sorted((ENGINE_DIR / "src").rglob("*.h")):
        text = check_lua_api.strip_comments(header.read_text(encoding="utf-8"))
        for name in re.findall(r"\bclass\s+(Game\w+)\b\s*(?:final\s*)?[:{]", text):
            if name not in FACADES and name not in NOT_FACADES:
                problems.append(f"{name} ({header.relative_to(ENGINE_DIR)}): a game API class that "
                                f"{Path(__file__).name} does not know; add it to FACADES (or NOT_FACADES)")

    seen = set()
    for class_name, (header, owner) in FACADES.items():
        path = ENGINE_DIR / header
        body = class_body(check_lua_api.strip_comments(path.read_text(encoding="utf-8")), class_name)
        if body is None:
            problems.append(f"{class_name}: not found in {header}")
            continue
        methods = public_methods(body, class_name)
        if not methods:
            problems.append(f"{class_name}: no public methods found in {header} (is this script out of date?)")
        lua_members = bound.get(owner, set())
        if owner not in bound.get("", set()):
            problems.append(f"{class_name}: nothing called {owner} is bound in {BINDINGS.name}")
        engine = class_name == "Engine"
        for method in methods:
            total += 1
            key = (class_name, method)
            seen.add(key)
            if key in C_PLUS_PLUS_ONLY or (engine and method in ENGINE_C_PLUS_PLUS_ONLY):
                continue
            alias = ENGINE_ALIASES.get(method) if engine else ALIASES.get(key)
            if alias:
                table, name = split_alias(alias, owner)
                if name not in bound.get(table, set()):
                    problems.append(f"{class_name}::{method}: aliased to {table}.{name}, which is not bound")
                continue
            if f"&{class_name}::{method}" in code or lua_names_for(method) & lua_members:
                continue
            problems.append(f"{class_name}::{method}: not bound in Lua. Bind it in {BINDINGS.name} (as {owner}."
                            f"{method}), describe it in templates/common/lua/types/engine.lua, or list it in "
                            f"{'ENGINE_C_PLUS_PLUS_ONLY' if engine else 'C_PLUS_PLUS_ONLY'} here with the reason")

    for key in list(ALIASES) + list(C_PLUS_PLUS_ONLY) + [("Engine", m) for m in ENGINE_ALIASES] + \
            [("Engine", m) for m in ENGINE_C_PLUS_PLUS_ONLY]:
        if key not in seen:
            problems.append(f"{key[0]}::{key[1]}: listed in {Path(__file__).name} but no longer a public method")
    for method in ENGINE_ALIASES.keys() & ENGINE_C_PLUS_PLUS_ONLY.keys():
        problems.append(f"Engine::{method}: both aliased and listed as C++ only in {Path(__file__).name}")

    if problems:
        print("The C++ game API and the Lua bindings differ:")
        for problem in problems:
            print(f"  {problem}")
        sys.exit(1)
    print(f"Every one of the {total} public methods of {', '.join(FACADES)} is bound in Lua or listed as C++ only")


if __name__ == "__main__":
    # No options: only --help, which says what the tool does
    argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter).parse_args()
    main()
