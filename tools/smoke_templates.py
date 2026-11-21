#!/usr/bin/env python3
"""Creates a project of each kind with the launcher, runs its game and drives its screens.

The six projects are the ones `launcher --create` makes: Lua, C++ and C++ + Lua (mixed), each as the example game and as
an empty project. Every game runs with --deterministic and `--script tools/smoke/<example|empty>.lua`, which plays it
the way a player would, by clicking the buttons of its documents (ui.click): the example games go main menu -> Options
-> Back -> Play -> pause menu (Options, Resume) -> game over (Try again, Main menu) -> Play; the empty ones open the
pause menu, visit Options and Resume. A game fails the test when it exits with an error, hangs (timeout), logs an
error (ERR or FTL lines, which include script errors) or never reaches the script's "SMOKE OK" line.

The C++ and mixed projects are built against this checkout first (the same cmake calls as CI: a full engine build in
the project's own build folder, so it takes minutes). A folder that already holds the project is used as it is: CI's
"Create a project of each kind" and "Build the C++ + Lua game" steps leave theirs in $RUNNER_TEMP, and one that is
built (bin/<Name> exists) is not built again.

    python3 tools/smoke_templates.py                      all six, built into a temporary folder
    python3 tools/smoke_templates.py --cpp none           only the Lua projects (seconds)
    python3 tools/smoke_templates.py --cpp examples       skip the empty C++ and mixed projects' builds
    python3 tools/smoke_templates.py LuaGame MixedGame    only these (LuaGame CppGame MixedGame EmptyLua EmptyCpp EmptyMixed)
    python3 tools/smoke_templates.py --work-dir ~/smoke   keep the projects there (reused by the next run)
    python3 tools/smoke_templates.py --cmake-arg=-DCPM_SDL_SOURCE=... --cmake-arg=...   for the cmake of C++ projects

It needs a display and an OpenGL 4.1 context like the tests: on a machine without one,
`xvfb-run -a -s "-screen 0 1280x720x24" python3 tools/smoke_templates.py`. bin/launcher and the engine's other
binaries must be built. Standard library only.
"""

import argparse
import os
import shutil
import subprocess
import sys
import tempfile
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BIN = os.path.join(ROOT, "bin")
SCRIPTS = os.path.join(ROOT, "tools", "smoke")
EXE_SUFFIX = ".exe" if os.name == "nt" else ""
RUN_TIMEOUT_SECONDS = 120
OK_LINE = "SMOKE OK"

# (folder and program name, launcher kind, empty project)
PROJECTS = [
    ("LuaGame", "lua", False),
    ("CppGame", "cpp", False),
    ("MixedGame", "mixed", False),
    ("EmptyLua", "lua", True),
    ("EmptyCpp", "cpp", True),
    ("EmptyMixed", "mixed", True),
]


def run(command, **options):
    return subprocess.run(command, capture_output=True, text=True, encoding="utf-8", errors="replace", **options)


def tail(text, lines=25):
    return "\n".join(text.strip().splitlines()[-lines:])


def create(name, kind, empty, work_dir, env):
    folder = os.path.join(work_dir, name)
    if os.path.isdir(folder) and os.listdir(folder):
        return folder
    launcher = os.path.join(BIN, "launcher", "launcher" + EXE_SUFFIX)
    command = [launcher, "--create", folder, kind] + (["--empty"] if empty else [])
    done = run(command, env=env, timeout=600)  # a Lua project may build the runner first
    if done.returncode != 0:
        raise RuntimeError("launcher --create failed:\n" + tail(done.stdout + done.stderr))
    return folder


def build(folder, name, cmake_args, jobs):
    if os.path.exists(os.path.join(folder, "bin", name + EXE_SUFFIX)):
        return
    build_dir = os.path.join(folder, "build")
    configured = run(["cmake", "-S", folder, "-B", build_dir, "-DCMAKE_BUILD_TYPE=Release", *cmake_args])
    if configured.returncode != 0:
        raise RuntimeError("cmake configure failed:\n" + tail(configured.stdout + configured.stderr))
    built = run(["cmake", "--build", build_dir, "--config", "Release", "--parallel", str(jobs)])
    if built.returncode != 0:
        raise RuntimeError("cmake build failed:\n" + tail(built.stdout + built.stderr))
    if not os.path.exists(os.path.join(folder, "bin", name + EXE_SUFFIX)):
        raise RuntimeError(f"the build left no bin/{name}{EXE_SUFFIX}")


