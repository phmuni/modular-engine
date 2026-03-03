# Modular Engine

A modular 3D graphics engine developed in modern C++. Built on a robust Entity-Component-System (ECS) architecture with flexible rendering capabilities using OpenGL and SDL3.

## 📋 Table of Contents

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

## ✨ Features

- **Entity-Component-System (ECS)** - Flexible, decoupled architecture for scalable game logic
- **App Base Class** - Simple `App` interface with `setup()` and `update()` for clean game logic
- **Generic Components** - Any plain struct works as a component
- **OpenGL Rendering** - Modern graphics API integration with shader support
- **PBR Material System** - Per-submesh materials with diffuse, specular, normal, and emission maps
- **Resource Caching** - Automatic texture caching and handle-based resource management
- **Shadow Mapping** - Real-time shadow rendering with configurable depth maps
- **Asset Loading** - Support for OBJ/FBX formats via Assimp with automated resource management
- **Camera System** - Flexible camera control with quaternion-based rotations
- **Input System** - Keyboard and mouse input handling with SDL3
- **Lighting System** - Multiple light types (directional, point, spotlight) with PBR shading
- **UI Integration** - ImGui-based editor with material inspector and entity hierarchy
- **Scene Management** - Entity lifecycle management and component composition

## 🏗️ Architecture

The engine is built on the **Entity-Component-System (ECS)** pattern, which separates data (components) from logic (systems) for maximum flexibility and scalability.

### Directory Organization

- **`src/`** - Implementation files for all systems and components
- **`internal/`** - Header files defining interfaces and structures
- **`external/`** - Third-party dependencies and libraries
- **`assets/`** - Game assets (models, shaders, textures, sounds)

This modular structure allows for easy expansion and maintenance as the engine evolves.

## 📦 Prerequisites

- **CMake** 3.10+
- **C++ compiler** with modern standard support
- **OpenGL** compatible graphics driver
- **Git**

## 🚀 Installation

### Clone the Repository

```bash
git clone https://github.com/phmuni/modular-engine.git
cd modular-engine
```

### Dependencies

All dependencies are included in the `external/` directory. The project uses industry-standard libraries for graphics, mathematics, and utilities. See `CMakeLists.txt` for the complete dependency list.

## 📂 Project Structure

```
├── src/                    # Implementation files
│   ├── foundation/         # Core engine functionality
│   ├── rendering/          # Rendering backend implementation
│   └── systems/            # ECS system logic
├── internal/               # Header files and interfaces
│   ├── components/         # Component definitions
│   ├── foundation/         # Core structures and interfaces
│   ├── rendering/          # Renderer interfaces and API
│   └── systems/            # System interfaces
├── external/               # Third-party dependencies
├── assets/                 # Game assets (models, shaders, textures)
├── bin/                    # Compiled binaries
├── build/                  # CMake build directory
└── CMakeLists.txt          # Build configuration

```

## 🔨 Building

### Generate Build Files

```bash
cmake -S . -B build
```

### Compile the Engine

```bash
cmake --build build
```

### Run the Engine

```bash
./bin/engine
```

## 💻 Usage

See `src/foundation/core/main.cpp` for a complete example. The engine uses an `App` base class — just override `setup()` and `update()`:

### Quick Start

```cpp
#include "foundation/core/engine.h"

class MyApp : public App {
  Entity player;

  void setup(Engine &engine) override {
    player = engine.createModelEntity("Player", EngineConfig::MODEL_BOX,
                                      glm::vec3(0.0f), glm::vec3(0.0f), glm::vec3(1.0f));

    engine.createLightEntity("Light", glm::vec3(2, 3, 2), glm::vec3(-1, -1, -1),
                             glm::vec3(1.0f), LightType::Directional, 1.5f, 0, 0);

    engine.createCameraEntity(glm::vec3(0, 0, 5));
  }

  void update(Engine &engine, float dt) override {
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

Any plain struct works as a component — no base class required:

```cpp
struct Health { int current = 3; int max = 3; };
struct Velocity { glm::vec3 dir; float speed; };

Entity e = engine.createEntity();
engine.addComponent<Health>(e, 3, 3);
engine.addComponent<Velocity>(e, glm::vec3(1, 0, 0), 2.0f);

auto *hp = engine.tryGetComponent<Health>(e);
```

For detailed API documentation, explore the header files in the `internal/` directory.

## 🛠️ Technologies

| Category     | Technology      |
| ------------ | --------------- |
| Language     | Modern C++      |
| Graphics API | OpenGL          |
| Build System | CMake           |
| Window/Input | SDL             |
| Math         | GLM             |
| UI Framework | Dear ImGui      |
| Dependencies | See `external/` |

## 🎯 Future Enhancements

This engine is actively developed with continuous improvements planned. Check the [Issues](https://github.com/phmuni/modular-engine/issues) and [Projects](https://github.com/phmuni/modular-engine/projects) pages for current development goals and planned features.

## 🤝 Contributing

Contributions are welcome! Please follow these steps:

1. Fork the repository
2. Create a feature branch (`git checkout -b feature/amazing-feature`)
3. Commit your changes (`git commit -m 'Add amazing feature'`)
4. Push to the branch (`git push origin feature/amazing-feature`)
5. Open a Pull Request

## 📄 License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

## 👤 Author

**[phmuni](https://github.com/phmuni)**

## 📞 Support

For issues and questions, please open an [issue](https://github.com/phmuni/modular-engine/issues) on GitHub.
