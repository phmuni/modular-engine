cmake_minimum_required(VERSION 3.10)

# Project name
project(modular-engine)
set(CMAKE_CXX_STANDARD 17)

# --- Build Type ---
# Allow user to specify build type via command line: cmake -DCMAKE_BUILD_TYPE=Release ..
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Choose the type of build." FORCE)
endif()
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")

# Set optimization flags for Release builds
if(CMAKE_BUILD_TYPE STREQUAL Release)
  set(CMAKE_CXX_FLAGS_RELEASE "-O3")
endif()

# --- Directories ---
# Output directory for binaries
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin")
# Directory where external libraries are located
set(EXTERNAL_DIR "${CMAKE_SOURCE_DIR}/external")

# Include directories for project and external dependencies
include_directories(
    ${CMAKE_SOURCE_DIR}/internal
    ${EXTERNAL_DIR}
)
# --- Source files ---
# Recursively find .cpp files only in src folder (external libraries linked separately)
file(GLOB_RECURSE SOURCE_FILES "${CMAKE_SOURCE_DIR}/src/**/*.cpp")
# Add only essential external implementations (GLAD is required for OpenGL initialization)
file(GLOB GLAD_SOURCE "${CMAKE_SOURCE_DIR}/external/glad/*.cpp")
file(GLOB IMGUI_SOURCE "${CMAKE_SOURCE_DIR}/external/imgui/*.cpp")
add_executable(engine ${SOURCE_FILES} ${GLAD_SOURCE} ${IMGUI_SOURCE})

# --- Libraries ---
# Link external libraries manually
target_link_libraries(engine
    ${EXTERNAL_DIR}/SDL3/lib/libSDL3.dll.a
    # link other necessary libraries
)

# --- Copy DLLs ---
# Copy all DLLs from external dependencies to the bin folder after build
file(GLOB DLL_FILES "${EXTERNAL_DIR}/*/bin/*.dll")
foreach(DLL ${DLL_FILES})
    add_custom_command(TARGET engine POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${DLL}" "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/"
    )
endforeach()