def play(folder, name, empty, env):
    """Runs the game with the driver script; returns the problems found (empty when it passed)."""
    script = os.path.join(SCRIPTS, "empty.lua" if empty else "example.lua")
    for mesa in ("opengl32.dll", "libgallium_wgl.dll"):  # Windows without a GPU: the games need CI's Mesa as well
        if os.path.exists(os.path.join(BIN, mesa)):
            shutil.copy(os.path.join(BIN, mesa), os.path.join(folder, "bin", mesa))
    program = os.path.join(folder, "bin", name + EXE_SUFFIX)
    try:
        done = run([program, "--deterministic", "--script", script], cwd=os.path.join(folder, "bin"), env=env,
                   timeout=RUN_TIMEOUT_SECONDS)
    except subprocess.TimeoutExpired as hang:
        log = (hang.stdout or b"") + (hang.stderr or b"")
        log = log.decode("utf-8", "replace") if isinstance(log, bytes) else log
        return [f"hung: no exit after {RUN_TIMEOUT_SECONDS} s", tail(log, 10)]
    log = done.stdout + done.stderr  # the engine logs to stderr
    problems = []
    if done.returncode != 0:
        problems.append(f"exit code {done.returncode}")
    problems += [line.strip() for line in log.splitlines() if " ERR " in line or " FTL " in line]
    if OK_LINE not in log:
        problems.append(f'the script never reached "{OK_LINE}"; the end of the log:\n' + tail(log, 15))
    return problems


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("projects", nargs="*", metavar="PROJECT", help="only these (default: all six)")
    parser.add_argument("--cpp", choices=("all", "examples", "none"), default="all",
                        help="which C++ and mixed projects to build and run (default: all four)")
    parser.add_argument("--work-dir", help="where the projects are made (default: a temporary folder)")
    parser.add_argument("--cmake-arg", action="append", default=[], help="an extra argument for the cmake of C++ projects")
    parser.add_argument("-j", "--jobs", type=int, default=os.cpu_count() or 2, help="parallel compile jobs")
    options = parser.parse_args()

    known = [name for name, _, _ in PROJECTS]
    for name in options.projects:
        if name not in known:
            sys.exit(f"unknown project {name} (one of {', '.join(known)})")
    chosen = [p for p in PROJECTS if not options.projects or p[0] in options.projects]
    if not options.projects:
        if options.cpp == "none":
            chosen = [p for p in chosen if p[1] == "lua"]
        elif options.cpp == "examples":
            chosen = [p for p in chosen if p[1] == "lua" or not p[2]]
    if not os.path.exists(os.path.join(BIN, "launcher", "launcher" + EXE_SUFFIX)):
        sys.exit("bin/launcher/launcher is not built (cmake --build build)")

    env = dict(os.environ, SMOKE_DIR=SCRIPTS)
    # The launcher adds what it creates to its project list: keep these out of the real one where it is configurable
    scratch = tempfile.mkdtemp(prefix="smoke-prefs-")
    env["XDG_DATA_HOME"] = scratch
    work_dir = options.work_dir or tempfile.mkdtemp(prefix="smoke-templates-")
    os.makedirs(work_dir, exist_ok=True)

    # Where XDG_DATA_HOME does not apply (macOS, Windows), the launcher writes to the real list: put it back afterwards
    listed = launcher_list()
    saved = open(listed, "rb").read() if listed and os.path.exists(listed) else None
    try:
        return run_all(chosen, options, work_dir, env)
    finally:
        if listed and saved is not None:
            with open(listed, "wb") as file:
                file.write(saved)
        elif listed and os.path.exists(listed):
            os.remove(listed)


def launcher_list():
    """The launcher's project list (SDL_GetPrefPath("modular-engine", "launcher")) on macOS and Windows."""
    if sys.platform == "darwin":
        return os.path.expanduser("~/Library/Application Support/modular-engine/launcher/projects.json")
    if os.name == "nt" and os.environ.get("APPDATA"):
        return os.path.join(os.environ["APPDATA"], "modular-engine", "launcher", "projects.json")
    return None


def run_all(chosen, options, work_dir, env):
    failures = []
    total = time.time()
    for name, kind, empty in chosen:
        print(f"{name:12}", end=" ", flush=True)
        start = time.time()
        try:
            folder = create(name, kind, empty, work_dir, env)
            built = time.time()
            if kind != "lua":
                build(folder, name, options.cmake_arg, options.jobs)
            built_seconds = time.time() - built
            problems = play(folder, name, empty, env)
        except Exception as error:  # a failed creation or build is this project's failure, the others still run
            problems = [str(error)]
            built_seconds = 0.0
        note = f"(build {built_seconds:.0f} s, " if kind != "lua" else "("
        print(("ok   " if not problems else "FAIL ") + note + f"total {time.time() - start:.1f} s)")
        if problems:
            failures.append(name)
            for problem in problems:
                print("    " + problem.replace("\n", "\n    "))
    print(f"{len(chosen) - len(failures)}/{len(chosen)} passed in {time.time() - total:.1f} s (projects in {work_dir})")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
