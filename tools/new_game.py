#!/usr/bin/env python3
"""Creates a game project on modular-engine from templates/game.

    python3 tools/new_game.py ~/Games/Asteroids            # the name is the folder's: Asteroids
    python3 tools/new_game.py ~/Games/asteroids --name Asteroids

The project builds against this engine checkout (its path goes into the project's CMakeLists.txt, as
MODULAR_ENGINE_DIR; change it there, or empty it to download the engine from GitHub instead):

    cd ~/Games/Asteroids
    cmake -B build && cmake --build build -j
    ./bin/Asteroids
"""

import argparse
import re
import shutil
import sys
from pathlib import Path

ENGINE_DIR = Path(__file__).resolve().parent.parent
TEMPLATE = ENGINE_DIR / "templates" / "game"
PLACEHOLDER = "MyGame"


def main():
    parser = argparse.ArgumentParser(description="Create a game project on modular-engine.")
    parser.add_argument("folder", help="where to create it (must not exist yet, or be empty)")
    parser.add_argument("--name", help="the game's name: its executable, CMake project and window title "
                                       "(default: the folder's name)")
    args = parser.parse_args()

    folder = Path(args.folder).expanduser().resolve()
    name = args.name or folder.name
    # Used as a C++ class name and a CMake target, so letters, digits and underscores
    if not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", name):
        sys.exit(f"'{name}' cannot be a C++ class or CMake target name; pass --name with letters, digits and _")
    if folder.exists() and any(folder.iterdir()):
        sys.exit(f"{folder} is not empty")

    shutil.copytree(TEMPLATE, folder, dirs_exist_ok=True, ignore=shutil.ignore_patterns("build", "bin"))
    for path in [folder / "CMakeLists.txt", folder / "src" / "main.cpp", folder / "README.md"]:
        text = path.read_text()
        text = text.replace(PLACEHOLDER, name).replace("@ENGINE_DIR@", ENGINE_DIR.as_posix())
        path.write_text(text)

    print(f"Created {name} in {folder}\n")
    print(f"  cd {folder}")
    print("  cmake -B build && cmake --build build -j")
    print(f"  ./bin/{name}")


if __name__ == "__main__":
    main()
