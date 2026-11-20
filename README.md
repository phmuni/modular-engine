<div align="center">

# Modular Engine

**A modular 3D graphics engine built with modern C++**

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![C++](https://img.shields.io/badge/C%2B%2B-Modern-00599C?logo=c%2B%2B)](https://isocpp.org/)
[![CMake](https://img.shields.io/badge/CMake-3.14%2B-064F8C?logo=cmake)](https://cmake.org/)
[![OpenGL](https://img.shields.io/badge/OpenGL-Rendering-5586A4?logo=opengl)](https://www.opengl.org/)

Built on a robust **Entity-Component-System (ECS)** architecture with flexible rendering capabilities using OpenGL and SDL3.

[Features](#features) · [Architecture](#architecture) · [Installation](#installation) · [Usage](#usage) · [Contributing](#contributing)

<br/>

<img src="docs/images/black-hole.png" alt="Black hole example: a lensed particle accretion disk" width="100%"/>

<sub>The black hole example: 86k particles on Keplerian orbits, bent by a point-mass gravitational lens.</sub>

</div>

---

## Table of Contents

- [Features](#features)
- [Architecture](#architecture)
- [Prerequisites](#prerequisites)
- [Installation](#installation)
- [Project Structure](#project-structure)
- [Building](#building)
- [Usage](#usage)
- [Technologies](#technologies)
- [Contributing](#contributing)
- [License](#license)

---

<a id="features"></a>

## Features

| Category                   | Description                                                                    |
| -------------------------- | ------------------------------------------------------------------------------ |
| **ECS Architecture**       | Sparse-set component pools: O(1) lookups, packed iteration, stable component addresses |
| **App Base Class**         | Simple `App` interface with `setup()` and `update()` for clean game logic      |
| **Generic Components**     | Any plain struct works as a component — no base class required                 |
| **OpenGL Rendering**       | Modern graphics API integration with full shader support                       |
| **Instanced Rendering**    | Models sharing mesh and material are drawn in one call, in the main and shadow passes |
| **Frustum Culling**        | Models outside the camera are skipped using their mesh's bounding sphere       |
| **PBR Material System**    | Per-submesh materials with diffuse, specular, normal, and emission maps        |
| **Solid Color Fallback**   | Centralized solid color texture generation and caching for simple rendering    |
| **Resource Caching**       | Automatic texture caching with handle-based resource management                |
| **Shadow Mapping**         | Cascaded sun shadows that follow the camera (3 cascades out to 80 units) and cube-map shadows for up to 4 point/spot lights (`Light::castShadows`), re-rendered only when something within the light's range moves; hardware-filtered PCF |
| **Asset Loading**          | OBJ (Tiny_Obj) and glTF/GLB (tinygltf) with named parts, shared materials, background loading (`preloadModel`) and a binary mesh cache (`bin/cache/meshes`) |
| **Texture Streaming**      | Textures converted once to GPU-compressed BC1/BC3 (BC5 for normal maps) with mipmaps, cached in `bin/cache/textures`, and uploaded over several frames, smallest mip first |
| **Normal Maps**            | Tangent-space normal maps on OBJ and glTF models; tangents read from glTF or generated from UVs |
| **Skybox**                 | Equirectangular panorama (png/jpg/hdr) behind the scene: `withSkybox(path, intensity, rotation)`, saved in scene files, editable in the Properties panel |
| **HDR & Bloom**            | Lighting rendered in HDR, highlights rolled off instead of clipped, and bloom around emission and strong highlights (`F10`) |
| **glTF Materials**         | Base color tint, alpha modes (opaque, mask, blend), double-sided and unlit (`KHR_materials_unlit`) materials |
| **Camera System**          | Flexible camera control with quaternion-based rotations                        |
| **Input System**           | Keyboard, mouse and gamepads (SDL3), named actions and axes with bindings in code or a JSON file |
| **Lighting System**        | Directional, point, and spotlight types with intensity and cutoff controls; gamma-correct by default |
| **Transparency & Sorting** | Back-to-front sorting for transparent objects with opacity support             |
| **Audio**                  | 3D positioned sounds following the camera, one-shots, streamed music and volume buses (miniaudio); effects pause with `F7` |
| **Game UI**                | Menus and HUD as RmlUi documents (HTML/CSS-like RML/RCSS) with data binding, transitions and animations; `F9` opens its debugger |
| **Lua Scripting**          | Entity and game scripts with hot reload, the engine's API, and C++ functions handed to them (Lua 5.4, sol2) |
| **Project Launcher**       | Creates Lua, C++ or C++ + Lua projects, builds and plays them, opens them in VS Code |
| **Skeletal Animation**     | glTF skins and clips (linear, step, cubic spline), crossfades, rigid parts following their nodes, animated shadows |
| **Physics**                | Jolt: static, dynamic and kinematic bodies (box, sphere, capsule, convex hull, mesh), a character controller, impulses, raycasts and contact events |
| **Play / Stop**            | Freeze the game, edit, Play from there and Stop back to the saved state, game components included (`Ctrl+P`) |
| **Hierarchy & Prefabs**    | Parent/child entities (`setParent`, `withParent`) and prefabs (`savePrefab`, `instantiate`), saved in scenes and edited in the editor |
| **Collisions**             | Axis-aligned boxes (`withCollision`, or fitted to the model with `withModelCollision`), queried by component pair (`forEachCollision<Bullet, Enemy>`) and by ray (`raycast<Enemy>`) |
| **Particle System**        | Advanced emitter with cone/sphere/box/ring shapes, attraction, and recycling   |
| **Gravitational Lensing**  | `GravityLens` component bends particles behind it into primary and secondary images |
| **Performance Overlay**    | `F5` shows FPS, frame time, work time before the buffer swap and the worst frame |
| **VSync & Pause**          | `F6` toggles vsync; `F7` pauses the simulation while the camera keeps moving |
| **Scenes**                 | Save the running scene to JSON and load or switch scenes at runtime (`Engine::saveScene`/`loadScene`, Settings panel) |
| **Screenshots**            | `F12` (or `--screenshot`) saves the current frame as a BMP                     |
| **UI Integration**         | ImGui-based editor (`F2`) with click-to-select in the scene, move/rotate/scale gizmos (ImGuizmo), material inspector, light tweaks, entity hierarchy and a settings panel |
| **Regression Tests**       | `tools/regression.py` compares screenshots and frame times of every example and scene with references, on repeatable `--deterministic` runs |
| **Settings Panel**         | Every toggle with its key (`F1`–`F10`), time scale, camera FOV/speed, audio volumes, screenshots and render stats (draw calls, culled meshes, particles) |
| **Scene Management**       | Entity lifecycle management and component composition                          |

---

<a id="architecture"></a>

## Architecture

The engine is built on the **Entity-Component-System (ECS)** pattern, which separates data (components) from logic (systems) for maximum flexibility and performance.

```
┌────────────────────────────────────────────────────────┐
│                        Engine                          │
│  ┌─────────────┐  ┌──────────────┐  ┌───────────────┐  │
│  │  ECS Core   │  │   Renderer   │  │  Input/Window │  │
│  │  (Entities, │  │  (OpenGL,    │  │  (SDL3)       │  │
│  │  Components,│  │  Shaders,    │  │               │  │
│  │  Systems)   │  │  Shadows)    │  │               │  │
│  └─────────────┘  └──────────────┘  └───────────────┘  │
│  ┌─────────────┐  ┌──────────────┐  ┌───────────────┐  │
│  │  Resources  │  │  Scenes      │  │  Editor UI    │  │
│  │  (OBJ/glTF, │  │  (Lifecycle, │  │  (ImGui:      │  │
│  │  mesh and   │  │  JSON save/  │  │  inspector,   │  │
│  │  texture    │  │  load and    │  │  hierarchy,   │  │
│  │  caches)    │  │  switching)  │  │  settings)    │  │
│  └─────────────┘  └──────────────┘  └───────────────┘  │
│  ┌─────────────┐  ┌──────────────┐                     │
│  │  Audio      │  │  Game UI     │                     │
│  │  (miniaudio,│  │  (RmlUi:     │                     │
│  │  3D sound)  │  │  menus, HUD) │                     │
│  └─────────────┘  └──────────────┘                     │
└────────────────────────────────────────────────────────┘
```

### Directory Organization

| Directory   | Purpose                                             |
| ----------- | --------------------------------------------------- |
| `src/`      | Source and header files organized by engine module  |
| `external/` | Third-party dependencies and libraries              |
| `assets/`   | Game assets — models, shaders, sounds, scenes and game UI documents |

---

<a id="prerequisites"></a>

## Prerequisites

- **CMake** 3.14+
- **C++ compiler** with modern standard support (C++17 or later recommended)
- **OpenGL** compatible graphics driver
- **Git**

---

<a id="installation"></a>

## Installation

**Clone the repository:**

```bash
git clone https://github.com/phmuni/modular-engine.git
cd modular-engine
```

Bundled dependencies (ImGui, glad, stb_image, Tiny_Obj, tinygltf) live in `external/`. SDL3, GLM, nlohmann/json, miniaudio, RmlUi and FreeType are fetched automatically by CPM at configure time (internet required on first build).

---

<a id="project-structure"></a>

## Project Structure

```
modular-engine/
├── src/                        # Source and header files
│   ├── components/             # ECS component definitions and their JSON form for scene files
│   ├── foundation/             # Core engine, ECS, config and file caches
│   ├── game/                   # Example apps: Space Aliens, Black Hole, Stress, a model viewer and a scene browser
│   ├── rendering/              # Renderer and resources: meshes, materials, texture conversion and streaming
│   └── systems/                # ECS systems: rendering, lights, audio, game UI, scenes, editor UI...
├── cmake/                      # CPM package manager script
├── external/                   # Bundled third-party dependencies
├── assets/                     # Models, shaders, sounds, scenes (JSON) and game UI (RML/RCSS)
├── bin/                        # Compiled binaries
├── build/                      # CMake build output
└── CMakeLists.txt              # Build configuration
```

---

<a id="building"></a>

## Building

**1. Generate build files:**

```bash
cmake -S . -B build                              # Release (default)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug     # Debug, without optimizations
```

**2. Compile the engine:**

```bash
cmake --build build
```

**3. Run:**

```bash
./bin/engine              # Space Aliens
./bin/engine blackhole    # Black hole
./bin/engine stress       # 2500 lit, shadowed boxes (for benchmarking)
./bin/engine view path/to/model.glb   # any OBJ or glTF/GLB, framed automatically
./bin/engine scene [file.json]        # scene files in assets/scenes; [ and ] switch between them
```

In Space Aliens a chase camera follows the ship: `W` `A` `S` `D` or the arrows fly up, down and sideways, `Space` fires, and four waves of aliens fly in from the far end into formations, shoot at the ship and dive at it.

In the black hole example and the model viewer the camera orbits on its own: `←` `→` orbit, `↑` `↓` change the elevation and `W` `S` zoom.

**Controls (all examples):**

| Key | Action |
| --- | --- |
| `F1` | Wireframe |
| `F2` | Debug UI (scene explorer, properties, settings) |
| `F3` | Shadows |
| `F4` | Camera fly mode |
| `F5` | Performance overlay |
| `F6` | VSync |
| `F7` | Pause the simulation |
| `F8` | Gamma-correct lighting |
| `F9` | Game UI (RmlUi) debugger |
| `F10` | Bloom |
| `F12` | Screenshot (`bin/screenshot.bmp`) |
| `Right Alt` / `Right Option` | Cursor lock |
| `W` `A` `S` `D`, `Space`, `Shift` + mouse | Free camera, when enabled (with the cursor locked) |
| Left click in the scene | Editor: select the model under the cursor (empty space clears the selection; debug UI shown, cursor unlocked) |
| `Ctrl` + `P` | Editor: freeze the game to edit the scene, Play from there, Stop to put the scene back (also the bar at the top) |
| `Ctrl` + `Z` / `Ctrl` + `Shift` + `Z` or `Ctrl` + `Y` | Editor: undo / redo the last scene edit (also the Undo and Redo buttons in the bar at the top) |
| `W` / `E` / `R` / `T` | Editor gizmo on the selected entity: move / rotate / scale / resize (cursor unlocked; `Ctrl` snaps; **Resize** puts a handle on each face, and dragging one moves that face alone; **Solid** in the Properties panel stops it where its triangles touch another model's, and it slides along; **Align to surface** makes the move gizmo place it on the surface under the cursor, tilted to its slope) |

To save a frame without a window manager screenshot, pass `--screenshot <file.bmp> [seconds]`; the engine waits that long (3 seconds by default), saves the frame and exits:

```bash
./bin/engine blackhole --screenshot black-hole.bmp 4
```

| Space Aliens | Black Hole |
| --- | --- |
| <img src="docs/images/space-aliens.png" alt="Space Aliens example"/> | <img src="docs/images/black-hole.png" alt="Black hole example"/> |

To turn what an example builds into a scene file, pass `--save-scene <file.json> [seconds]`; the engine runs that long (0 by default), saves and exits:

```bash
./bin/engine blackhole --save-scene ../assets/scenes/black_hole.json 0.5
```

### Regression tests

`tools/regression.py` runs every example and scene with `--deterministic` (fixed 1/60 s frames, seeded randomness, scene switches and texture uploads that do not depend on background threads), so a build renders the same pixels every time. It compares a screenshot of each with a reference PNG and a short benchmark with a stored baseline, and fails on changed pixels, warnings in the log, or frames more than 20 % slower:

```bash
python3 tools/regression.py --update   # record references and baselines from a build you trust
python3 tools/regression.py            # after a change: exit code 1 on a regression
python3 tools/regression.py --only aliens --images-only
```

References and baselines describe this machine's GPU, speed and build type, so they live in `tests/regression/` and are not committed; changed screenshots and diff images (changes in red) go to `tests/regression/out/`. Python's standard library is enough.

### Benchmarking

`--benchmark [seconds]` (10 by default) turns vsync off, skips a 1 s warm-up and logs frame-time statistics before exiting:

```bash
./bin/engine stress --benchmark 8
# [Benchmark] 21040 frames | avg 0.380 ms (2630 FPS) | p50 0.346 | p95 0.400 | p99 1.164 | max 3.526 ms
```

Average frame time on an Apple M5 at 1280×720, 8 s runs:

| Scene | Debug | Release | Release + optimizations |
| --- | ---: | ---: | ---: |
| Space Aliens | 0.50 ms | 0.27 ms | 0.27 ms |
| Black Hole | 16.04 ms | 0.98 ms | 0.78 ms |
| Stress | 24.18 ms | 6.32 ms | 0.38 ms |

The optimizations are instanced drawing for opaque models and shadows, sparse-set component storage, frustum culling, hardware shadow PCF, allocation-free uniform lookups and reused particle buffers.

---

<a id="usage"></a>

## Usage

The engine exposes an `App` base class — override `setup()` and `update()` to implement your game logic. [`src/game/`](src/game/) contains four examples, a complete game (Space Aliens), a black hole scene, a stress scene for benchmarking and a model viewer, all launched from [`src/main.cpp`](src/main.cpp) (`./bin/engine --help` lists them).

### Creating a game

A game is its own project, in one of three kinds:

| Kind | The game's code | Build |
| --- | --- | --- |
| **Lua** | Scripts in `assets/scripts/` | None: the engine's prebuilt runner plays it, and scripts reload when saved |
| **C++** | `src/*.cpp`, against the engine as a library | CMake |
| **C++ + Lua** | Both: C++ hands functions to the scripts | CMake for the C++; scripts reload when saved |

The **launcher** creates, builds and plays them. Build the engine once, then open it:

```bash
cmake -B build && cmake --build build -j
./bin/launcher/launcher
```

**New project** asks for a name, a folder and the kind; each project in the list then has **Play** (a C++ project
builds what changed first), **Build**, **VS Code** and **Folder**. From a terminal, the same:

```bash
./bin/launcher/launcher --create ~/Games/Asteroids lua     # or cpp, mixed
python3 tools/new_game.py ~/Games/Asteroids --type lua     # the same, in Python (asks the kind when left out)
```

Each kind starts as the same small game (walk, jump, push the crates, collect coins: [`templates/`](templates/)),
whose level is a scene file you edit in the game with `F2`.

A Lua project is `project.json` (its name, start scene, game script and input file), `assets/`, and `bin/` with the
runner named after the game. A C++ project's `CMakeLists.txt` points at this checkout (`MODULAR_ENGINE_DIR`; left
empty, the engine is downloaded from GitHub) and builds `src/*.cpp` with `modular_engine_add_game(...)`. Either way
`bin/` holds `bin/engine-assets` (the engine's shaders, fonts and basic models) and SDL's library, asset paths stay
`"../assets/..."` relative to the executable, and the folder is the whole game, runnable from anywhere. Every game
gets `--screenshot`, `--benchmark`, `--deterministic` and `--save-scene` on its command line.

Setting the `CPM_SOURCE_CACHE` environment variable to a folder shares the dependency downloads (SDL, RmlUi...)
between C++ projects.

### Lua scripts

An entity's `Script` component names a Lua file (`scripts/coin.lua`, relative to `assets/`) that gives it its behavior.
A game script (`scripts().runGameScript(...)`, or `project.json`'s `script`) runs for the whole game; the shared `game`
table is seen by every script. Saved scripts reload while the game runs, keeping each entity's state, and errors show
on screen with their file and line without stopping the game.

```lua
-- scripts/coin.lua
function onStart(self)                       -- once; fields on self are this entity's own state
  self.spin = 120
end
function onUpdate(self, dt)                  -- every frame
  local rotation = self.rotation
  rotation.y = rotation.y + self.spin * dt
  self.rotation = rotation
end
function onOverlap(self, other)              -- its Collision box overlaps another; also onContact(self, other, point,
  if other.name == "Player" then             --   normal) for physics bodies, and onReload(self) after an edit
    game.score = game.score + 1
    sound.play("sounds/coin.wav")
    self:destroy()
  end
end
```

What scripts can use:

| | |
| --- | --- |
| `vec3(x, y, z)` | `+ - * /`, `:length()`, `:normalized()`, `:dot(v)`, `:cross(v)`, `:distance(v)`, `:lerp(v, t)` |
| Entities (`self`, `other`) | `name`, `position`, `rotation`, `forward`, `transform`, `light`, `valid`, `:destroy()`, `:has("RigidBody")`, `:parent()`, `:setParent(e)`, `:children()`, `:setColor(c)`, `:setEmission(c, strength)`, `:setVisible(b)`; bodies: `velocity`, `:impulse(v)`, `:force(v)`; characters: `:move(v)`, `:jump(speed)`, `onGround`; `:play(clip, fade, loop)`, `:clips()`, `:sound(path, volume)`; any other field is the entity's own (`other.health`) |
| `engine` | `find(name)`, `create(name, { model, position, rotation, scale, color, emission, body, shape, mass, collision, script })`, `spawn(prefab, position, rotation)`, `destroy(e)`, `loadScene(path)`, `setTitle(text)` |
| `input` | `axis(name)`, `down(name)`, `pressed(name)`, `released(name)` (the actions and axes of `input.json`), `key("Space")`, `keyPressed("Space")`, `mouse()`, `click(button)`, `bindings(path)` |
| `camera` | `lookAt(from, target)`, `position()`, `forward()` |
| `physics` | `raycast(origin, direction, distance)` (a table with `entity`, `point`, `normal`, `distance`), `gravity(v)` |
| `sound` | `play(path, volume, pitch)`, `playAt(path, position, volume)`, `music(path, volume)`, `stopMusic()` |
| `ui` | `load(path)` (an RmlUi document), `text(id, value)`, `show(id, visible)`, `class(id, name, on)`, `onClick(id, function)` |

`require("name")` loads `assets/scripts/name.lua`, and `print` writes to the engine's log. C++ hands its own functions
to scripts with sol2:

```cpp
#include <sol/sol.hpp>
engine.scripts().lua()["celebrate"] = [&] { /* ... */ };   // in Lua: celebrate()
```

### Quick Start

```cpp
#include "foundation/core/engine.h"

class MyApp : public App {
    Entity player;

    void setup(Engine& engine) override {
        // Create player entity with the entity builder
        player = engine.entities()
            .create("Player")
            .withModel(
                EngineConfig::MODEL_BOX,
                glm::vec3(0.0f),   // position
                glm::vec3(0.0f),   // rotation
                glm::vec3(1.0f)    // scale
            )
            .withSolidColor(glm::vec3(0.2f, 0.8f, 1.0f))
            .withEmission(glm::vec3(0.2f, 0.8f, 1.0f), 1.5f)
            .build();

        // A red sun: direction, color, intensity
        engine.entities()
            .create("Sun")
            .withLight(Light::directional({-0.8f, -1.0f, 0.2f}, {1.0f, 0.0f, 0.0f}, 2.0f))
            .build();

        // A spot from above: direction, color, intensity, and the angles (degrees) where it fades and ends
        engine.entities()
            .create("Spotlight")
            .withTransform({0.0f, 10.0f, 0.0f}, glm::vec3(0.0f), glm::vec3(1.0f))
            .withLight(Light::spot({0.0f, -1.0f, 0.0f}, glm::vec3(1.0f), 3.0f, 15.0f, 25.0f))
            .build();

        // A point light with a cube shadow map (up to EngineConfig::MAX_POINT_SHADOWS at once) reaching 20 units
        engine.entities()
            .create("Lamp")
            .withTransform({2.0f, 1.5f, 0.0f}, glm::vec3(0.0f), glm::vec3(1.0f))
            .withLight(Light::point({1.0f, 0.8f, 0.6f}, 2.0f).withShadows(20.0f))
            .build();

        engine.createCameraEntity("Camera", glm::vec3(0, 0, 5));
        engine.getActiveCamera()->lookAt(glm::vec3(0, 2, 6), glm::vec3(0.0f));  // or set yaw/pitch
    }

    void update(Engine& engine, float dt) override {
        auto& input = engine.input();
        if (input.wasKeyPressed(SDL_SCANCODE_SPACE))           // once per press
            jump();
        if (input.isKeyDown(SDL_SCANCODE_A))                   // while held
            moveLeft(dt);
        if (input.wasMouseButtonPressed(MouseButton::Left))    // clicks the game UI or the editor took are left out
            shoot(input.getMousePosition());
    }
};

int main() {
    Engine engine;
    if (!engine.initialize()) return 1;

    MyApp app;
    engine.run(app);
    return 0;
}
```

### Custom Components

Any plain struct works as a component — no base class or registration required:

```cpp
struct Health {
    int current = 3;
    int max = 3;
};

struct Velocity {
    glm::vec3 direction;
    float speed;
};

// Create entity and add components
Entity e = engine.entities()
    .create("Enemy")
    .withComponent<Health>(3, 3)
    .withComponent<Velocity>(glm::vec3(1, 0, 0), 2.0f)
    .build();

// Query and modify components
auto& health = engine.get<Health>(e);
health.current -= 1;

// get's error says whether the entity lacks the component or no longer exists:
//   "entity 8 no longer exists (destroyed, or its scene was replaced), so it has no Health"

// Safe query (returns nullptr if component doesn't exist)
if (auto* vel = engine.tryGet<Velocity>(e)) {
    vel->speed *= 1.5f;
}

// Check if component exists
if (engine.has<Health>(e)) {
    // Do something
}

// Remove component
engine.removeComponent<Velocity>(e);

// Destroying unregisters the entity's light or camera too; collect entities first when iterating their components
engine.destroyEntity(e);
engine.isAlive(e);  // false: handles of destroyed entities, or of a scene that loadScene replaced
```

### Physics

[Jolt Physics](https://github.com/jrouwe/JoltPhysics) simulates entities with a `RigidBody`: static (floors, walls,
level geometry), dynamic (falls, collides, slides, bounces) or kinematic (follows its Transform and pushes what it
meets). The shape is a box, sphere, capsule, convex hull or the exact triangle mesh, sized from the model unless a size
is given, and scaled by the Transform. A `CharacterController` walks on all of it: it stays on the ground, climbs
slopes and steps, jumps and pushes dynamic bodies.

```cpp
engine.entities().create("Floor").withModel("../assets/models/level.glb", {0, 0, 0})
    .withRigidBody(BodyType::Static, BodyShape::Mesh).build();
Entity crate = engine.entities().create("Crate").withModel(EngineConfig::MODEL_BOX, {0, 5, 0})
    .withRigidBody(BodyType::Dynamic, BodyShape::Box, 20.0f /* kg */).build();
Entity hero = engine.entities().create("Hero").withModel("../assets/models/hero.glb", {0, 1, 4})
    .withCharacter(0.35f /* radius */, 1.8f /* height */).build();

// Each frame
auto &character = engine.get<CharacterController>(hero);
character.move = direction * 5.0f;              // horizontal velocity it wants
if (jumpPressed && character.onGround)
    character.jump = 6.0f;                      // upward speed

engine.physics().addImpulse(crate, {0, 200, 0});  // also velocities, forces, gravity
if (auto hit = engine.physics().raycast(origin, direction, 50.0f)) { /* hit->entity, point, normal */ }
engine.forEachContact<Bullet, Enemy>([&](Entity bullet, Bullet &, Entity enemy, Enemy &) { /* ... */ });
```

The simulation steps at a fixed 60 Hz, writes dynamic bodies' and characters' Transforms, and takes Transforms moved
by game code or the editor's gizmo as teleports; changing a `RigidBody` (or the entity's scale) rebuilds its body. Both
components are saved in scenes and edited in the Properties panel, the simulation stands still while editing, and
Stop rebuilds the bodies. [`assets/scenes/physics.json`](assets/scenes/physics.json) is a small example.

### Skeletal animation

glTF models with skins and animations play their clips: vertices follow up to four joints each, parts without a skin
follow their own node (a sword in a hand), clips use linear, step or cubic-spline keys, and switching clips crossfades.
Shadows, sun and point lights alike, follow the pose.

```cpp
Entity hero = engine.entities().create("Hero").withModel("../assets/models/hero.glb", {0, 0, 0})
    .withAnimation("Idle").build();
engine.playAnimation(hero, "Run");          // fades over from Idle in 0.2 s; also (clip, fadeSeconds, loop)
engine.getAnimationClips(hero);             // the model's clips, in file order

auto &animator = engine.get<Animator>(hero); // speed, time, loop, playing
```

`./bin/engine view model.glb` plays an animated model's first clip and switches clips with `1`-`9`. The Properties
panel picks a clip and scrubs through it; `Animator` is saved in scenes ([`assets/scenes/animation.json`](assets/scenes/animation.json)).
[`tools/generateAnimatedTestModel.py`](tools/generateAnimatedTestModel.py) writes the small test model it uses. Up to
128 joints per model are animated (`EngineConfig::MAX_JOINTS`); morph targets are not supported.

### Play and Stop

With the editor open (`F2`), the bar at the top (or `Ctrl` + `P`) freezes the game for editing: the app's `update`
stops and time stands still, while the camera and the editor keep working. **Play** then runs the game from the scene
as it is, and **Stop** puts every entity and component back as they were at Play (game components too, since the
snapshot copies the component storage itself), then returns to editing. Materials the game changes during play are
copies, so the originals come back as well. **Resume** unfreezes without anything to go back to.

The same from code: `engine.edit()`, `engine.play()`, `engine.stop()`, `engine.resume()` and `getPlayMode()`. Entity
handles taken before Play are valid again after Stop; state an app keeps in its own members can be reset in
`App::onPlayStopped`.

### Undo and redo

With the editor open, `Ctrl` + `Z` undoes the last edit and `Ctrl` + `Shift` + `Z` (or `Ctrl` + `Y`) does it again; the
bar at the top has buttons for both. Every scene edit counts: gizmo and resize drags, anything in the Properties panel,
adding and removing components, creating, deleting and parenting entities, placing prefabs. One drag or one typed value
is one step, and a click that changes nothing (selecting, opening a menu) leaves none. The editor copies the whole scene
before each edit, the same way Play does, so the last 50 steps cost 50 copies of it: a big scene uses that much more
memory. Notes:

- Undo does not restart scripts: Lua state on `self` and `game` stays, and entity scripts follow their restored
  `Script` components. The free camera stays where it is, unless the camera being edited is selected.
- Undo works while editing and while the game runs, but not between Play and Stop. The history survives Play and Stop
  (Stop restores the scene as it was at Play, so the steps before it still apply), and loading a scene clears it.
- While the game runs, a step also contains whatever the game changed during the edit, and undoing takes that back too.
- Edits to the editor's own settings (the Settings panel) are not scene edits and are not undone.

The same from code: `engine.undo()`, `engine.redo()`, `canUndo()` and `canRedo()` (each takes effect at the start of the
next frame), and `beginCheckpoint()` / `endCheckpoint()` to make code that edits the scene one step.

### Hierarchy and prefabs

An entity can follow another: a lamp on a post, a wheel on a car. Its `Transform` stays its place in the world (so
rendering, lights, audio and collisions treat it like any entity), kept at the parent's transform combined with a
local one each frame. Moving the child itself (game code, the editor's gizmo) updates the local transform instead.
Destroying a parent destroys its children, and scene files save the links.

```cpp
Entity car = engine.entities().create("Car").withModel("../assets/models/car.glb", {0, 0, 0}).build();
engine.entities().create("Light").withParent(car)          // positions given here are relative to the parent
    .withTransform({0, 1.2f, -2.0f}).withLight(Light::spot({0, -0.3f, -1}, glm::vec3(1), 8.0f)).build();

engine.setParent(child, parent);        // keeps the child where it is; -1 detaches it
engine.setLocalTransform(child, local); // place it relative to the parent
engine.getParent(child); engine.getChildren(parent);
```

A prefab is an entity and its children saved to a file (the scene format, with the entity at the origin), placed in
the scene as often as needed:

```cpp
engine.savePrefab(car, "../assets/prefabs/car.json");
Entity another = engine.instantiate("../assets/prefabs/car.json", {5, 0, 0}, {0, 90, 0}); // position, rotation
```

In the editor, the Scene Explorer's Hierarchy shows the families: drag an entity onto another to attach it, or onto
the empty space to detach it; the Properties panel has a Parent field and **Save as prefab**, and **Add Entity** places
a prefab in front of the camera.

### Collisions

Entities with a `Collision` box (`withCollision(size)`, centered on their Transform position) can be asked what they touch. Positions are read when you ask, so move things first and then query; each pair comes once, already split by component type:

```cpp
engine.forEachCollision<Bullet, Enemy>([&](Entity shot, Bullet& bullet, Entity enemy, Enemy& data) {
    data.health -= bullet.damage;
    toDestroy.push_back(shot);  // destroy after the loop, or right away: later pairs with it are skipped
});

if (engine.isColliding(player, door))
    openDoor();
```

Two static boxes (`Collision::isStatic`) are never reported. Pairs are found by sorting the boxes along one axis, not by testing every pair.

`withModelCollision(fit)` sizes the box to the model's bounds as rotated and scaled, instead of a size you guess; `fit` below 1 makes a forgiving hitbox (Space Aliens uses 0.6 for the ship). The box does not rotate with the entity: after turning it a lot, call `engine.fitCollisionToModel(entity, fit)` again.

Rays find the nearest box along a line, optionally only entities with a given component. With the distance a fast object moves in a frame, they are a swept test that thin targets cannot slip through:

Models can be picked without a `Collision` box, against their triangles (the editor's click-to-select uses it). Hits carry the surface normal:

```cpp
if (engine.input().wasMouseButtonPressed(MouseButton::Left))
    if (auto hit = engine.pickModel(engine.input().getMousePosition()))
        select(hit->entity);
// Or with any ray: engine.screenRay(point) gives the camera's ray through a window point,
// engine.raycastModels(origin, direction) the nearest model triangle along it (hit->point, hit->normal)
```

```cpp
if (auto hit = engine.raycast<Enemy>(muzzle, forward, 100.0f))
    aimAt(hit->point);  // also hit->entity and hit->distance

// A fast bullet: test the path it flies this frame, then move it
if (auto hit = engine.raycast<Enemy>(bullet.position, bullet.velocity, glm::length(bullet.velocity) * dt))
    explode(hit->entity);
```

### Material & Resource Management

The Engine provides convenient helpers for applying colors, emission, and textures:

```cpp
// Apply solid color to an entity
engine.setSolidColor(entity, glm::vec3(1.0f, 0.2f, 0.1f));  // Red

// Set emission on an entity
engine.setEmission(entity, glm::vec3(1.0f, 0.2f, 0.1f), 2.0f);

// Apply texture to an entity
engine.setTexture(entity, TextureSlot::Diffuse, "path/to/texture.png");

// Apply texture to specific submesh
engine.setTexture(entity, TextureSlot::Diffuse, "path/to/texture.png", 0);

// Tangent-space normal map (OpenGL convention: green points up the image)
engine.setTexture(entity, TextureSlot::Normal, "path/to/normal.png");

// Set shininess
engine.setShininess(entity, 32.0f);

// Address a part by name: an OBJ object/group or a glTF node, as named in the file
engine.setSolidColor(car, glm::vec3(0.9f, 0.1f, 0.1f), "wheel_2");
```

Entities created from the same file share their materials; changing one gives that entity (or part) its own copy, and materials nobody uses anymore are freed. Parts that share a material are drawn together, so a model costs about one draw call per material.

Large models can be read on a worker thread ahead of time, so creating the entity later does not stall the frame:

```cpp
void setup(Engine& engine) override {
    engine.preloadModel("../assets/models/castle/castle.glb");
}

// later, when it is needed: only the GPU upload is left
engine.entities().create("Castle").withModel("../assets/models/castle/castle.glb", pos, rot, scale).build();
```

Parsing a large model is slow, so the first load writes the result to `bin/cache/meshes`; later loads read it back (the 96 MB, 1-million-triangle room in `assets/models` goes from 485 ms of geometry to 6 ms in Release; the cathedral GLB from 819 ms to 85 ms in Debug, its embedded texture coming from the texture cache). An entry is rebuilt when its file or a `.mtl`/`.bin` in the same folder changes, and deleting the folder is always safe. The file parsers are compiled with optimizations in Debug builds too.

Textures go through the same idea. The first time one is loaded, a worker thread builds its mipmaps and, when the GPU supports S3TC, compresses it to BC1 (RGB) or BC3 (RGBA): 6 or 4 times less memory, and no PNG/JPEG decode afterwards, since the result is cached in `bin/cache/textures` (with both caches, the room and its 8192×8192 JPEG load in 65 ms in a Debug build, down from 3.5 s). Normal maps compress to BC5, which keeps their X and Y precisely (the shader rebuilds Z); a grayscale "normal map" is a height map, which some `.mtl` files give as `bump`, and is skipped with a warning. On the main thread, textures are then uploaded a few megabytes per frame (`EngineConfig::TEXTURE_UPLOAD_BUDGET`), smallest mip level first, so a large one appears blurry at once and sharpens over the next frames instead of stalling one; switching to the room scene went from a 540 ms frame to none above normal rendering. `EngineConfig::COMPRESS_TEXTURES` turns compression off.

Lit geometry renders into a 16-bit floating-point target, so light can go past white. A tone mapping pass leaves colors below about 0.76 untouched and rolls brighter ones off smoothly, keeping their hue instead of clipping them; particles then draw on top in display space, where their colors and additive blending were tuned. Bloom (`F10`) spreads light brighter than `EngineConfig::BLOOM_THRESHOLD` (emission, strong highlights) through a mip chain and adds it at the end. [`assets/scenes/normal_maps.json`](assets/scenes/normal_maps.json) shows normal maps lit from the side by a point light with shadows.

For direct ResourceSystem access and full API reference, explore [`src/systems/resourceSystem.h`](src/systems/resourceSystem.h).

### Skybox

A `Skybox` component draws an equirectangular panorama (360 by 180 degrees, twice as wide as tall, the usual format of HDRI sites) behind everything. The first one in the scene is drawn, from any entity:

```cpp
engine.entities().create("Sky").withSkybox("../assets/textures/skybox/space.png", 1.0f /* intensity */, 0.0f /* rotation, degrees */).build();
```

```json
{ "name": "Sky", "components": { "Skybox": { "path": "../assets/textures/skybox/space.png", "intensity": 1.0 } } }
```

The image goes through the same texture cache and streaming as model textures. Its center column is straight ahead (-Z); `intensity` above 1 lets bright stars reach the bloom. [`tools/generateSpaceSkybox.c`](tools/generateSpaceSkybox.c) generated the space panorama used by Space Aliens.

### Input actions and gamepads

Games can ask for named actions and axes instead of keys, each fed by any of its bindings: keys, mouse buttons,
gamepad buttons and sticks. Gamepads are picked up when they are plugged in (with several, they add up), sticks get a
dead zone, and the keyboard and a gamepad work side by side:

```cpp
auto &input = engine.input();
input.bindAxis("moveX", {InputBinding::keys(SDL_SCANCODE_A, SDL_SCANCODE_D),          // -1 / +1
                         InputBinding::gamepad(SDL_GAMEPAD_AXIS_LEFTX)});
input.bindAction("jump", {InputBinding::keyboard(SDL_SCANCODE_SPACE),
                          InputBinding::gamepad(SDL_GAMEPAD_BUTTON_SOUTH)});          // A / Cross

float x = input.getAxis("moveX");          // -1 to 1, the strongest binding
if (input.wasActionPressed("jump")) { }   // also isActionDown, wasActionReleased
```

Bindings can also come from a file, so they change without a build (`input.loadBindings("../assets/input.json")`):

```json
{ "actions": { "jump": ["Space", "gamepad:south"], "fire": ["mouse:left", "gamepad:righttrigger"] },
  "axes":    { "moveX": ["A/D", "Left/Right", "gamepad:leftx"], "moveY": ["S/W", "gamepad:-lefty"] } }
```

The gamepad itself is readable too: `isGamepadButtonDown`, `wasGamepadButtonPressed`, `getGamepadAxis`,
`getGamepadCount`.

### Audio

Sounds are WAV, MP3 or FLAC files, with paths relative to `bin/` like models. The listener is the active camera.

```cpp
void setup(Engine& engine) override {
    engine.preloadSound("../assets/sounds/explosion.wav"); // decoded in the background
    engine.playMusic("../assets/sounds/music.wav", 0.4f);  // streamed from disk, loops
}

// Fire-and-forget effects: heard the same anywhere, or placed in the world
engine.playSound("../assets/sounds/laser.wav", 0.35f);
engine.playSoundAt("../assets/sounds/explosion.wav", position, 0.8f, /*pitch*/ 1.05f);

// A sound that follows an entity
Entity fan = engine.entities()
                 .create("Fan")
                 .withModel("../assets/models/box/box.obj", pos, rot, scale)
                 .withSound("../assets/sounds/hum.wav", /*loop*/ true, /*volume*/ 0.6f)
                 .build();
engine.get<AudioSource>(fan).playing = false; // stop it; true starts it again
```

`AudioSource` also sets pitch, whether it is spatial, and how it fades with distance (`minDistance`, `maxDistance`, `rolloff`). Effects (sources and one-shots) pause with `F7` and follow the time scale; music keeps playing. The Settings panel has master, effects and music volumes, and the Properties panel edits an entity's `AudioSource`. Without an audio device the engine runs silently. See [`src/systems/audioSystem.h`](src/systems/audioSystem.h).

### Scenes

A scene file is JSON with one object per entity: its name and its components. Missing fields take the component's defaults, so files can be short and written by hand ([`assets/scenes/playground.json`](assets/scenes/playground.json); [`point_shadows.json`](assets/scenes/point_shadows.json) shows two lights with cube-map shadows):

```json
{
  "format": "modular-engine-scene",
  "version": 1,
  "entities": [
    {
      "name": "Red Box",
      "components": {
        "Transform": { "position": [-1.6, 0.25, 0.0], "rotation": [0.0, 25.0, 0.0] },
        "Model": { "path": "../assets/models/box/box.obj", "parts": [{ "part": 0, "color": [0.85, 0.2, 0.2] }] },
        "Collision": {}
      }
    }
  ]
}
```

```cpp
engine.saveScene("../assets/scenes/level1.json");  // now
engine.loadScene("../assets/scenes/level2.json");  // reads its models in the background, then replaces every entity

// Called after a load: handles to the old entities are gone, so look up what the game needs again
void onSceneLoaded(Engine& engine, const std::string& path) override { /* ... */ }
```

Saved: `Transform`, `Model` (the file plus per-part color, textures, emission and shininess changes), `Light`, `Camera` (and which one is active), `Collision`, `ParticleEmitter`, `GravityLens`, `AudioSource`, `Skybox` and `Hidden`. Runtime state such as live particles is not. Game components are saved once registered; plain data only needs `to_json`/`from_json`:

```cpp
#include "systems/sceneSerializer.h"

struct Health { float current = 100.0f; float max = 100.0f; };
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Health, current, max)

engine.getSceneSerializer().registerComponent<Health>("Health");
```

Components with handles or links to systems use `SceneSerializer::registerComponent(key, save, load)` instead (see [`src/systems/sceneSerializer.h`](src/systems/sceneSerializer.h)). In the debug UI (`F2`), the Settings panel saves and loads scenes and lists those in `assets/scenes` to switch with a click. `loadScene` reads the new scene's models on worker threads while the current one keeps running (`isLoadingScene()` tells when it is waiting), then switches at the end of a frame. Only uploading to the GPU stays on the main thread.

### Game UI

Menus and HUDs are [RmlUi](https://mikke89.github.io/RmlUiDoc/) documents: RML (HTML-like) for structure and RCSS (CSS-like) for style, with flexbox, gradients, shadows, transitions and animations. They are drawn over the scene and under the ImGui debug panels. A data model ties them to game state:

```cpp
#include <RmlUi/Core.h>

struct Hud { int score = 0; } hud;
Rml::DataModelHandle hudModel;

void setup(Engine& engine) override {
    // Data models before the documents that use them
    Rml::DataModelConstructor model = engine.getUIContext()->CreateDataModel("hud");
    model.Bind("score", &hud.score);
    model.BindEventCallback("restart", [this](Rml::DataModelHandle, Rml::Event&, const Rml::VariantList&) { /* ... */ });
    hudModel = model.GetModelHandle();

    engine.loadUIDocument("../assets/ui/hud.rml");
}

void update(Engine& engine, float dt) override {
    hud.score = 42;
    hudModel.DirtyVariable("score"); // shown this frame
}
```

```html
<!-- hud.rml -->
<rml>
<head><link type="text/rcss" href="hud.rcss"/></head>
<body data-model="hud">
    <div class="score">{{ score }}</div>
    <button data-event-click="restart">Play again</button>
</body>
</rml>
```

RCSS has no default style sheet, so give `div` a `display: block`. Lato is loaded as `font-family: LatoLatin`; `engine.loadUIFont` adds others. Clicks and key presses the UI uses don't reach the game, and `pointer-events: none` lets clicks pass through HUD areas. `F9` opens RmlUi's debugger to inspect elements and see style errors. The Space Aliens HUD ([`assets/ui/aliens/`](assets/ui/aliens/)) is a complete example.

---

<a id="technologies"></a>

## Technologies

| Category       | Technology          |
| -------------- | ------------------- |
| Language       | Modern C++ (C++17+) |
| Graphics API   | OpenGL              |
| Build System   | CMake               |
| Window & Input | SDL3                |
| Mathematics    | GLM                 |
| Asset Loading  | Tiny_Obj, tinygltf  |
| Audio          | miniaudio           |
| UI Framework   | Dear ImGui and ImGuizmo (editor), RmlUi (game UI) |
| Dependencies   | See `external/`     |

---

<a id="contributing"></a>

## Contributing

Contributions are welcome! Please follow these steps:

1. **Fork** the repository
2. **Create** a feature branch
   ```bash
   git checkout -b feature/my-feature
   ```
3. **Commit** your changes with a descriptive message
   ```bash
   git commit -m "feat: add my feature"
   ```
4. **Push** to your fork
   ```bash
   git push origin feature/my-feature
   ```
5. **Open** a Pull Request

Check the [Issues](https://github.com/phmuni/modular-engine/issues) and [Projects](https://github.com/phmuni/modular-engine/projects) pages for open tasks and planned features.

---

<a id="license"></a>

## License

This project is licensed under the **MIT License** — see the [LICENSE](LICENSE) file for details.

---

<div align="center">

Made by [phmuni](https://github.com/phmuni) · [Report a bug](https://github.com/phmuni/modular-engine/issues) · [Request a feature](https://github.com/phmuni/modular-engine/issues)

</div>
