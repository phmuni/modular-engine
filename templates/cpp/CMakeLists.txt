cmake_minimum_required(VERSION 3.14)
project(MyGame C CXX)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the type of build." FORCE)
endif()

# The engine: a modular-engine checkout (MODULAR_ENGINE_DIR), or else downloaded from GitHub.
# Its dependencies (SDL, RmlUi...) download on the first configure; set the CPM_SOURCE_CACHE environment variable to a
# folder to share those downloads between projects.
set(MODULAR_ENGINE_DIR "@ENGINE_DIR@" CACHE PATH "Path to a modular-engine checkout (empty: download it)")
if(MODULAR_ENGINE_DIR AND EXISTS "${MODULAR_ENGINE_DIR}/CMakeLists.txt")
  add_subdirectory("${MODULAR_ENGINE_DIR}" modular-engine)
else()
  include(FetchContent)
  FetchContent_Declare(modular_engine GIT_REPOSITORY https://github.com/phmuni/modular-engine.git GIT_TAG main)
  FetchContent_MakeAvailable(modular_engine)
endif()

# The game: src/*.cpp, built into bin/ next to assets/. bin/ and assets/ together are the game to ship.
file(GLOB_RECURSE GAME_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
modular_engine_add_game(MyGame OUTPUT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/bin" SOURCES ${GAME_SOURCES})
