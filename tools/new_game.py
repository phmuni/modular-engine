#!/usr/bin/env python3
"""Creates a game project on modular-engine.

    python3 tools/new_game.py ~/Games/Asteroids --type lua     # scripts only: runs right away, nothing to compile
    python3 tools/new_game.py ~/Games/Asteroids --type cpp     # C++ built with CMake
    python3 tools/new_game.py ~/Games/Asteroids --type mixed   # C++ and Lua scripts
    python3 tools/new_game.py ~/Games/Asteroids                # asks which

The name is the folder's unless --name gives one. Each type starts as the same small game (walk, jump, push crates,
collect coins) written its way. C++ projects build against this engine checkout (its path goes into their
CMakeLists.txt as MODULAR_ENGINE_DIR); Lua projects get a copy of the engine's prebuilt runner (bin/runner), built
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


def copy_template(name, folder):
    shutil.copytree(TEMPLATES / name, folder, dirs_exist_ok=True, ignore=shutil.ignore_patterns("build", "bin"))


def fill_placeholders(folder, name):
    for path in folder.rglob("*"):
        if path.is_file() and path.suffix in {".txt", ".cpp", ".h", ".md", ".json", ".lua"}:
            text = path.read_text()
            new = text.replace(PLACEHOLDER, name).replace("@ENGINE_DIR@", ENGINE_DIR.as_posix())
            if new != text:
                path.write_text(new)


def runner_executable():
    return RUNNER_DIR / ("runner.exe" if sys.platform == "win32" else "runner")


def ensure_runner():
    """The prebuilt Lua runner, built from this checkout the first time it is needed."""
    if runner_executable().exists():
        return True
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
    shutil.copytree(RUNNER_DIR, bin_dir, dirs_exist_ok=True, ignore=shutil.ignore_patterns("cache"))
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


def create(folder, name, kind):
    """Creates the project; returns the commands that build and run it."""
    if kind == "lua":
        if not ensure_runner():
            sys.exit("Could not build the Lua runner; build the engine first (cmake -B build && cmake --build build)")
        copy_template("lua", folder)
        fill_placeholders(folder, name)
        install_runner(folder, name)
        return [f"./bin/{name}"]
    copy_template("cpp", folder)
    if kind == "mixed":
        copy_template("mixed", folder)  # over the C++ one: its main.cpp, level, scripts and HUD
    fill_placeholders(folder, name)
    return ["cmake -B build && cmake --build build -j", f"./bin/{name}"]


def main():
    parser = argparse.ArgumentParser(description="Create a game project on modular-engine.")
    parser.add_argument("folder", help="where to create it (must not exist yet, or be empty)")
    parser.add_argument("--type", choices=list(TYPES), help="lua, cpp or mixed (asked when left out)")
    parser.add_argument("--name", help="the game's name: its executable, window title and C++ class "
                                       "(default: the folder's name)")
    args = parser.parse_args()

    folder = Path(args.folder).expanduser().resolve()
    name = args.name or folder.name
    # Used as a C++ class name and a CMake target, so letters, digits and underscores
    if not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", name):
        sys.exit(f"'{name}' cannot be a C++ class or CMake target name; pass --name with letters, digits and _")
    if folder.exists() and any(folder.iterdir()):
        sys.exit(f"{folder} is not empty")
    kind = args.type or ask_type()

    commands = create(folder, name, kind)
    print(f"\nCreated {name} ({TYPES[kind]}) in {folder}\n")
    print(f"  cd {folder}")
    for command in commands:
        print(f"  {command}")


if __name__ == "__main__":
    main()
