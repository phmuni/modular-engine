#!/usr/bin/env python3
"""Writes assets/models/cylinder/cylinder.obj: a cylinder of diameter 1 and height 1 along Y, centered on the origin
(like the box, so scale gives its size). The side has smooth normals, the caps flat ones.

    python3 tools/generateCylinderModel.py
"""

import argparse
import math
from pathlib import Path

SIDES = 24
RADIUS = 0.5
HEIGHT = 1.0
OUT = Path(__file__).resolve().parent.parent / "assets" / "models" / "cylinder" / "cylinder.obj"


def main():
    v, vt, vn, faces = [], [], [], []

    def vertex(position, uv, normal):
        v.append(position)
        vt.append(uv)
        vn.append(normal)
        return len(v)

    top, bottom = HEIGHT / 2, -HEIGHT / 2
    angles = [2 * math.pi * i / SIDES for i in range(SIDES)]

    # Side: a seam column (SIDES + 1 columns) so the texture wraps once
    side = []
    for i in range(SIDES + 1):
        a = 2 * math.pi * i / SIDES
        c, s = math.cos(a), math.sin(a)
        low = vertex((RADIUS * c, bottom, RADIUS * s), (i / SIDES, 0.0), (c, 0.0, s))
        high = vertex((RADIUS * c, top, RADIUS * s), (i / SIDES, 1.0), (c, 0.0, s))
        side.append((low, high))
    for i in range(SIDES):
        (l0, h0), (l1, h1) = side[i], side[i + 1]
        # Counter-clockwise seen from outside (the angle grows from +X towards +Z)
        faces.append((l0, h0, h1))
        faces.append((l0, h1, l1))

    # Caps: a fan around a center vertex, the texture laid on as a disc
    for y, normal, flip in ((top, (0.0, 1.0, 0.0), False), (bottom, (0.0, -1.0, 0.0), True)):
        center = vertex((0.0, y, 0.0), (0.5, 0.5), normal)
        ring = [vertex((RADIUS * math.cos(a), y, RADIUS * math.sin(a)),
                       (0.5 + 0.5 * math.cos(a), 0.5 + 0.5 * math.sin(a)), normal) for a in angles]
        for i in range(SIDES):
            a, b = ring[i], ring[(i + 1) % SIDES]
            faces.append((center, a, b) if flip else (center, b, a))

    OUT.parent.mkdir(parents=True, exist_ok=True)
    with open(OUT, "w") as f:
        f.write(f"# Cylinder of diameter 1 and height 1 along Y, {SIDES} sides (tools/generateCylinderModel.py)\n\n")
        for p in v:
            f.write("v %.5f %.5f %.5f\n" % p)
        f.write("\n")
        for t in vt:
            f.write("vt %.5f %.5f\n" % t)
        f.write("\n")
        for n in vn:
            f.write("vn %.5f %.5f %.5f\n" % n)
        f.write("\n")
        for a, b, c in faces:
            f.write(f"f {a}/{a}/{a} {b}/{b}/{b} {c}/{c}/{c}\n")
    print(f"{OUT}: {len(v)} vertices, {len(faces)} triangles")


if __name__ == "__main__":
    # No options: only --help, which says what the tool does
    argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter).parse_args()
    main()
