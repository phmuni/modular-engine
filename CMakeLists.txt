cmake_minimum_required(VERSION 3.10)

# Project name
project(modular-engine C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- Build Type ---
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Choose the type of build." FORCE)
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
endif()
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")

# Optimization flags
if(CMAKE_BUILD_TYPE STREQUAL "Release")
  if(MSVC)
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /O2")
  else()
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3")
  endif()
endif()

# --- Directories ---
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin")
set(EXTERNAL_DIR "${CMAKE_SOURCE_DIR}/external")

# Include directories
include_directories(
    ${CMAKE_SOURCE_DIR}/internal
    ${EXTERNAL_DIR}
)

# --- Source files ---
file(GLOB_RECURSE SOURCE_FILES "${CMAKE_SOURCE_DIR}/src/*.cpp")
file(GLOB GLAD_SOURCE "${EXTERNAL_DIR}/glad/*.cpp")
file(GLOB IMGUI_SOURCE "${EXTERNAL_DIR}/imgui/*.cpp")

# Create executable
add_executable(engine ${SOURCE_FILES} ${GLAD_SOURCE} ${IMGUI_SOURCE})

# --- Platform-specific Libraries ---
if(WIN32)
    # Windows
    if(EXISTS "${EXTERNAL_DIR}/SDL3/lib/libSDL3.dll.a")
        target_link_libraries(engine "${EXTERNAL_DIR}/SDL3/lib/libSDL3.dll.a")
    elseif(EXISTS "${EXTERNAL_DIR}/SDL3/lib/SDL3.lib")
        target_link_libraries(engine "${EXTERNAL_DIR}/SDL3/lib/SDL3.lib")
    endif()
    
    # OpenGL libraries for Windows
    target_link_libraries(engine opengl32)
    
elseif(UNIX AND NOT APPLE)
    # Linux
    find_package(SDL3 QUIET)
    if(SDL3_FOUND)
        target_link_libraries(engine SDL3::SDL3)
    else()
        target_link_libraries(engine 
            ${EXTERNAL_DIR}/SDL3/lib/libSDL3.so
        )
    endif()
    
    # OpenGL libraries for Linux
    find_package(OpenGL REQUIRED)
    target_link_libraries(engine OpenGL::GL ${CMAKE_DL_LIBS})
    
elseif(APPLE)
    # macOS
    find_package(SDL3 QUIET)
    if(SDL3_FOUND)
        target_link_libraries(engine SDL3::SDL3)
    else()
        target_link_libraries(engine "${EXTERNAL_DIR}/SDL3/lib/libSDL3.dylib")

        # Copy the dylib to the output directory so the executable can find it at runtime
        add_custom_command(TARGET engine POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${EXTERNAL_DIR}/SDL3/lib/libSDL3.dylib"
                "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/libSDL3.dylib"
            COMMENT "Copying SDL3 dylib to bin/"
        )
    endif()
    
    # OpenGL framework for macOS
    find_library(OPENGL_LIBRARY OpenGL)
    target_link_libraries(engine ${OPENGL_LIBRARY})
endif()

# --- Copy DLLs (Windows only) ---
if(WIN32)
    file(GLOB DLL_FILES "${EXTERNAL_DIR}/*/bin/*.dll")
    foreach(DLL ${DLL_FILES})
        add_custom_command(TARGET engine POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${DLL}" "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/"
            COMMENT "Copying ${DLL}"
        )
    endforeach()
endif()

# --- Post-build messages ---
add_custom_command(TARGET engine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E echo "Build completed successfully!"
    COMMAND ${CMAKE_COMMAND} -E echo "Executable: ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/engine${CMAKE_EXECUTABLE_SUFFIX}"
)