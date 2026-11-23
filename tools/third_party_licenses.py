#!/usr/bin/env python3
"""THIRD_PARTY_LICENSES.txt: the licenses of everything the engine is built with, for it and the games made with it.

    python3 tools/third_party_licenses.py                  # writes it from the sources in build/_deps and external/
    python3 tools/third_party_licenses.py --deps <folder>  # the CPM sources somewhere else
    python3 tools/third_party_licenses.py --check          # needs no sources: run in CI

The file goes next to the README in the engine's package and into bin/engine-assets of every game (CMakeLists.txt), so
a shipped game carries the notices its libraries ask for. --check fails when a CPM package or a folder of external/ is
not in the list below, or when the file was written for another version of a package. After adding or updating a
dependency: read its license (and those of what it bundles), list it here, and run this without --check.
"""

import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUTPUT = ROOT / "THIRD_PARTY_LICENSES.txt"

# (title, license, the CPM package or external/ folder it covers, what to quote, a note)
# What to quote: files of the package (or of the repository for external/ and assets/), or (file, first, last): its
# lines from the first one matching a pattern to the next one matching the other.
PACKAGES = [
    ("SDL", "zlib License", "SDL",
     ["LICENSE.txt", "src/hidapi/LICENSE-bsd.txt", "src/video/yuv2rgb/LICENSE"],
     "Includes HIDAPI (used under its BSD license) and yuv2rgb (BSD)."),
    ("GLM", "MIT License", "glm", ["copying.txt"], "Used under the MIT License, one of the two it offers."),
    ("JSON for Modern C++", "MIT License", "nlohmann_json", ["LICENSE.MIT", "LICENSES/Apache-2.0.txt"],
     "Includes code by Bjoern Hoehrmann and Florian Loitsch (MIT), Evan Nemerson (Hedley, CC0) and the Abseil authors "
     "(Apache License 2.0)."),
    ("FreeType", "The FreeType License (FTL)", "freetype",
     ["docs/FTL.TXT", ("src/bdf/README", r"^Copyright \(C\) 2001", r"^THE USE OR OTHER DEALINGS"),
      ("src/pcf/README", r"^Copyright", r"^SOFTWARE OR THE USE"),
      ("src/gzip/zlib.h", r"Copyright \(C\) 1995", r"jloup@gzip"),
      ("src/autofit/hb-script-list.h", r"Copyright", r"MODIFICATIONS|ENHANCEMENTS")],
     "Portions of this software are copyright (c) The FreeType Project (https://freetype.org). All rights reserved. "
     "Used under the FTL, one of the two licenses it offers. Includes the BDF and PCF drivers, zlib and "
     "parts of HarfBuzz."),
    ("RmlUi", "MIT License", "RmlUi",
     ["LICENSE.txt", "Include/RmlUi/Core/Containers/LICENSE.txt", "Source/Debugger/LICENSE.txt"],
     "Includes robin_hood and itlib (MIT), and the fonts of its debugger."),
    ("ImGuizmo", "MIT License", "ImGuizmo", ["LICENSE"], ""),
    ("Jolt Physics", "MIT License", "JoltPhysics", ["LICENSE"], ""),
    ("Recast Navigation", "zlib License", "recastnavigation", ["License.txt"], ""),
    ("Lua", "MIT License", "lua", [("lua.h", r"^\* Copyright \(C\) 1994", r"^\* SOFTWARE OR THE USE")], ""),
    ("sol2", "MIT License", "sol2", ["LICENSE.txt"], ""),
    ("miniaudio", "MIT No Attribution", "miniaudio", ["LICENSE"], "Either of the two licenses it offers."),
    ("Dear ImGui", "MIT License", "external/imgui",
     ["external/imgui/LICENSE.txt", ("external/imgui/imgui_impl_opengl3_loader.h", r"This file was generated", r"OTHER DEALINGS")],
     "Includes modified stb_rect_pack, stb_textedit and stb_truetype (see stb below) and imgl3w (public domain)."),
    ("glad", "MIT License, Apache License 2.0", "external/glad", ["external/glad/LICENSE.txt"],
     "A generated OpenGL loader; the Apache License 2.0 is quoted in full under JSON for Modern C++."),
    ("khrplatform.h", "MIT License", "external/KHR", [("external/KHR/khrplatform.h", r"Copyright", r"MATERIALS OR THE USE")], ""),
    ("stb_image, stb_dxt", "MIT License", "external/stb_image",
     [("external/stb_image/stb_image.h", r"^ALTERNATIVE A - MIT", r"^SOFTWARE\.$")],
     "Used under the MIT License, one of the two they offer."),
    ("tinygltf", "MIT License", "external/tiny_gltf_loader",
     [("external/tiny_gltf_loader/tiny_gltf_v3.h", r"The MIT License", r"THE SOFTWARE\.$")], ""),
    ("tinyobjloader", "MIT License", "external/tiny_obj_loader",
     [("external/tiny_obj_loader/tiny_obj_loader.h", r"The MIT License", r"THE SOFTWARE\.$")], ""),
    ("AMD FidelityFX Super Resolution 1.0", "MIT License", None, [("assets/shaders/fsrEasu.frag", r"Copyright", r"^// SOFTWARE\.$")],
     "The shaders fsrEasu.frag and fsrRcas.frag are ports of it."),
    ("Lato fonts", "SIL Open Font License 1.1", None, ["assets/ui/fonts/LICENSE.txt"], ""),
]
ALSO_COVERED = {"external/stb_dxt"}  # under another entry of the list

