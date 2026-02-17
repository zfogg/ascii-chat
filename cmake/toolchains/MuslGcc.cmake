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

# Only search within the sysroot for headers and libraries
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
