# =============================================================================
# MuslGcc.cmake - CMake toolchain file for musl-gcc cross-compilation
# =============================================================================
# Used by ExternalProject_Add() when building CMake-based dependencies
# (like libwebsockets) under musl-gcc. Prevents CMake from injecting
# glibc system include paths (/usr/include) that conflict with musl headers.
#
# musl-gcc's specs file already provides:
#   -nostdinc -isystem /usr/lib/musl/include
# but CMake's compiler introspection detects /usr/include as a system
# directory and adds it back via -isystem, causing header conflicts.
# =============================================================================

set(CMAKE_SYSTEM_NAME Linux)

# Use musl-gcc (set by parent via MUSL_GCC_PATH cache variable)
set(CMAKE_C_COMPILER "${MUSL_GCC_PATH}")

# Tell CMake to use musl's sysroot for header/library searches
set(CMAKE_SYSROOT /usr/lib/musl)
set(CMAKE_FIND_ROOT_PATH /usr/lib/musl)

# Only search within the sysroot for headers and libraries
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)

# Prevent CMake from searching in system directories that conflict with musl
set(CMAKE_IGNORE_PATH "/usr/include" "/usr/lib" "/usr/lib64" "/usr/local/include" "/usr/local/lib")
set(CMAKE_SYSTEM_IGNORE_PATH "/usr/include" "/usr/lib" "/usr/lib64" "/usr/local/include" "/usr/local/lib")

# Prevent CMake from adding system include paths discovered during compiler detection
# These paths are captured during compiler introspection and added automatically
set(CMAKE_C_IMPLICIT_INCLUDE_DIRECTORIES "")
set(CMAKE_C_IMPLICIT_LINK_DIRECTORIES "")

# Force musl headers and prevent glibc header contamination across all configurations
# Initialize CMAKE_C_FLAGS for all build types
set(CMAKE_C_FLAGS_INIT "-nostdinc -isystem /usr/lib/musl/include")

# Add optimization flags for Release builds (freetype uses Release build)
set(CMAKE_C_FLAGS_RELEASE_INIT "-O2 -fPIC -nostdinc -isystem /usr/lib/musl/include")

# Disable system paths in imported target handling
set(CMAKE_NO_SYSTEM_FROM_IMPORTED TRUE)

# Additional safety: explicitly set these after CMake compiler detection
# This overrides implicit include directories that CMake added
set(CMAKE_C_IMPLICIT_INCLUDE_DIRECTORIES "" FORCE)
set(CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES "" FORCE)
