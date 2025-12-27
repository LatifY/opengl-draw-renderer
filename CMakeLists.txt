cmake_minimum_required(VERSION 3.10)

project(OpenGL-Drawing-C C)

# Specify the C standard
set(CMAKE_C_STANDARD 99)

# Gather all source files
file(GLOB_RECURSE SOURCES "src/*.c" "src/*.h")

# Create the executable
add_executable(OpenGL-Drawing-C ${SOURCES})

# Include directories
target_include_directories(OpenGL-Drawing-C PRIVATE src src/third_party)

# Platform-specific settings
if(WIN32)
    # Windows libraries
    target_link_libraries(OpenGL-Drawing-C PRIVATE gdi32 kernel32 user32 opengl32)
    
    # Add defines if necessary (though base_defs.h handles most)
    # target_compile_definitions(OpenGL-Drawing-C PRIVATE _CRT_SECURE_NO_WARNINGS) # Common for MSVC
elseif(UNIX AND NOT APPLE)
    # Linux libraries
    target_link_libraries(OpenGL-Drawing-C PRIVATE m X11 GL GLX)
endif()

# Debug definitions
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_definitions(OpenGL-Drawing-C PRIVATE DEBUG)
else()
    target_compile_definitions(OpenGL-Drawing-C PRIVATE NDEBUG)
endif()
