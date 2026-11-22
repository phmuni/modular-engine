#!/usr/bin/env python3
"""Visual and performance regression checks for the engine, run locally.

Each case runs bin/engine with --deterministic (fixed 1/60 s frames, seeded randomness), so the same build renders
the same pixels every time:

  * image: a screenshot after a fixed number of frames, compared with a reference PNG
  * perf:  a short --benchmark, compared with a stored baseline (average and p95 frame time)

References and baselines describe this machine's GPU and speed, so they live in tests/regression/ and are not
committed. Create them with --update on a build you trust, then run without it after each change:

  python3 tools/regression.py --update      # record references and baselines
  python3 tools/regression.py               # compare; exit code 1 on a regression
  python3 tools/regression.py --only aliens --images-only

Differing screenshots are written to tests/regression/out/ next to a diff image (changed pixels in red).
Standard library only.
"""

import argparse
import json
import os
import re
import struct
import subprocess
import sys
import tempfile
import time
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BIN = os.path.join(ROOT, "bin")
ENGINE = os.path.join(BIN, "engine.exe" if os.name == "nt" else "engine")
DATA = os.path.join(ROOT, "tests", "regression")
REFERENCES = os.path.join(DATA, "reference")
BASELINE = os.path.join(DATA, "baseline.json")
OUT = os.path.join(DATA, "out")

# name -> engine arguments (relative paths resolve from bin/, like the engine's own)
CASES = {
    "aliens": ["aliens"],
    "blackhole": ["blackhole"],
    "stress": ["stress"],
    "viewer-alien": ["view", "../assets/models/alien/alien.glb"],
    "scene-playground": ["scene", "../assets/scenes/playground.json"],
    "scene-point-shadows": ["scene", "../assets/scenes/point_shadows.json"],
    "scene-sun-shadows": ["scene", "../assets/scenes/sun_shadows.json"],
    "scene-black-hole": ["scene", "../assets/scenes/black_hole.json"],
    "scene-normal-maps": ["scene", "../assets/scenes/normal_maps.json"],
    "scene-physics": ["scene", "../assets/scenes/physics.json"],  # Jolt is deterministic, threads and all
    "scene-animation": ["scene", "../assets/scenes/animation.json"],
    "scene-navigation": ["scene", "../assets/scenes/navigation.json"],  # a character walking a computed path
    "scene-custom-shaders": ["scene", "../assets/scenes/custom_shaders.json"],  # water, hologram and dissolve shaders
    "scene-reflection-probes": ["scene", "../assets/scenes/reflection_probes.json"],  # two baked reflection probes: a room and a corridor
    "scene-many-lights": ["scene", "../assets/scenes/many_lights.json"],  # 180 colored point lights (clustered), 5 with shadows, metal and rough balls
}

SCREENSHOT_SECONDS = 2.0     # 120 fixed frames
BENCHMARK_SECONDS = 3.0
PIXEL_TOLERANCE = 8          # per channel, out of 255
IMAGE_TOLERANCE = 0.1        # percent of pixels allowed to differ
PERF_TOLERANCE = 0.20        # 20 % slower fails...
PERF_SLACK_MS = 0.15         # ...unless the difference is below this (tiny frame times are noisy)


# ---------------------------------------------------------------------------------------------- images

def read_bmp(path):
    """The engine's screenshots: uncompressed 24/32-bit BMP. Returns (width, height, RGB rows top first)."""
    with open(path, "rb") as f:
        data = f.read()
    offset, = struct.unpack_from("<I", data, 10)
    width, height = struct.unpack_from("<ii", data, 18)
    bpp, = struct.unpack_from("<H", data, 28)
    if bpp not in (24, 32):
        raise ValueError(f"{path}: {bpp}-bit BMP is not supported")
    step = bpp // 8
    stride = (width * step + 3) & ~3
    rows = []
    for y in range(abs(height)):
        start = offset + y * stride
        row = data[start:start + width * step]
        rgb = bytearray(width * 3)
        rgb[0::3] = row[2::step]
        rgb[1::3] = row[1::step]
        rgb[2::3] = row[0::step]
        rows.append(bytes(rgb))
    if height > 0:  # bottom-up
        rows.reverse()
    return width, abs(height), rows


