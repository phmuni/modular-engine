#!/usr/bin/env python3
"""Writes the tiny HDR panoramas the image-based lighting tests use (tests/fixtures/sky/*.hdr).

An equirectangular Radiance .hdr, 64x32, uncompressed: a bright sky above the horizon, a dark ground below, and a very
bright "sun" (well over 1.0, which only a float image keeps). `sky_blue.hdr` has a blue sky, `sky_red.hdr` a red one.
Run it from anywhere: python3 tools/make_test_sky.py
"""
import math
import os
import struct

WIDTH, HEIGHT = 64, 32


def rgbe(r, g, b):
    biggest = max(r, g, b)
    if biggest < 1e-32:
        return struct.pack("4B", 0, 0, 0, 0)
    mantissa, exponent = math.frexp(biggest)
    scale = mantissa * 256.0 / biggest
    return struct.pack("4B", int(r * scale), int(g * scale), int(b * scale), exponent + 128)


def pixel(x, y, sky):
    # Row 0 is the top of the image (+Y up); the center column looks down -Z
    elevation = (0.5 - (y + 0.5) / HEIGHT) * math.pi
    longitude = ((x + 0.5) / WIDTH - 0.5) * 2.0 * math.pi
    if elevation < 0.0:
        return (0.25, 0.18, 0.12)
    r, g, b = (c * (0.8 + 0.8 * math.sin(elevation)) for c in sky)
    # A sun above the horizon, a little to the right of the center
    if abs(longitude - 0.8) < 0.25 and abs(elevation - 0.7) < 0.2:
        return (60.0, 55.0, 45.0)
    return (r, g, b)


def write(path, sky):
    with open(path, "wb") as out:
        out.write(b"#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y %d +X %d\n" % (HEIGHT, WIDTH))
        for y in range(HEIGHT):
            for x in range(WIDTH):
                out.write(rgbe(*pixel(x, y, sky)))


def main():
    folder = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tests", "fixtures", "sky")
    os.makedirs(folder, exist_ok=True)
    write(os.path.join(folder, "sky_blue.hdr"), (0.3, 0.5, 1.0))
    write(os.path.join(folder, "sky_red.hdr"), (1.0, 0.2, 0.1))


if __name__ == "__main__":
    main()
