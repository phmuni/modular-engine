#!/usr/bin/env python3
"""Packages the engine prebuilt, for people who only write Lua (no compiler, no CMake, no engine sources).

    python3 tools/package_engine.py
    python3 tools/package_engine.py --cmake-arg=-DCPM_SDL_SOURCE=/path/to/sdl   # extra arguments for the configure step

Builds the launcher and the Lua runner in Release (in build-release/, so the development build and bin/ stay as they
are) and gathers dist/modular-engine-<platform>/:

    bin/launcher/   the launcher and SDL's library
    bin/runner/     the Lua runner, SDL's library and engine-assets (copied into each Lua project)
    templates/      the project templates
    README.md, LICENSE

The launcher finds the engine two folders above bin/launcher/, so the folder works wherever it is put. It has no
sources: creating a C++ project from it makes one that downloads the engine from GitHub (and needs CMake and a
compiler), while Lua projects need nothing else.
"""

import argparse
import platform
import shutil
import subprocess
import sys
from pathlib import Path

ENGINE_DIR = Path(__file__).resolve().parent.parent
# What a run leaves next to the programs
RUN_LEFTOVERS = shutil.ignore_patterns("cache", "*.ini", "*.bmp", "*.log", "*.pdb", "*.ilk")


def platform_name():
    system = {"Darwin": "macos", "Windows": "windows", "Linux": "linux"}.get(platform.system(), platform.system().lower())
    machine = platform.machine().lower().replace("amd64", "x86_64").replace("aarch64", "arm64")
    return f"{system}-{machine}"


def run(command):
    print("$", " ".join(command), flush=True)
    if subprocess.run(command).returncode != 0:
        sys.exit("package_engine: that failed")


def main():
    parser = argparse.ArgumentParser(description="Package the engine prebuilt into dist/.")
    parser.add_argument("--cmake-arg", action="append", default=[], help="an extra argument for cmake's configure step")
    parser.add_argument("--build-dir", default=str(ENGINE_DIR / "build-release"),
                        help="where to build (default: build-release)")
    args = parser.parse_args()

    build = Path(args.build_dir).resolve()
    run(["cmake", "-S", str(ENGINE_DIR), "-B", str(build), "-DCMAKE_BUILD_TYPE=Release",
         "-DMODULAR_ENGINE_BUILD_EXAMPLES=OFF", f"-DMODULAR_ENGINE_BIN_DIR={build / 'bin'}", *args.cmake_arg])
    run(["cmake", "--build", str(build), "--config", "Release", "--target", "launcher", "runner", "-j"])

    package = ENGINE_DIR / "dist" / f"modular-engine-{platform_name()}"
    shutil.rmtree(package, ignore_errors=True)
    for program in ("launcher", "runner"):
        shutil.copytree(build / "bin" / program, package / "bin" / program, symlinks=True, ignore=RUN_LEFTOVERS)
    shutil.copytree(ENGINE_DIR / "templates", package / "templates", ignore=shutil.ignore_patterns("build", "bin"))
    for name in ("README.md", "LICENSE"):
        shutil.copy2(ENGINE_DIR / name, package / name)
    print(f"\nPackaged the engine in {package}")


if __name__ == "__main__":
    main()
