#!/usr/bin/env python3
"""Fails when the C++ game API gets ahead of Lua: every public method of the facade classes (GameEntity, GameUI,
GameCamera, GameTime, GameTween) must be bound in src/systems/scriptSystem.cpp, or listed below with the reason it is C++ only.
Needs no build.

    python3 tools/check_api_parity.py

Together with tools/check_lua_api.py (every binding is described in templates/common/lua/types/engine.lua) this makes
a new C++ method fail CI until Lua has it and the editor's definitions describe it.

A method counts as bound when scriptSystem.cpp takes its address (`&GameUI::load`), or binds a Lua name made from it:
the same name, or `setX` / `isX` for the property or function `x`.
"""
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
}

# Classes named Game... that are not facades of the game API
NOT_FACADES = {"GameUISystem"}

# Bound under another Lua name: (class, method) -> the Lua name
ALIASES = {
    ("GameEntity", "hasScript"): "script",  # Lua's `script` is the entity itself or nil, a different type
    ("GameEntity", "color"): "getColor",  # not `color`: a script may keep its own field of that name
}

# Not bound on purpose: (class, method) -> why
C_PLUS_PLUS_ONLY = {
    ("GameTime", "tick"): "the engine calls it once per frame",
    ("GameTime", "clear"): "the engine calls it when a play session ends",
    ("GameTween", "tick"): "the engine calls it once per frame",
    ("GameTween", "clear"): "the engine calls it when a play session ends",
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
        for method in methods:
            total += 1
            key = (class_name, method)
            seen.add(key)
            if key in C_PLUS_PLUS_ONLY:
                continue
            if key in ALIASES:
                if ALIASES[key] not in lua_members:
                    problems.append(f"{class_name}::{method}: aliased to {owner}.{ALIASES[key]}, which is not bound")
                continue
            if f"&{class_name}::{method}" in code or lua_names_for(method) & lua_members:
                continue
            problems.append(f"{class_name}::{method}: not bound in Lua. Bind it in {BINDINGS.name} (as {owner}."
                            f"{method}), describe it in templates/common/lua/types/engine.lua, or list it in "
                            f"C_PLUS_PLUS_ONLY here with the reason")

    for key in list(ALIASES) + list(C_PLUS_PLUS_ONLY):
        if key not in seen:
            problems.append(f"{key[0]}::{key[1]}: listed in {Path(__file__).name} but no longer a public method")

    if problems:
        print("The C++ game API and the Lua bindings differ:")
        for problem in problems:
            print(f"  {problem}")
        sys.exit(1)
    print(f"Every one of the {total} public methods of {', '.join(FACADES)} is bound in Lua or listed as C++ only")


if __name__ == "__main__":
    main()
