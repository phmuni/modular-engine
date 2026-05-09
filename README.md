<div align="center">

# Modular Engine

**A modular 3D graphics engine built with modern C++**

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![C++](https://img.shields.io/badge/C%2B%2B-Modern-00599C?logo=c%2B%2B)](https://isocpp.org/)
[![CMake](https://img.shields.io/badge/CMake-3.10%2B-064F8C?logo=cmake)](https://cmake.org/)
[![OpenGL](https://img.shields.io/badge/OpenGL-Rendering-5586A4?logo=opengl)](https://www.opengl.org/)

Built on a robust **Entity-Component-System (ECS)** architecture with flexible rendering capabilities using OpenGL and SDL3.

[Features](#features) · [Architecture](#architecture) · [Installation](#installation) · [Usage](#usage) · [Contributing](#contributing)

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

| Category | Description |
|---|---|
| **ECS Architecture** | Flexible, decoupled Entity-Component-System for scalable game logic |
| **App Base Class** | Simple `App` interface with `setup()` and `update()` for clean game logic |
| **Generic Components** | Any plain struct works as a component — no base class required |
| **OpenGL Rendering** | Modern graphics API integration with full shader support |
| **PBR Material System** | Per-submesh materials with diffuse, specular, normal, and emission maps |
| **Resource Caching** | Automatic texture caching with handle-based resource management |
| **Shadow Mapping** | Real-time shadow rendering with configurable depth maps |
| **Asset Loading** | OBJ support via Tiny_Obj with automated resource management |
| **Camera System** | Flexible camera control with quaternion-based rotations |
| **Input System** | Keyboard and mouse handling via SDL3 |
| **Lighting System** | Directional, point, and spotlight types with PBR shading |
| **UI Integration** | ImGui-based editor with material inspector and entity hierarchy |
| **Scene Management** | Entity lifecycle management and component composition |

---

<a id="architecture"></a>
## Architecture

The engine is built on the **Entity-Component-System (ECS)** pattern, which separates data (components) from logic (systems) for maximum flexibility and performance.

```
┌─────────────────────────────────────────────────────────┐
│                        Engine                           │
│  ┌─────────────┐  ┌──────────────┐  ┌───────────────┐  │
│  │  ECS Core   │  │   Renderer   │  │  Input/Window │  │
│  │  (Entities, │  │  (OpenGL,    │  │  (SDL3)       │  │
│  │  Components,│  │  Shaders,    │  │               │  │
│  │  Systems)   │  │  Shadows)    │  │               │  │
│  └─────────────┘  └──────────────┘  └───────────────┘  │
│  ┌─────────────┐  ┌──────────────┐  ┌───────────────┐  │
│  │  Asset Mgr  │  │  Scene Mgr   │  │  UI (ImGui)   │  │
│  │  (Tiny_Obj, │  │  (Lifecycle, │  │  (Inspector,  │  │
│  │  Caching)   │  │  Composition)│  │  Hierarchy)   │  │
│  └─────────────┘  └──────────────┘  └───────────────┘  │
└─────────────────────────────────────────────────────────┘
```

### Directory Organization

| Directory | Purpose |
|---|---|
| `src/` | Implementation files for all systems and components |
| `internal/` | Header files defining interfaces and structures |
| `external/` | Third-party dependencies and libraries |
| `assets/` | Game assets — models, shaders, textures, sounds |

---

<a id="prerequisites"></a>
## Prerequisites

- **CMake** 3.10+
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

All dependencies are included in the `external/` directory. See `CMakeLists.txt` for the complete dependency list.

---

<a id="project-structure"></a>
## Project Structure

```
modular-engine/
├── src/                        # Implementation files
│   ├── foundation/             # Core engine functionality
│   ├── rendering/              # Rendering backend
│   └── systems/                # ECS system logic
├── internal/                   # Header files and interfaces
│   ├── components/             # Component definitions
│   ├── foundation/             # Core structures and interfaces
│   ├── rendering/              # Renderer interfaces and API
│   └── systems/                # System interfaces
├── external/                   # Third-party dependencies
├── assets/                     # Models, shaders, textures
├── bin/                        # Compiled binaries
├── build/                      # CMake build output
└── CMakeLists.txt              # Build configuration
```

---

<a id="building"></a>
## Building

**1. Generate build files:**

```bash
cmake -S . -B build
```

**2. Compile the engine:**

```bash
cmake --build build
```

**3. Run:**

```bash
./bin/engine
```

---

<a id="usage"></a>
## Usage

The engine exposes an `App` base class — override `setup()` and `update()` to implement your game logic. See [`src/foundation/core/main.cpp`](src/foundation/core/main.cpp) for a complete example.

### Quick Start

```cpp
#include "foundation/core/engine.h"

class MyApp : public App {
    Entity player;

    void setup(Engine& engine) override {
        player = engine.createModelEntity(
            "Player",
            EngineConfig::MODEL_BOX,
            glm::vec3(0.0f),   // position
            glm::vec3(0.0f),   // rotation
            glm::vec3(1.0f)    // scale
        );

        engine.createLightEntity(
            "Sun",
            glm::vec3(2, 3, 2),
            glm::vec3(-1, -1, -1),
            glm::vec3(1.0f),
            LightType::Directional,
            1.5f, 0, 0
        );

        engine.createCameraEntity(glm::vec3(0, 0, 5));
    }

    void update(Engine& engine, float dt) override {
        // Game logic here
    }
};

int main() {
    Engine engine;
    if (!engine.init()) return 1;

    MyApp app;
    engine.run(app);
    return 0;
}
```

### Custom Components

Any plain struct works as a component — no base class or registration required:

```cpp
struct Health   { int current = 3; int max = 3; };
struct Velocity { glm::vec3 dir; float speed;   };

Entity e = engine.createEntity();
engine.addComponent<Health>(e, 3, 3);
engine.addComponent<Velocity>(e, glm::vec3(1, 0, 0), 2.0f);

auto* hp = engine.tryGetComponent<Health>(e);
if (hp) hp->current -= 1;
```

For the full API reference, explore the header files in [`internal/`](internal/).

---

<a id="technologies"></a>
## Technologies

| Category | Technology |
|---|---|
| Language | Modern C++ (C++17+) |
| Graphics API | OpenGL |
| Build System | CMake |
| Window & Input | SDL3 |
| Mathematics | GLM |
| Asset Loading | Tiny_Obj |
| UI Framework | Dear ImGui |
| Dependencies | See `external/` |

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