def write_png(path, width, height, rows):
    def chunk(kind, payload):
        return struct.pack(">I", len(payload)) + kind + payload + struct.pack(">I", zlib.crc32(kind + payload))
    raw = b"".join(b"\x00" + row for row in rows)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(raw, 6)))
        f.write(chunk(b"IEND", b""))


def read_png(path):
    """PNGs written by write_png: 8-bit RGB, no interlace, every row unfiltered."""
    with open(path, "rb") as f:
        data = f.read()
    pos, idat, width, height = 8, b"", 0, 0
    while pos < len(data):
        length, = struct.unpack_from(">I", data, pos)
        kind = data[pos + 4:pos + 8]
        payload = data[pos + 8:pos + 8 + length]
        if kind == b"IHDR":
            width, height, depth, color = struct.unpack_from(">IIBB", payload)
            if depth != 8 or color != 2:
                raise ValueError(f"{path}: only 8-bit RGB references are supported")
        elif kind == b"IDAT":
            idat += payload
        pos += 12 + length
    raw = zlib.decompress(idat)
    stride = width * 3 + 1
    rows = []
    for y in range(height):
        if raw[y * stride] != 0:
            raise ValueError(f"{path}: filtered rows are not supported")
        rows.append(raw[y * stride + 1:(y + 1) * stride])
    return width, height, rows


def compare_images(reference, actual):
    """Percent of pixels that differ by more than PIXEL_TOLERANCE, and a diff image (changes in red)."""
    (rw, rh, rrows), (aw, ah, arows) = reference, actual
    if (rw, rh) != (aw, ah):
        return 100.0, None
    changed = 0
    diff_rows = []
    for ref_row, act_row in zip(rrows, arows):
        out = bytearray(len(ref_row))
        for i in range(0, len(ref_row), 3):
            if (abs(ref_row[i] - act_row[i]) > PIXEL_TOLERANCE or abs(ref_row[i + 1] - act_row[i + 1]) > PIXEL_TOLERANCE
                    or abs(ref_row[i + 2] - act_row[i + 2]) > PIXEL_TOLERANCE):
                changed += 1
                out[i], out[i + 1], out[i + 2] = 255, 0, 0
            else:  # the reference, dimmed
                out[i], out[i + 1], out[i + 2] = ref_row[i] // 3, ref_row[i + 1] // 3, ref_row[i + 2] // 3
        diff_rows.append(bytes(out))
    return 100.0 * changed / (rw * rh), (rw, rh, diff_rows)


# ---------------------------------------------------------------------------------------------- engine runs

def run_engine(args, timeout=120):
    result = subprocess.run([ENGINE, *args, "--deterministic"], cwd=BIN, capture_output=True, text=True,
                            timeout=timeout)
    log = result.stdout + result.stderr  # the engine logs to stderr
    errors = [line for line in log.splitlines() if " ERR " in line or " WRN " in line]
    return result.returncode, errors, log


def screenshot(args, path):
    code, errors, _ = run_engine([*args, "--screenshot", path, str(SCREENSHOT_SECONDS)])
    if code != 0 or not os.path.exists(path):
        raise RuntimeError(f"the engine exited with {code} without a screenshot")
    return errors


BENCHMARK_LINE = re.compile(r"avg ([0-9.]+) ms.*p95 ([0-9.]+)")


def benchmark(args):
    code, errors, log = run_engine([*args, "--benchmark", str(BENCHMARK_SECONDS)])
    match = BENCHMARK_LINE.search(log)
    if code != 0 or not match:
        raise RuntimeError(f"the engine exited with {code} without benchmark results")
    return {"avg": float(match.group(1)), "p95": float(match.group(2))}, errors


# ---------------------------------------------------------------------------------------------- main

