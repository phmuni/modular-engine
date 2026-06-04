cmake_minimum_required(VERSION 3.10)
project(modular-engine C CXX)

set(CMAKE_WARN_DEPRECATED OFF CACHE BOOL "" FORCE)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- Build Type ---
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Choose the type of build." FORCE)
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

# --- Directories ---
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin")
foreach(OUTPUT_CONFIG Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${OUTPUT_CONFIG}" OUTPUT_CONFIG_UPPER)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${OUTPUT_CONFIG_UPPER} "${CMAKE_SOURCE_DIR}/bin")
endforeach()
set(EXTERNAL_DIR "${CMAKE_SOURCE_DIR}/external")

# --- Source files ---
file(GLOB_RECURSE SOURCE_FILES "${CMAKE_SOURCE_DIR}/src/*.cpp")
file(GLOB GLAD_SOURCE "${EXTERNAL_DIR}/glad/*.cpp")
file(GLOB IMGUI_SOURCE "${EXTERNAL_DIR}/imgui/*.cpp")

# --- Executable ---
add_executable(engine ${SOURCE_FILES} ${GLAD_SOURCE} ${IMGUI_SOURCE})

# --- Include directories ---
target_include_directories(engine PRIVATE
    ${CMAKE_SOURCE_DIR}/internal
    ${EXTERNAL_DIR}
)

# --- Link libraries ---
target_link_libraries(engine
    SDL3::SDL3
    glm::glm
    nlohmann_json::nlohmann_json
)

# Suppress MSVC deprecation warnings for legacy C functions in third-party headers
if(MSVC)
  target_compile_definitions(engine PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

# --- OpenGL por plataforma ---
if(WIN32)
    target_link_libraries(engine opengl32)
elseif(UNIX AND NOT APPLE)
    find_package(OpenGL REQUIRED)
    target_link_libraries(engine OpenGL::GL ${CMAKE_DL_LIBS})
elseif(APPLE)
    find_library(OPENGL_LIBRARY OpenGL REQUIRED)
    target_link_libraries(engine ${OPENGL_LIBRARY})
endif()

# --- Post-build ---
add_custom_command(TARGET engine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E echo "Build completed! Executable: ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/engine${CMAKE_EXECUTABLE_SUFFIX}"
)