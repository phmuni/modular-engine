#!/usr/bin/env python3
"""Runs the behavior tests of the game API: every Lua script in tests/lua and every C++ test case in tests/cpp.

Each test is one start of bin/engine_tests (it opens a window and an OpenGL 4.1 context, and plays the test for a
fixed number of 1/60 s frames), so it needs the build with -DMODULAR_ENGINE_BUILD_TESTS=ON and a display. On a machine
with no GPU or screen (CI):

    xvfb-run -a -s "-screen 0 1280x720x24" python3 tools/run_tests.py

    python3 tools/run_tests.py                 run them all; exit code 1 if any fails
    python3 tools/run_tests.py tags camera     only the tests whose name contains one of these words
    python3 tools/run_tests.py -j 4            four at a time
    python3 tools/run_tests.py -v              show each test's own output, not only the failures'
    python3 tools/run_tests.py --list          the test names

Standard library only.
"""

import argparse
import concurrent.futures
import os
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXE = "engine_tests.exe" if os.name == "nt" else "engine_tests"
LUA_DIR = os.path.join(ROOT, "tests", "lua")
HELPER = "test.lua"  # the helper every Lua test loads, not a test
TIMEOUT_SECONDS = 120


def find_tests(binary):
    """(name, argument for engine_tests) for each Lua script, then each C++ case."""
    tests = []
    for name in sorted(os.listdir(LUA_DIR)):
        if name.endswith(".lua") and name != HELPER:
            tests.append(("lua/" + name[:-4], os.path.join(LUA_DIR, name)))
    # Decoded as UTF-8 whatever the console's code page is (Windows': cp1252, where some bytes do not decode)
    listed = subprocess.run([binary, "--list"], capture_output=True, text=True, encoding="utf-8", errors="replace",
                            timeout=30)
    if listed.returncode != 0:
        sys.exit("engine_tests --list failed:\n" + listed.stdout + listed.stderr)
    for name in listed.stdout.split():
        tests.append(("cpp/" + name, name))
    return tests


def run_test(binary, test):
    name, argument = test
    start = time.time()
    try:
        done = subprocess.run([binary, argument], capture_output=True, text=True, encoding="utf-8", errors="replace",
                              timeout=TIMEOUT_SECONDS)
        output = done.stdout + done.stderr
        passed = done.returncode == 0
        if not passed and done.returncode not in (1,):
            output += "\n(exit code %d: the engine did not get as far as running the test)\n" % done.returncode
    except subprocess.TimeoutExpired as timeout:
        output = (timeout.stdout or b"").decode(errors="replace") if isinstance(timeout.stdout, bytes) else (timeout.stdout or "")
        output += "\n(no result after %d s: hung)\n" % TIMEOUT_SECONDS
        passed = False
    return name, passed, time.time() - start, output


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("filters", nargs="*", help="run only tests whose name contains one of these")
    parser.add_argument("-j", "--jobs", type=int, default=1, help="tests to run at the same time (default 1)")
    parser.add_argument("-v", "--verbose", action="store_true", help="print the output of passing tests too")
    parser.add_argument("--list", action="store_true", help="list the tests and exit")
    parser.add_argument("--binary", default=os.path.join(ROOT, "bin", EXE), help="engine_tests to run")
    args = parser.parse_args()

    # The engine's output may have characters the console's code page (Windows') cannot print: never fail on them
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, "reconfigure"):
            stream.reconfigure(errors="replace")

    if not os.path.exists(args.binary):
        sys.exit("%s is not built: configure with -DMODULAR_ENGINE_BUILD_TESTS=ON and build target engine_tests" %
                 args.binary)
    tests = find_tests(args.binary)
    if args.filters:
        tests = [t for t in tests if any(word in t[0] for word in args.filters)]
    if args.list:
        print("\n".join(name for name, _ in tests))
        return 0
    if not tests:
        sys.exit("no test matches")

    started = time.time()
    failed = []
    # Input tests push mouse and key events at their window: with other test windows open at the same time the
    # window system can send them elsewhere, so they run alone, after the others
    alone = [t for t in tests if "input" in t[0]]
    together = [t for t in tests if "input" not in t[0]]
    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, args.jobs)) as pool:
        results = list(pool.map(lambda t: run_test(args.binary, t), together))
    results += [run_test(args.binary, t) for t in alone]
    for name, passed, seconds, output in results:
        print("%s %-36s %5.1f s" % ("PASS" if passed else "FAIL", name, seconds), flush=True)
        if not passed or args.verbose:
            # The engine's log lines are long; the failures are the lines that are not log lines
            lines = output.splitlines()
            shown = lines if (args.verbose or len(lines) <= 60) else lines[-60:]
            print("\n".join("    " + line for line in shown), flush=True)
        if not passed:
            failed.append(name)
    print("\n%d tests, %d failed, %.0f s" % (len(tests), len(failed), time.time() - started))
    if failed:
        print("failed: " + ", ".join(failed))
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