CREDITS = """assets/shaders/fxaa.frag follows FXAA 3.11 by Timothy Lottes (NVIDIA).

The engine's models, sounds and textures were made for it and are under its license. Versions of the repository before
this file also held assets/models/box/diffuse.png, the container texture of https://learnopengl.com (Joey de Vries),
whose images are under CC BY 4.0 (https://creativecommons.org/licenses/by/4.0/).
"""


def cpm_versions():
    """name -> version of every CPM package in CMakeLists.txt, in both ways they are written there."""
    text = (ROOT / "CMakeLists.txt").read_text()
    versions = {}
    for body in re.findall(r"CPMAddPackage\(([^)]*)\)", text):
        short = re.match(r'\s*"gh:[^/]+/([^#"]+)#([^"]+)"', body)
        if short:
            versions[short.group(1)] = short.group(2)
            continue
        name = re.search(r"\bNAME\s+(\S+)", body).group(1)
        versions[name] = re.search(r"\b(?:GIT_TAG|VERSION)\s+(\S+)", body).group(1)
    return versions


def quote(base, spec):
    path, first, last = spec if isinstance(spec, tuple) else (spec, None, None)
    lines = (base / path).read_text(encoding="utf-8", errors="replace").splitlines()
    if first:
        start = next((i for i, line in enumerate(lines) if re.search(first, line)), None)
        end = None if start is None else next((i for i in range(start, len(lines)) if re.search(last, lines[i])), None)
        if end is None:
            raise ValueError(f"{base / path}: no lines from {first!r} to {last!r}")
        lines = [re.sub(r"^\s*(?://|\*\*|\*|/\*)\s?", "", line) for line in lines[start:end + 1]]  # comment marks
    text = "\n".join(line.rstrip() for line in lines).strip("\n")
    if len(text) < 200:
        raise ValueError(f"{base / path}: quoted almost nothing ({text!r})")
    return text


def write(deps):
    versions = cpm_versions()
    rule = "=" * 100
    out = ["Third-party licenses", "",
           "This software is made with modular-engine and contains the works below, under these licenses. Keep this file",
           "with every copy of the software. Written by tools/third_party_licenses.py.", "", rule,
           "modular-engine - MIT License", rule, "", quote(ROOT, "LICENSE"), ""]
    for title, license_name, covers, specs, note in PACKAGES:
        version = versions.get(covers)
        base = deps / f"{covers.lower()}-src" if version else ROOT
        out += [rule, f"{title}{' ' + version if version else ''} - {license_name}", rule, ""]
        if note:
            out += [note, ""]
        for spec in specs:
            out += [quote(base, spec), "", "-" * 100, ""]
        out = out[:-2]
    out += [rule, "Credits", rule, "", CREDITS]
    OUTPUT.write_text("\n".join(out), encoding="utf-8")
    print(f"Wrote {OUTPUT} ({len(PACKAGES)} works)")


def check():
    versions = cpm_versions()
    listed = {covers for _, _, covers, _, _ in PACKAGES if covers} | ALSO_COVERED
    folders = {f"external/{p.name}" for p in (ROOT / "external").iterdir() if p.is_dir()}
    problems = [f"{name} is not listed in tools/third_party_licenses.py" for name in sorted((set(versions) | folders) - listed)]
    problems += [f"{name} is listed but no longer there" for name in sorted(listed - set(versions) - folders)]
    text = OUTPUT.read_text(encoding="utf-8") if OUTPUT.exists() else ""
    for title, license_name, covers, _, _ in PACKAGES:
        version = versions.get(covers)
        heading = f"{title}{' ' + version if version else ''} - {license_name}"
        if f"\n{heading}\n" not in text:
            problems.append(f'{OUTPUT.name} has no "{heading}": run tools/third_party_licenses.py')
    for problem in problems:
        print(problem)
    if problems:
        return 1
    print(f"{OUTPUT.name} covers all {len(versions)} CPM packages and {len(folders)} folders of external/")
    return 0


def main():
    parser = argparse.ArgumentParser(description="Write or check THIRD_PARTY_LICENSES.txt.")
    parser.add_argument("--check", action="store_true", help="only check that the file covers every dependency")
    parser.add_argument("--deps", type=Path, default=ROOT / "build" / "_deps", help="the folder of the CPM sources")
    args = parser.parse_args()
    if args.check:
        return check()
    write(args.deps)
    return 0


if __name__ == "__main__":
    sys.exit(main())