def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--update", action="store_true", help="record references and baselines from this build")
    parser.add_argument("--only", action="append", choices=sorted(CASES), help="run only these cases")
    parser.add_argument("--images-only", action="store_true")
    parser.add_argument("--perf-only", action="store_true")
    parser.add_argument("--build", action="store_true", help="run 'cmake --build build' first")
    options = parser.parse_args()

    if options.build:
        subprocess.run(["cmake", "--build", "build", "-j"], cwd=ROOT, check=True)
    if not os.path.exists(ENGINE):
        sys.exit(f"{ENGINE} not found; build the engine first (or pass --build)")

    os.makedirs(REFERENCES, exist_ok=True)
    os.makedirs(OUT, exist_ok=True)
    baseline = json.load(open(BASELINE)) if os.path.exists(BASELINE) else {}
    cases = options.only or list(CASES)
    failures, missing = [], []
    started = time.time()

    with tempfile.TemporaryDirectory() as scratch:
        for name in cases:
            args = CASES[name]
            print(f"{name:22}", end=" ", flush=True)
            notes = []
            try:
                if not options.perf_only:
                    shot = os.path.join(scratch, name + ".bmp")
                    warnings = screenshot(args, shot)
                    actual = read_bmp(shot)
                    reference_path = os.path.join(REFERENCES, name + ".png")
                    if options.update:
                        write_png(reference_path, *actual)
                        notes.append("image recorded")
                    elif not os.path.exists(reference_path):
                        missing.append(name)
                        notes.append("image: no reference")
                    else:
                        percent, diff = compare_images(read_png(reference_path), actual)
                        if percent > IMAGE_TOLERANCE:
                            failures.append(f"{name}: {percent:.2f}% of pixels changed")
                            write_png(os.path.join(OUT, name + ".png"), *actual)
                            if diff:
                                write_png(os.path.join(OUT, name + ".diff.png"), *diff)
                            notes.append(f"IMAGE CHANGED {percent:.2f}%")
                        else:
                            notes.append(f"image ok ({percent:.3f}%)")
                    if warnings:
                        failures.append(f"{name}: {len(warnings)} warning(s)/error(s) in the log, first: {warnings[0]}")
                        notes.append(f"{len(warnings)} LOG WARNING(S)")

                if not options.images_only:
                    perf, _ = benchmark(args)
                    if options.update:
                        baseline[name] = perf
                        notes.append(f"perf recorded (avg {perf['avg']:.3f} ms, p95 {perf['p95']:.3f} ms)")
                    elif name not in baseline:
                        missing.append(name)
                        notes.append(f"perf: no baseline (avg {perf['avg']:.3f} ms)")
                    else:
                        for key in ("avg", "p95"):
                            before, now = baseline[name][key], perf[key]
                            slower = now - before
                            if slower > PERF_SLACK_MS and now > before * (1 + PERF_TOLERANCE):
                                failures.append(f"{name}: {key} {before:.3f} -> {now:.3f} ms (+{100 * slower / before:.0f}%)")
                                notes.append(f"{key.upper()} SLOWER {before:.3f} -> {now:.3f} ms")
                            else:
                                notes.append(f"{key} {now:.3f} ms ({before:.3f})")
            except Exception as error:  # a crash or a timeout is a failure, not a reason to stop
                failures.append(f"{name}: {error}")
                notes.append(f"FAILED: {error}")
            print(" | ".join(notes))

    if options.update:
        with open(BASELINE, "w") as f:
            json.dump(baseline, f, indent=2, sort_keys=True)

    print(f"\n{len(cases)} case(s) in {time.time() - started:.0f} s")
    if missing:
        print(f"No reference for: {', '.join(sorted(set(missing)))} (run with --update)")
    if failures:
        print(f"{len(failures)} regression(s):")
        for failure in failures:
            print(f"  - {failure}")
        print(f"Screenshots and diffs: {OUT}")
        sys.exit(1)
    print("No regressions" if not options.update else "References and baselines updated")


if __name__ == "__main__":
    main()
