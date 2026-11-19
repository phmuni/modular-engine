cmake_minimum_required(VERSION 3.14)
project(modular-engine C CXX)

set(CMAKE_WARN_DEPRECATED OFF CACHE BOOL "" FORCE)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- Build Type ---
# Release by default; pass -DCMAKE_BUILD_TYPE=Debug for debugging
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
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
include(cmake/CPM.cmake)

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
# Audio: only the sources are used; miniaudio.c is compiled into the engine below
CPMAddPackage(
  NAME miniaudio
  GITHUB_REPOSITORY mackron/miniaudio
  GIT_TAG 0.11.26
  DOWNLOAD_ONLY YES
)

# --- Directories ---
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin")
foreach(OUTPUT_CONFIG Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${OUTPUT_CONFIG}" OUTPUT_CONFIG_UPPER)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${OUTPUT_CONFIG_UPPER} "${CMAKE_SOURCE_DIR}/bin")
endforeach()
set(EXTERNAL_DIR "${CMAKE_SOURCE_DIR}/external")

# --- Source files ---
file(GLOB_RECURSE SOURCE_FILES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/*.cpp")
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

# The file parsers (tinyobj, tinygltf, stb_image, stb_dxt) and the texture conversion loops are optimized in every
# build type: unoptimized, they take seconds on a large model. MSVC is left alone, since its Debug runtime checks
# (/RTC1) refuse to combine with /O2.
set_source_files_properties(
  "${CMAKE_SOURCE_DIR}/src/rendering/resources/thirdPartyImpl.cpp"
  "${CMAKE_SOURCE_DIR}/src/rendering/resources/textureProcessing.cpp"
  PROPERTIES
  COMPILE_OPTIONS "$<$<AND:$<CONFIG:Debug>,$<NOT:$<CXX_COMPILER_ID:MSVC>>>:-O2>")

# --- Executable ---
add_executable(engine ${SOURCE_FILES} ${GLAD_SOURCE} ${IMGUI_SOURCE} ${MINIAUDIO_SOURCE} ${RMLUI_BACKEND_SOURCE}
               ${IMGUIZMO_SOURCE})

# --- Include directories ---
target_include_directories(engine PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${EXTERNAL_DIR}
)
target_include_directories(engine SYSTEM PRIVATE ${miniaudio_SOURCE_DIR} ${RmlUi_SOURCE_DIR}/Backends
                           ${ImGuizmo_SOURCE_DIR}/src ${EXTERNAL_DIR}/imgui)
target_compile_definitions(engine PRIVATE RMLUI_SDL_VERSION_MAJOR=3 "RMLUI_GL3_CUSTOM_LOADER=<glad/glad.h>")

# --- Link libraries ---
target_link_libraries(engine
    SDL3::SDL3
    glm::glm
    nlohmann_json::nlohmann_json
    RmlUi::RmlUi
)

if(MSVC)
  target_compile_definitions(engine PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

# --- OpenGL por plataforma ---
if(WIN32)
    target_link_libraries(engine opengl32)
elseif(UNIX AND NOT APPLE)
    find_package(OpenGL REQUIRED)
    find_package(Threads REQUIRED)
    # miniaudio needs threads and libm, and loads the audio backends (ALSA, PulseAudio...) at runtime
    target_link_libraries(engine OpenGL::GL Threads::Threads m ${CMAKE_DL_LIBS})
elseif(APPLE)
    find_library(OPENGL_LIBRARY OpenGL REQUIRED)
    target_link_libraries(engine ${OPENGL_LIBRARY})
endif()

# --- Post-build ---
add_custom_command(TARGET engine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:SDL3::SDL3>"
        "$<TARGET_FILE_DIR:engine>"
    COMMAND ${CMAKE_COMMAND} -E echo "Build completed! Executable: ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/engine${CMAKE_EXECUTABLE_SUFFIX}"
)