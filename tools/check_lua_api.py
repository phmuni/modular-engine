#!/usr/bin/env python3
"""Fails when something src/systems/scriptSystem.cpp binds for Lua is missing from the editor definitions
(templates/common/lua/types/engine.lua), so the autocomplete cannot fall behind the API. Needs no build.

    python3 tools/check_lua_api.py
"""
import argparse
import re
import sys
from pathlib import Path

ENGINE_DIR = Path(__file__).resolve().parent.parent
SOURCE = ENGINE_DIR / "src" / "systems" / "scriptSystem.cpp"
DEFINITIONS = ENGINE_DIR / "templates" / "common" / "lua" / "types" / "engine.lua"
LUARC = ENGINE_DIR / "templates" / "common" / "lua" / ".luarc.json"

# Bound but not described on purpose: the Lua extension already knows these (the standard library)
ALLOWED = {"print"}  # same as Lua's print; only the output goes to the engine's log


def strip_comments(text):
    """Blanks // and /* */ comments, keeping string literals (and the line structure)."""
    out = []
    i = 0
    while i < len(text):
        two = text[i:i + 2]
        if text[i] == '"':
            j = i + 1
            while text[j] != '"':
                j += 2 if text[j] == "\\" else 1
            out.append(text[i:j + 1])
            i = j + 1
        elif two == "//":
            j = text.find("\n", i)
            i = len(text) if j < 0 else j
        elif two == "/*":
            j = text.find("*/", i)
            i = len(text) if j < 0 else j + 2
        else:
            out.append(text[i])
            i += 1
    return "".join(out)


def usertype_members(code):
    """{type name: members} from lua.new_usertype<...>("Name", ...): the string literals directly inside the call
    (not in lambdas or nested calls) are the name, then the member names."""
    found = {}
    for match in re.finditer(r"new_usertype<[^>]*>\(", code):
        depth = 1
        i = match.end()
        strings = []
        while depth:
            c = code[i]
            if c in "([{":
                depth += 1
            elif c in ")]}":
                depth -= 1
            elif c == '"':
                j = i + 1
                while code[j] != '"':
                    j += 2 if code[j] == "\\" else 1
                if depth == 1:
                    strings.append(code[i + 1:j])
                i = j
            i += 1
        found[strings[0]] = strings[1:]
    return found


def bound_names(code):
    """Everything bound as {Owner: set(names)}; Owner is "" for globals."""
    bound = {}
    tables = {}  # C++ variable -> Lua table name
    for var, name in re.findall(r'sol::table\s+(\w+)\s*=\s*lua\.create_named_table\("(\w+)"\)', code):
        tables[var] = name
    for name in re.findall(r'lua\.create_named_table\("(\w+)"\)', code):  # gameTable = ...: no C++ variable
        tables.setdefault(name, name)
    for name in tables.values():
        bound.setdefault("", set()).add(name)
    for var, name in re.findall(r'(\w+)\.set_function\("(\w+)"', code):
        if var == "lua":
            bound.setdefault("", set()).add(name)
        elif var in tables:
            bound.setdefault(tables[var], set()).add(name)
        else:
            sys.exit(f"check_lua_api: {var}.set_function(\"{name}\") on a table this script does not know")
    for var, name in re.findall(r'(\w+)\["(\w+)"\]\s*=[^=]', code):
        if var == "lua":
            bound.setdefault("", set()).add(name)
        elif var in tables:
            bound.setdefault(tables[var], set()).add(name)
    for name, members in usertype_members(code).items():
        bound.setdefault("", set()).add(name)
        bound.setdefault(name, set()).update(members)
    return bound


def callbacks(code):
    """The functions the engine calls in scripts, and the table it reads from them."""
    names = set(re.findall(r'call\([^,]+,\s*"(\w+)"', code))
    names |= set(re.findall(r'notify\([^;]*?"(on\w+)"', code))
    if 'raw_get<sol::object>("properties")' in code:
        names.add("properties")
    return names


def defined_names(text):
    """{Owner: set(names)} the definitions describe: `function Owner.name` / `Owner:name`, the ---@field lines of a
    ---@class, and globals assigned at the top level (`engine = {}`) or defined as functions (`function wait() end`)."""
    defined = {}
    owner = None
    for line in text.splitlines():
        if match := re.match(r"---@class\s+(\w+)", line):
            owner = match.group(1)
            defined.setdefault(owner, set())
        elif match := re.match(r"---@field\s+(\w+)", line):
            if owner:
                defined[owner].add(match.group(1))
        elif match := re.match(r"(?:local\s+)?function\s+(\w+)[.:](\w+)\s*\(", line):
            defined.setdefault(match.group(1), set()).add(match.group(2))
        elif match := re.match(r"function\s+(\w+)\s*\(", line):  # a global function: `function wait(seconds) end`
            defined.setdefault("", set()).add(match.group(1))
        elif match := re.match(r"(?:local\s+)?(\w+)\s*=\s*(?:\{\}|nil)", line):
            defined.setdefault("", set()).add(match.group(1))
        if line and not line.startswith("---"):
            owner = None
    # `local Entity = {}` declares the class Entity; its name is a type, which scripts only see through values
    defined.setdefault("", set()).update(name for name in defined if name)
    return defined


def main():
    code = strip_comments(SOURCE.read_text(encoding="utf-8"))
    definitions = DEFINITIONS.read_text(encoding="utf-8")
    defined = defined_names(definitions)

    missing = []
    total = 0
    for owner, names in sorted(bound_names(code).items()):
        for name in sorted(names - ALLOWED if owner == "" else names):
            total += 1
            if name not in defined.get(owner, set()):
                missing.append(f"{owner}.{name}" if owner else name)

    luarc = LUARC.read_text(encoding="utf-8")
    for name in sorted(callbacks(code)):
        total += 1
        if not re.search(rf"^{name}\s*=", definitions, re.M):
            missing.append(f"{name} (script callback or table; define it in {DEFINITIONS.name})")
        if f'"{name}"' not in luarc:
            missing.append(f"{name} (script global; add it to diagnostics.globals in {LUARC.name})")

    if missing:
        print(f"{DEFINITIONS.relative_to(ENGINE_DIR)} lacks what {SOURCE.name} binds:")
        for name in missing:
            print(f"  {name}")
        sys.exit(1)
    print(f"Lua API definitions cover all {total} names bound in {SOURCE.name}")


if __name__ == "__main__":
    # No options: only --help, which says what the tool does
    argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter).parse_args()
    main()
