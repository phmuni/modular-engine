#!/usr/bin/env python3
"""Creates a game project on modular-engine.

    python3 tools/new_game.py ~/Games/Asteroids --type lua     # scripts only: runs right away, nothing to compile
    python3 tools/new_game.py ~/Games/Asteroids --type cpp     # C++ built with CMake
    python3 tools/new_game.py ~/Games/Asteroids --type mixed   # C++ and Lua scripts
    python3 tools/new_game.py ~/Games/Asteroids                # asks which
    python3 tools/new_game.py ~/Games/Asteroids --type lua --empty   # an empty project instead of the example game

The name is the folder's unless --name gives one. Each type starts as the same small game (walk, jump, push crates,
collect coins) written its way, or with --empty as an empty project: a camera, a light, a sky and a floor, the game
code stubbed (templates/empty has what differs from the example; its keep.txt lists the example's files it keeps). C++ projects build against this engine checkout (its path goes into their
CMakeLists.txt as MODULAR_ENGINE_DIR; empty in the prebuilt package, which has no sources, so the engine is
downloaded); Lua projects get a copy of the engine's prebuilt runner (bin/runner), built
first if needed.
"""

import argparse
import re
import shutil
import subprocess
import sys
from pathlib import Path

ENGINE_DIR = Path(__file__).resolve().parent.parent
TEMPLATES = ENGINE_DIR / "templates"
RUNNER_DIR = ENGINE_DIR / "bin" / "runner"
PLACEHOLDER = "MyGame"
TYPES = {
    "lua": "Lua: scripts only, nothing to compile",
    "cpp": "C++: game code built with CMake",
    "mixed": "C++ + Lua: C++ code and Lua scripts together",
}


def copy_template(name, folder, only=None):
    """templates/<name> into the folder; with `only`, just those paths (a folder's name takes what is in it)."""
    source = TEMPLATES / name
    if only is None:
        shutil.copytree(source, folder, dirs_exist_ok=True, ignore=shutil.ignore_patterns("build", "bin", ".DS_Store"))
        return
    for path in source.rglob("*"):
        relative = path.relative_to(source).as_posix()
        if path.is_file() and any(relative == keep or relative.startswith(keep + "/") for keep in only):
            target = folder / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(path, target)


def read_keep_list():
    """The example template's files an empty project keeps (templates/empty/keep.txt)."""
    lines = (TEMPLATES / "empty" / "keep.txt").read_text().splitlines()
    return [line.strip() for line in lines if line.strip() and not line.startswith("#")]


def copy_project_files(kind, folder, empty):
    """The template's files: the example game's, or the empty project's (the example's kept files with
    templates/empty/<type> over them). A mixed project is the C++ template with the mixed files over it."""
    base = "lua" if kind == "lua" else "cpp"
    if empty:
        copy_template(base, folder, read_keep_list())
        copy_template("empty/" + base, folder)
        if kind == "mixed":
            copy_template("empty/mixed", folder)
    else:
        copy_template(base, folder)
        if kind == "mixed":
            copy_template("mixed", folder)  # over the C++ one: its main.cpp, scripts and prefab


def fill_placeholders(folder, name):
    for path in folder.rglob("*"):
        if path.is_file() and path.suffix in {".txt", ".cpp", ".h", ".md", ".json", ".lua", ".rml"}:
            text = path.read_text()
            # With no engine sources here (the prebuilt package), an empty directory makes the project download the engine
            engine_dir = ENGINE_DIR.as_posix() if (ENGINE_DIR / "CMakeLists.txt").exists() else ""
            new = text.replace(PLACEHOLDER, name).replace("@ENGINE_DIR@", engine_dir)
            if new != text:
                path.write_text(new)


def runner_executable():
    return RUNNER_DIR / ("runner.exe" if sys.platform == "win32" else "runner")


def ensure_runner():
    """The prebuilt Lua runner, built from this checkout the first time it is needed."""
    if runner_executable().exists():
        return True
    if not (ENGINE_DIR / "CMakeLists.txt").exists():
        print(f"This engine has no Lua runner ({runner_executable()}) and no sources to build one from.")
        return False
    print("Building the Lua runner (once)...")
    build = ENGINE_DIR / "build"
    steps = [["cmake", "-S", str(ENGINE_DIR), "-B", str(build)],
             ["cmake", "--build", str(build), "--target", "runner", "-j"]]
    for step in steps:
        if subprocess.run(step).returncode != 0:
            return False
    return runner_executable().exists()


def install_runner(folder, name):
    """bin/ gets the runner (renamed after the game), SDL's library and the engine's own assets."""
    bin_dir = folder / "bin"
    shutil.copytree(RUNNER_DIR, bin_dir, dirs_exist_ok=True, ignore=shutil.ignore_patterns("cache", ".DS_Store"))
    runner = bin_dir / runner_executable().name
    game = bin_dir / (name + (".exe" if sys.platform == "win32" else ""))
    runner.rename(game)
    game.chmod(0o755)


def ask_type():
    print("What kind of project?")
    keys = list(TYPES)
    for i, key in enumerate(keys, 1):
        print(f"  {i}. {TYPES[key]}")
    while True:
        answer = input("Choose 1-3: ").strip()
        if answer in {"1", "2", "3"}:
            return keys[int(answer) - 1]
        if answer in TYPES:
            return answer


def create(folder, name, kind, empty=False):
    """Creates the project; returns the commands that build and run it."""
    if kind == "lua":
        if not ensure_runner():
            sys.exit("Could not build the Lua runner; build the engine first (cmake -B build && cmake --build build)")
        copy_project_files(kind, folder, empty)
        copy_template("common/lua", folder)  # the editor's Lua settings and API definitions
        fill_placeholders(folder, name)
        install_runner(folder, name)
        return [f"./bin/{name}"]
    copy_project_files(kind, folder, empty)
    if kind == "mixed":
        copy_template("common/lua", folder)  # the editor's Lua settings and API definitions
    fill_placeholders(folder, name)
    return ["cmake -B build && cmake --build build -j", f"./bin/{name}"]


def main():
    parser = argparse.ArgumentParser(description="Create a game project on modular-engine.")
    parser.add_argument("folder", help="where to create it (must not exist yet, or be empty)")
    parser.add_argument("--type", choices=list(TYPES), help="lua, cpp or mixed (asked when left out)")
    parser.add_argument("--name", help="the game's name: its executable, window title and C++ class "
                                       "(default: the folder's name)")
    parser.add_argument("--empty", action="store_true",
                        help="an empty project (a camera, a light, a sky, a floor and stubbed game code) "
                             "instead of the example game")
    args = parser.parse_args()

    folder = Path(args.folder).expanduser().resolve()
    name = args.name or folder.name
    # Used as a C++ class name and a CMake target, so letters, digits and underscores
    if not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", name):
        sys.exit(f"'{name}' cannot be a C++ class or CMake target name; pass --name with letters, digits and _")
    if folder.exists() and any(folder.iterdir()):
        sys.exit(f"{folder} is not empty")
    kind = args.type or ask_type()

    commands = create(folder, name, kind, args.empty)
    print(f"\nCreated {name} ({TYPES[kind]}{', empty' if args.empty else ''}) in {folder}\n")
    print(f"  cd {folder}")
    for command in commands:
        print(f"  {command}")


if __name__ == "__main__":
    main()
