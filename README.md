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
- **OpenGL Rendering** - Modern graphics API integration with shader support
- **Asset Loading** - Support for multiple asset formats and automated resource management
- **Camera System** - Flexible camera control and projection management
- **Input System** - Keyboard and mouse input handling
- **Lighting System** - Configurable lighting with support for multiple light sources
- **UI Integration** - ImGui-based user interface framework
- **Scene Management** - Entity and scene lifecycle management

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
│   ├── core/              # Engine core
│   ├── ecs/               # Entity-Component-System managers
│   ├── loader/            # Asset loading systems
│   ├── model/             # Data structures
│   └── system/            # Game logic systems
├── internal/              # Header files and interfaces
│   ├── component/         # Component definitions
│   ├── core/              # Core interfaces
│   ├── ecs/               # ECS interfaces
│   ├── loader/            # Loader interfaces
│   ├── model/             # Model definitions
│   └── system/            # System interfaces
├── external/              # Third-party dependencies
├── assets/                # Game assets (models, shaders, textures)
├── bin/                   # Compiled binaries
├── build/                 # CMake build directory
└── CMakeLists.txt         # Build configuration
```

## 🔨 Building

### Generate Build Files

```bash
cmake -S . -B build -G "Ninja"
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

See the `src/core/main.cpp` file for a complete example of engine initialization and usage. The engine can be extended by creating new components and systems following the ECS pattern.

### Quick Start

1. Initialize the engine
2. Create entities and attach components
3. Run the main loop to update systems and render

For detailed API documentation and examples, explore the header files in the `internal/` directory.

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
