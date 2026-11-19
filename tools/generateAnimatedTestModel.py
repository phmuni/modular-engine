#!/usr/bin/env python3
"""Writes assets/models/tentacle/tentacle.glb: a small animated glTF for testing skeletal animation.

A tube skinned to a chain of four joints (each vertex shared by the two nearest, so it bends smoothly), a cube at its
tip that is not skinned but hangs from the last joint (a rigid part), and two clips: "Sway" bends the chain from
side to side, "Twist" turns it around while it leans. Everything is computed here, so the file has no license
attached beyond the engine's.

    python3 tools/generateAnimatedTestModel.py
"""

import json
import math
import struct
from pathlib import Path

OUTPUT = Path(__file__).resolve().parent.parent / "assets" / "models" / "tentacle" / "tentacle.glb"

SEGMENT = 0.75   # length of each of the four joints
JOINTS = 4
RINGS = 16       # rings of vertices along the tube
SIDES = 10
RADIUS = 0.22    # at the base, tapering toward the tip


class Writer:
    """Collects binary data into one buffer, with an accessor per array."""

    def __init__(self):
        self.binary = bytearray()
        self.views = []
        self.accessors = []

    def add(self, data, component, count, kind, target=None, minimum=None, maximum=None):
        while len(self.binary) % 4:
            self.binary.append(0)
        view = {"buffer": 0, "byteOffset": len(self.binary), "byteLength": len(data)}
        if target:
            view["target"] = target
        self.binary += data
        self.views.append(view)
        accessor = {"bufferView": len(self.views) - 1, "componentType": component, "count": count, "type": kind}
        if minimum is not None:
            accessor["min"], accessor["max"] = minimum, maximum
        self.accessors.append(accessor)
        return len(self.accessors) - 1


def floats(values):
    return struct.pack(f"<{len(values)}f", *values)


def quaternion(axis, degrees):
    half = math.radians(degrees) / 2
    s = math.sin(half)
    return [axis[0] * s, axis[1] * s, axis[2] * s, math.cos(half)]


def multiply(a, b):  # quaternions as [x, y, z, w]
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return [aw * bx + ax * bw + ay * bz - az * by, aw * by - ax * bz + ay * bw + az * bx,
            aw * bz + ax * by - ay * bx + az * bw, aw * bw - ax * bx - ay * by - az * bz]


def tube():
    """Positions, normals, joints, weights and indices of the skinned tube, from y = 0 up."""
    positions, normals, joints, weights, indices = [], [], [], [], []
    height = SEGMENT * JOINTS
    for ring in range(RINGS + 1):
        y = height * ring / RINGS
        radius = RADIUS * (1.0 - 0.6 * ring / RINGS)
        # Between the joint below and the one above: weights slide from one to the other over the segment
        along = min(y / SEGMENT, JOINTS - 1.0)
        lower = min(int(along), JOINTS - 1)
        upper = min(lower + 1, JOINTS - 1)
        blend = along - lower if upper != lower else 0.0
        for side in range(SIDES):
            angle = 2 * math.pi * side / SIDES
            x, z = math.cos(angle), math.sin(angle)
            positions += [x * radius, y, z * radius]
            normals += [x, 0.0, z]
            joints += [lower, upper, 0, 0]
            weights += [1.0 - blend, blend, 0.0, 0.0]
    for ring in range(RINGS):
        for side in range(SIDES):
            a = ring * SIDES + side
            b = ring * SIDES + (side + 1) % SIDES
            c, d = a + SIDES, b + SIDES
            indices += [a, c, b, b, c, d]
    # A cap on top
    top = len(positions) // 3
    positions += [0.0, height, 0.0]
    normals += [0.0, 1.0, 0.0]
    joints += [JOINTS - 1, 0, 0, 0]
    weights += [1.0, 0.0, 0.0, 0.0]
    for side in range(SIDES):
        a = RINGS * SIDES + side
        b = RINGS * SIDES + (side + 1) % SIDES
        indices += [a, top, b]
    return positions, normals, joints, weights, indices


def cube(size):
    """A cube centered on the origin, with a normal per face."""
    positions, normals, indices = [], [], []
    h = size / 2
    faces = [((1, 0, 0), (0, 1, 0), (0, 0, 1)), ((-1, 0, 0), (0, 1, 0), (0, 0, -1)),
             ((0, 1, 0), (0, 0, 1), (1, 0, 0)), ((0, -1, 0), (0, 0, -1), (1, 0, 0)),
             ((0, 0, 1), (1, 0, 0), (0, 1, 0)), ((0, 0, -1), (-1, 0, 0), (0, 1, 0))]
    for normal, u, v in faces:
        base = len(positions) // 3
        for su, sv in [(-1, -1), (1, -1), (1, 1), (-1, 1)]:
            positions += [h * (normal[i] + su * u[i] + sv * v[i]) for i in range(3)]
            normals += list(normal)
        indices += [base, base + 1, base + 2, base, base + 2, base + 3]
    return positions, normals, indices


def bounds(positions):
    xs, ys, zs = positions[0::3], positions[1::3], positions[2::3]
    return [min(xs), min(ys), min(zs)], [max(xs), max(ys), max(zs)]


