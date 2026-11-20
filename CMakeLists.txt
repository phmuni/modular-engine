cmake_minimum_required(VERSION 3.14)
project(modular-engine C CXX)

# Built on its own, this also builds the examples (bin/engine). A game includes it with add_subdirectory (or
# FetchContent) and gets the modular_engine library and modular_engine_add_game(); see templates/game.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  set(MODULAR_ENGINE_TOP_LEVEL ON)
else()
  set(MODULAR_ENGINE_TOP_LEVEL OFF)
endif()
option(MODULAR_ENGINE_BUILD_EXAMPLES "Build the example apps (bin/engine)" ${MODULAR_ENGINE_TOP_LEVEL})
option(MODULAR_ENGINE_BUILD_TOOLS "Build the Lua runner (bin/runner) and the project launcher (bin/launcher)"
       ${MODULAR_ENGINE_TOP_LEVEL})
option(MODULAR_ENGINE_BUILD_TESTS "Build bin/engine_tests, the behavior tests of the game API (see tests/ and tools/run_tests.py)" OFF)
set(ENGINE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
# Where this checkout's own programs (bin/engine, bin/runner, bin/launcher) are built; tools/package_engine.py points it
# elsewhere so packaging does not disturb the development build
set(MODULAR_ENGINE_BIN_DIR "${ENGINE_DIR}/bin" CACHE PATH "Where the engine's own programs are built")
# Also for modular_engine_add_game, which runs in the scope of the game that calls it
set(MODULAR_ENGINE_ROOT "${ENGINE_DIR}" CACHE INTERNAL "modular-engine checkout")

set(CMAKE_WARN_DEPRECATED OFF CACHE BOOL "" FORCE)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- Build Type ---
# Release by default; pass -DCMAKE_BUILD_TYPE=Debug for debugging
if(MODULAR_ENGINE_TOP_LEVEL AND NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the type of build." FORCE)
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
endif()
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")

if(CMAKE_BUILD_TYPE STREQUAL "Release")
  if(MSVC)
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /O2")
  else()
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3")
  endif()
endif()

# --- CPM ---
include("${ENGINE_DIR}/cmake/CPM.cmake")

CPMAddPackage("gh:libsdl-org/SDL#release-3.2.14")
CPMAddPackage("gh:g-truc/glm#1.0.1")
CPMAddPackage(
  NAME nlohmann_json
  GITHUB_REPOSITORY nlohmann/json
  VERSION 3.11.3
  OPTIONS "JSON_BuildTests OFF"
)
# Game UI: RmlUi (HTML/CSS-like documents) with FreeType for fonts, both built as static libraries.
# FreeType comes first so RmlUi finds its Freetype::Freetype target instead of a system copy.
CPMAddPackage(
  NAME freetype
  GITHUB_REPOSITORY freetype/freetype
  GIT_TAG VER-2-14-3
  OPTIONS
    "BUILD_SHARED_LIBS OFF"
    "FT_DISABLE_ZLIB ON"
    "FT_DISABLE_BZIP2 ON"
    "FT_DISABLE_PNG ON"
    "FT_DISABLE_HARFBUZZ ON"
    "FT_DISABLE_BROTLI ON"
)
if(TARGET freetype AND NOT TARGET Freetype::Freetype)
  add_library(Freetype::Freetype ALIAS freetype)
endif()
CPMAddPackage(
  NAME RmlUi
  GITHUB_REPOSITORY mikke89/RmlUi
  GIT_TAG 6.3
  OPTIONS
    "BUILD_SHARED_LIBS OFF"
    "RMLUI_SAMPLES OFF"
    "BUILD_TESTING OFF"
    "RMLUI_FONT_ENGINE freetype"
)
# Editor gizmos (move/rotate/scale handles) for Dear ImGui; only ImGuizmo.cpp is compiled, against external/imgui.
# Pinned to a commit: the tagged releases predate ImGui 1.92.
CPMAddPackage(
  NAME ImGuizmo
  GITHUB_REPOSITORY CedricGuillemet/ImGuizmo
  GIT_TAG 105e4fde45e4e74aae8973d98750820ef7aca7e2
  DOWNLOAD_ONLY YES
)
# Physics: Jolt (rigid bodies, characters), built as a static library without its samples and tests
CPMAddPackage(
  NAME JoltPhysics
  GITHUB_REPOSITORY jrouwe/JoltPhysics
  GIT_TAG v5.6.0
  SOURCE_SUBDIR Build
  OPTIONS
    "TARGET_UNIT_TESTS OFF"
    "TARGET_HELLO_WORLD OFF"
    "TARGET_PERFORMANCE_TEST OFF"
    "TARGET_SAMPLES OFF"
    "TARGET_VIEWER OFF"
    "ENABLE_ALL_WARNINGS OFF"
    "INTERPROCEDURAL_OPTIMIZATION OFF"
    "USE_STATIC_MSVC_RUNTIME_LIBRARY OFF"
    "OVERRIDE_CXX_FLAGS OFF"
    "DEBUG_RENDERER_IN_DEBUG_AND_RELEASE OFF"
    "PROFILER_IN_DEBUG_AND_RELEASE OFF"
    "JPH_USE_DX12 OFF"
    "JPH_USE_VK OFF"
    "JPH_USE_MTL OFF"
)
# Optimized in every build type, like the particle update: an unoptimized physics step slows a Debug game down a lot
if(TARGET Jolt AND NOT MSVC)
  target_compile_options(Jolt PRIVATE "$<$<CONFIG:Debug>:-O2>")
endif()
# Scripting: Lua (compiled below from its sources, which have no CMake build of their own) and sol2, the header-only
# bridge between C++ and Lua
CPMAddPackage(
  NAME lua
  GITHUB_REPOSITORY lua/lua
  GIT_TAG v5.4.8
  DOWNLOAD_ONLY YES
)
CPMAddPackage(
  NAME sol2
  GITHUB_REPOSITORY ThePhD/sol2
  GIT_TAG v3.5.0
  DOWNLOAD_ONLY YES
)
# Audio: only the sources are used; miniaudio.c is compiled into the engine below
CPMAddPackage(
  NAME miniaudio
  GITHUB_REPOSITORY mackron/miniaudio
  GIT_TAG 0.11.26
  DOWNLOAD_ONLY YES
)

set(EXTERNAL_DIR "${ENGINE_DIR}/external")

# --- Source files ---
# The engine library is everything in src/; the examples (examples/) are built on top of it, like any game
file(GLOB_RECURSE SOURCE_FILES CONFIGURE_DEPENDS "${ENGINE_DIR}/src/*.cpp")
file(GLOB GLAD_SOURCE "${EXTERNAL_DIR}/glad/*.cpp")
file(GLOB IMGUI_SOURCE "${EXTERNAL_DIR}/imgui/*.cpp")
list(FILTER IMGUI_SOURCE EXCLUDE REGEX "imgui_demo\\.cpp$")
set(MINIAUDIO_SOURCE "${miniaudio_SOURCE_DIR}/miniaudio.c")
set(IMGUIZMO_SOURCE "${ImGuizmo_SOURCE_DIR}/src/ImGuizmo.cpp")
# RmlUi's reference SDL platform and OpenGL 3 renderer, built against the engine's SDL3 and glad
set(RMLUI_BACKEND_SOURCE
  "${RmlUi_SOURCE_DIR}/Backends/RmlUi_Platform_SDL.cpp"
  "${RmlUi_SOURCE_DIR}/Backends/RmlUi_Renderer_GL3.cpp"
)

# The file parsers (tinyobj, tinygltf, stb_image, stb_dxt), the texture conversion loops and the particle update are
# optimized in every build type: unoptimized, the parsers take seconds on a large model, and the particles (every glm
# operation a function call) cost the black hole 8x its frame time. MSVC is left alone, since its Debug runtime
# checks (/RTC1) refuse to combine with /O2.
set_source_files_properties(
  "${ENGINE_DIR}/src/rendering/resources/thirdPartyImpl.cpp"
  "${ENGINE_DIR}/src/rendering/resources/textureProcessing.cpp"
  "${ENGINE_DIR}/src/systems/particleSystem.cpp"
  PROPERTIES
  COMPILE_OPTIONS "$<$<AND:$<CONFIG:Debug>,$<NOT:$<CXX_COMPILER_ID:MSVC>>>:-O2>")

# --- Lua ---
# The library's sources: all but the stand-alone interpreter (lua.c), the one-file build (onelua.c) and the test
# hooks (ltests.c)
file(GLOB LUA_SOURCES "${lua_SOURCE_DIR}/*.c")
list(FILTER LUA_SOURCES EXCLUDE REGEX "/(lua|onelua|ltests|luac)\\.c$")
add_library(lua_static STATIC ${LUA_SOURCES})
target_include_directories(lua_static SYSTEM PUBLIC "${lua_SOURCE_DIR}")
if(UNIX)
  target_compile_definitions(lua_static PRIVATE LUA_USE_POSIX)
endif()
if(NOT MSVC)
  target_compile_options(lua_static PRIVATE "$<$<CONFIG:Debug>:-O2>") # scripts run at full speed in Debug too
endif()

# --- Engine library ---
add_library(modular_engine STATIC ${SOURCE_FILES} ${GLAD_SOURCE} ${IMGUI_SOURCE} ${MINIAUDIO_SOURCE}
            ${RMLUI_BACKEND_SOURCE} ${IMGUIZMO_SOURCE})

# Games include the engine's headers (src/) and its third-party ones as their own
target_include_directories(modular_engine PUBLIC
  ${ENGINE_DIR}/src
  ${EXTERNAL_DIR}
)
target_include_directories(modular_engine SYSTEM PUBLIC ${miniaudio_SOURCE_DIR} ${RmlUi_SOURCE_DIR}/Backends
                           ${sol2_SOURCE_DIR}/include
                           ${ImGuizmo_SOURCE_DIR}/src ${EXTERNAL_DIR}/imgui)
target_compile_definitions(modular_engine PRIVATE RMLUI_SDL_VERSION_MAJOR=3 "RMLUI_GL3_CUSTOM_LOADER=<glad/glad.h>")
# sol2's checks on what scripts pass in; public, since every file including sol2 must agree on them
target_compile_definitions(modular_engine PUBLIC SOL_ALL_SAFETIES_ON=1)
target_link_libraries(modular_engine PUBLIC
    SDL3::SDL3
    glm::glm
    nlohmann_json::nlohmann_json
    RmlUi::RmlUi
    Jolt
    lua_static
)
if(MSVC)
  target_compile_definitions(modular_engine PUBLIC _CRT_SECURE_NO_WARNINGS)
  # sol2's templates make object files with more sections than MSVC allows by default
  target_compile_options(modular_engine PUBLIC /bigobj)
endif()

# --- OpenGL per platform ---
if(WIN32)
    target_link_libraries(modular_engine PUBLIC opengl32)
elseif(UNIX AND NOT APPLE)
    find_package(OpenGL REQUIRED)
    find_package(Threads REQUIRED)
    # miniaudio needs threads and libm, and loads the audio backends (ALSA, PulseAudio...) at runtime
    target_link_libraries(modular_engine PUBLIC OpenGL::GL Threads::Threads m ${CMAKE_DL_LIBS})
elseif(APPLE)
    find_library(OPENGL_LIBRARY OpenGL REQUIRED)
    target_link_libraries(modular_engine PUBLIC ${OPENGL_LIBRARY})
endif()

# --- Games ---
# modular_engine_add_game(<target> OUTPUT_DIR <dir> SOURCES <files...> [ENGINE_ASSETS_IN_PLACE])
#
# An executable linked with the engine, built into OUTPUT_DIR (the game's bin/ folder, next to its assets/ folder).
# Next to it go SDL's shared library and bin/engine-assets: the shaders, fonts, pause menu and basic models the
# engine itself loads, which it looks for there when the game's own assets/ folder does not have them. A game folder
# is then complete: copy bin/ and assets/ to ship it. ENGINE_ASSETS_IN_PLACE skips that copy, for the engine's own
# examples.
# MODULAR_ENGINE_GAME_OUTPUT_DIR (a cache variable, for a game built on its own) replaces OUTPUT_DIR: the launcher's
# Export builds a Release into its own folder this way, so the game's bin/ keeps the build it was playing.
function(modular_engine_add_game target)
  cmake_parse_arguments(GAME "ENGINE_ASSETS_IN_PLACE" "OUTPUT_DIR" "SOURCES" ${ARGN})
  if(MODULAR_ENGINE_GAME_OUTPUT_DIR)
    set(GAME_OUTPUT_DIR "${MODULAR_ENGINE_GAME_OUTPUT_DIR}")
  elseif(NOT GAME_OUTPUT_DIR)
    set(GAME_OUTPUT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/bin")
  endif()
  add_executable(${target} ${GAME_SOURCES})
  target_link_libraries(${target} PRIVATE modular_engine)
  # SDL's library is looked for next to the executable too, so a game folder works wherever it is copied
  if(APPLE)
    set_target_properties(${target} PROPERTIES BUILD_RPATH "@executable_path" INSTALL_RPATH "@executable_path")
  elseif(UNIX)
    set_target_properties(${target} PROPERTIES BUILD_RPATH "$ORIGIN" INSTALL_RPATH "$ORIGIN")
  endif()
  set_target_properties(${target} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${GAME_OUTPUT_DIR}")
  foreach(config Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${config}" config_upper)
    set_target_properties(${target} PROPERTIES RUNTIME_OUTPUT_DIRECTORY_${config_upper} "${GAME_OUTPUT_DIR}")
  endforeach()

  add_custom_command(TARGET ${target} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:SDL3::SDL3>" "$<TARGET_FILE_DIR:${target}>")
  if(NOT GAME_ENGINE_ASSETS_IN_PLACE)
    set(engine_assets "$<TARGET_FILE_DIR:${target}>/engine-assets")
    # copyAssetFolder.cmake instead of "cmake -E copy_directory": it leaves out the .DS_Store files macOS scatters
    foreach(folder shaders ui/fonts ui/engine models/box models/sphere models/cylinder models/grid)
      add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} "-DFROM=${MODULAR_ENGINE_ROOT}/assets/${folder}" "-DTO=${engine_assets}/${folder}"
                -P "${MODULAR_ENGINE_ROOT}/cmake/copyAssetFolder.cmake")
    endforeach()
  endif()
  add_custom_command(TARGET ${target} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E echo "Build completed! Executable: $<TARGET_FILE:${target}>")
endfunction()

# --- Examples ---
if(MODULAR_ENGINE_BUILD_EXAMPLES)
  modular_engine_add_game(engine OUTPUT_DIR "${MODULAR_ENGINE_BIN_DIR}" SOURCES "${ENGINE_DIR}/examples/main.cpp"
                          ENGINE_ASSETS_IN_PLACE)
endif()

# --- Tools ---
# bin/runner: the prebuilt player of Lua projects, copied (with its engine-assets and SDL) into each Lua project's bin/
if(MODULAR_ENGINE_BUILD_TOOLS)
  modular_engine_add_game(runner OUTPUT_DIR "${MODULAR_ENGINE_BIN_DIR}/runner" SOURCES "${ENGINE_DIR}/runner/main.cpp")
  # bin/launcher: creates, builds and plays projects; finds the templates two folders up (no engine assets needed)
  modular_engine_add_game(launcher OUTPUT_DIR "${MODULAR_ENGINE_BIN_DIR}/launcher" SOURCES "${ENGINE_DIR}/launcher/main.cpp"
                          ENGINE_ASSETS_IN_PLACE)
endif()

# --- Tests ---
# bin/engine_tests: runs one C++ test (tests/cpp) or Lua test script (tests/lua) per start, deterministically; it needs a
# window with OpenGL 4.1 (on a machine with no GPU: xvfb and Mesa, see the README). tools/run_tests.py runs them all.
if(MODULAR_ENGINE_BUILD_TESTS)
  file(GLOB TEST_SOURCES CONFIGURE_DEPENDS "${ENGINE_DIR}/tests/cpp/*.cpp")
  modular_engine_add_game(engine_tests OUTPUT_DIR "${MODULAR_ENGINE_BIN_DIR}" SOURCES ${TEST_SOURCES}
                          ENGINE_ASSETS_IN_PLACE)
endif()
