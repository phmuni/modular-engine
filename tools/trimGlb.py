#!/usr/bin/env python3
"""Writes a smaller copy of a binary glTF: only some of its animations, without some of its nodes, and only the data
those need.

  python3 tools/trimGlb.py in.glb out.glb --clips Idle,Walking_A --drop 2H_Sword,Badge_Shield

The knight (assets/models/knight) is Knight.glb from KayKit: Adventurers 1.0 (CC0), trimmed with:

  python3 tools/trimGlb.py Knight.glb assets/models/knight/knight.glb \\
    --clips Idle,Walking_A,Walking_Backwards,Running_A,Running_Strafe_Left,Running_Strafe_Right,Jump_Start,Jump_Idle,\\
Jump_Land,Dodge_Forward,1H_Melee_Attack_Chop,1H_Melee_Attack_Slice_Diagonal,Block,Blocking,Block_Hit,Hit_A,Death_A \\
    --drop 1H_Sword_Offhand,Badge_Shield,Rectangle_Shield,Spike_Shield,2H_Sword
"""

import argparse
import json
import struct


def read_glb(path):
    with open(path, "rb") as file:
        data = file.read()
    magic, version, _ = struct.unpack_from("<III", data, 0)
    if magic != 0x46546C67 or version != 2:
        raise SystemExit(f"{path}: not a binary glTF 2 file")
    gltf, binary, offset = None, b"", 12
    while offset < len(data):
        length, kind = struct.unpack_from("<II", data, offset)
        chunk = data[offset + 8 : offset + 8 + length]
        if kind == 0x4E4F534A:
            gltf = json.loads(chunk)
        elif kind == 0x004E4942:
            binary = chunk
        offset += 8 + length
    return gltf, binary


def write_glb(path, gltf, binary):
    text = json.dumps(gltf, separators=(",", ":")).encode()
    text += b" " * (-len(text) % 4)
    binary += b"\0" * (-len(binary) % 4)
    with open(path, "wb") as file:
        file.write(struct.pack("<III", 0x46546C67, 2, 12 + 8 + len(text) + 8 + len(binary)))
        file.write(struct.pack("<II", len(text), 0x4E4F534A) + text)
        file.write(struct.pack("<II", len(binary), 0x004E4942) + binary)


class Keeper:
    """Copies the items of a list that are asked for, once each, and says where they ended up."""

    def __init__(self, items):
        self.items, self.kept, self.places = items, [], {}

    def keep(self, index):
        if index not in self.places:
            self.places[index] = len(self.kept)
            self.kept.append(self.items[index])
        return self.places[index]


def trim(gltf, binary, clips, drop):
    nodes = gltf["nodes"]
    names = {node.get("name") for node in nodes}
    animations = {animation.get("name"): animation for animation in gltf.get("animations", [])}
    for name in drop:
        if name not in names:
            raise SystemExit(f"no node is called {name}")
    for name in clips:
        if name not in animations:
            raise SystemExit(f"no animation is called {name}")

    # Nodes: what the scenes reach without the dropped ones
    places, order = {}, []

    def visit(index):
        if nodes[index].get("name") in drop:
            return
        places[index] = len(order)
        order.append(index)
        for child in nodes[index].get("children", []):
            visit(child)

    for scene in gltf["scenes"]:
        for root in scene["nodes"]:
            visit(root)
        scene["nodes"] = [places[n] for n in scene["nodes"] if n in places]

    accessors, meshes = Keeper(gltf["accessors"]), Keeper(gltf["meshes"])
    kept_nodes = []
    for index in order:
        node = nodes[index]
        if "children" in node:
            node["children"] = [places[c] for c in node["children"] if c in places]
            if not node["children"]:
                del node["children"]
        if "mesh" in node:
            node["mesh"] = meshes.keep(node["mesh"])
        kept_nodes.append(node)
    for mesh in meshes.kept:
        for primitive in mesh["primitives"]:
            if "indices" in primitive:
                primitive["indices"] = accessors.keep(primitive["indices"])
            for attributes in [primitive["attributes"], *primitive.get("targets", [])]:
                for name in attributes:
                    attributes[name] = accessors.keep(attributes[name])
    for skin in gltf.get("skins", []):
        if any(joint not in places for joint in skin["joints"]):
            raise SystemExit("a dropped node is a joint")
        skin["joints"] = [places[joint] for joint in skin["joints"]]
        if "skeleton" in skin:
            skin["skeleton"] = places[skin["skeleton"]]
        if "inverseBindMatrices" in skin:
            skin["inverseBindMatrices"] = accessors.keep(skin["inverseBindMatrices"])

    kept_animations = []
    for name in clips:
        animation = animations[name]
        samplers, channels = Keeper(animation["samplers"]), []
        for channel in animation["channels"]:
            if channel["target"].get("node") in places:
                channel["target"]["node"] = places[channel["target"]["node"]]
                channel["sampler"] = samplers.keep(channel["sampler"])
                channels.append(channel)
        for sampler in samplers.kept:
            sampler["input"] = accessors.keep(sampler["input"])
            sampler["output"] = accessors.keep(sampler["output"])
        kept_animations.append({**animation, "samplers": samplers.kept, "channels": channels})

    views = Keeper(gltf["bufferViews"])
    for accessor in accessors.kept:
        if "sparse" in accessor:
            raise SystemExit("sparse accessors are not handled")
        if "bufferView" in accessor:
            accessor["bufferView"] = views.keep(accessor["bufferView"])
    for image in gltf.get("images", []):
        if "bufferView" in image:
            image["bufferView"] = views.keep(image["bufferView"])

    data = bytearray()
    for view in views.kept:
        start = view.get("byteOffset", 0)
        data += b"\0" * (-len(data) % 4)
        view["byteOffset"] = len(data)
        data += binary[start : start + view["byteLength"]]

    gltf.update(nodes=kept_nodes, meshes=meshes.kept, animations=kept_animations, accessors=accessors.kept,
                bufferViews=views.kept, buffers=[{"byteLength": len(data)}])
    return gltf, bytes(data)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("input")
    parser.add_argument("output")
    parser.add_argument("--clips", required=True, help="animations to keep, in this order, separated by commas")
    parser.add_argument("--drop", default="", help="nodes to leave out, with their children, separated by commas")
    args = parser.parse_args()
    gltf, binary = read_glb(args.input)
    clips = [name for name in args.clips.split(",") if name]
    drop = {name for name in args.drop.split(",") if name}
    write_glb(args.output, *trim(gltf, binary, clips, drop))


if __name__ == "__main__":
    main()