def main():
    w = Writer()
    FLOAT, USHORT, UINT = 5126, 5123, 5125
    ARRAY, ELEMENTS = 34962, 34963

    positions, normals, joints, weights, indices = tube()
    low, high = bounds(positions)
    tube_attributes = {
        "POSITION": w.add(floats(positions), FLOAT, len(positions) // 3, "VEC3", ARRAY, low, high),
        "NORMAL": w.add(floats(normals), FLOAT, len(normals) // 3, "VEC3", ARRAY),
        "JOINTS_0": w.add(struct.pack(f"<{len(joints)}H", *joints), USHORT, len(joints) // 4, "VEC4", ARRAY),
        "WEIGHTS_0": w.add(floats(weights), FLOAT, len(weights) // 4, "VEC4", ARRAY),
    }
    tube_indices = w.add(struct.pack(f"<{len(indices)}I", *indices), UINT, len(indices), "SCALAR", ELEMENTS)

    cube_positions, cube_normals, cube_indices = cube(0.35)
    low, high = bounds(cube_positions)
    cube_attributes = {
        "POSITION": w.add(floats(cube_positions), FLOAT, len(cube_positions) // 3, "VEC3", ARRAY, low, high),
        "NORMAL": w.add(floats(cube_normals), FLOAT, len(cube_normals) // 3, "VEC3", ARRAY),
    }
    cube_index_accessor = w.add(struct.pack(f"<{len(cube_indices)}I", *cube_indices), UINT, len(cube_indices),
                                "SCALAR", ELEMENTS)

    # Joint k sits SEGMENT * k up at rest: its inverse bind matrix moves back down (column-major)
    inverse_binds = []
    for k in range(JOINTS):
        inverse_binds += [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, -SEGMENT * k, 0, 1]
    inverse_bind_accessor = w.add(floats(inverse_binds), FLOAT, JOINTS, "MAT4")

    # Nodes: 0 the tube (skinned), 1-4 the joint chain, 5 the tip cube hanging from the last joint
    nodes = [{"name": "Tube", "mesh": 0, "skin": 0}]
    for k in range(JOINTS):
        node = {"name": f"Joint{k}", "translation": [0.0, 0.0 if k == 0 else SEGMENT, 0.0]}
        node["children"] = [2 + k] if k + 1 < JOINTS else [5]
        nodes.append(node)
    nodes.append({"name": "Tip", "mesh": 1, "translation": [0.0, SEGMENT + 0.2, 0.0]})

    # Clips: two seconds each, sampled 25 times, linear
    keys = 25
    times = [2.0 * i / (keys - 1) for i in range(keys)]
    time_accessor = w.add(floats(times), FLOAT, keys, "SCALAR", None, [0.0], [2.0])
    animations = []
    sway_channels, sway_samplers = [], []
    for k in range(1, JOINTS):
        rotations = []
        for t in times:
            rotations += quaternion((0, 0, 1), 22.0 * math.sin(2 * math.pi * t / 2.0 - 0.6 * k))
        output = w.add(floats(rotations), FLOAT, keys, "VEC4")
        sway_samplers.append({"input": time_accessor, "output": output, "interpolation": "LINEAR"})
        sway_channels.append({"sampler": len(sway_samplers) - 1, "target": {"node": 1 + k, "path": "rotation"}})
    animations.append({"name": "Sway", "channels": sway_channels, "samplers": sway_samplers})

    twist_rotations, lean_rotations = [], []
    for t in times:
        twist_rotations += quaternion((0, 1, 0), 360.0 * t / 2.0)
        lean_rotations += quaternion((1, 0, 0), 30.0)
    twist = w.add(floats(twist_rotations), FLOAT, keys, "VEC4")
    lean = w.add(floats(lean_rotations), FLOAT, keys, "VEC4")
    animations.append({"name": "Twist", "samplers": [
        {"input": time_accessor, "output": twist, "interpolation": "LINEAR"},
        {"input": time_accessor, "output": lean, "interpolation": "LINEAR"}], "channels": [
        {"sampler": 0, "target": {"node": 1, "path": "rotation"}},
        {"sampler": 1, "target": {"node": 2, "path": "rotation"}}]})

    gltf = {
        "asset": {"version": "2.0", "generator": "modular-engine tools/generateAnimatedTestModel.py"},
        "scene": 0,
        "scenes": [{"nodes": [0, 1]}],
        "nodes": nodes,
        "meshes": [
            {"name": "Tube", "primitives": [{"attributes": tube_attributes, "indices": tube_indices, "material": 0}]},
            {"name": "Tip", "primitives": [{"attributes": cube_attributes, "indices": cube_index_accessor,
                                            "material": 1}]},
        ],
        "materials": [
            {"name": "Skin", "pbrMetallicRoughness": {"baseColorFactor": [0.55, 0.3, 0.75, 1.0]}},
            {"name": "Tip", "pbrMetallicRoughness": {"baseColorFactor": [1.0, 0.8, 0.25, 1.0]},
             "emissiveFactor": [1.0, 0.7, 0.2]},
        ],
        "skins": [{"name": "Chain", "joints": [1, 2, 3, 4], "inverseBindMatrices": inverse_bind_accessor,
                   "skeleton": 1}],
        "animations": animations,
        "accessors": w.accessors,
        "bufferViews": w.views,
        "buffers": [{"byteLength": len(w.binary)}],
    }

    text = json.dumps(gltf, separators=(",", ":")).encode()
    text += b" " * ((4 - len(text) % 4) % 4)
    binary = bytes(w.binary) + b"\0" * ((4 - len(w.binary) % 4) % 4)
    total = 12 + 8 + len(text) + 8 + len(binary)
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    with open(OUTPUT, "wb") as f:
        f.write(struct.pack("<III", 0x46546C67, 2, total))
        f.write(struct.pack("<II", len(text), 0x4E4F534A) + text)
        f.write(struct.pack("<II", len(binary), 0x004E4942) + binary)
    print(f"wrote {OUTPUT} ({total} bytes)")


if __name__ == "__main__":
    main()
