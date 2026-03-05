# =============================================================================
# pkg-config Environment Setup
# =============================================================================
# This module sets up pkg-config environment variables globally so that
# nested processes (like Zig builds) can find system libraries.
#
# These variables are set early so they're available to all dependencies,
# not just in the execute_process() call for Ghostty.

# Determine the system's pkg-config directories
# Try to find pkg-config first to get the default search path
find_program(PKG_CONFIG_PROGRAM pkg-config REQUIRED)

# Build comprehensive pkg-config search paths
set(PKG_CONFIG_DIRS "")

# Standard system paths (highest priority - checked first)
list(APPEND PKG_CONFIG_DIRS "/usr/lib/pkgconfig")
list(APPEND PKG_CONFIG_DIRS "/usr/share/pkgconfig")
list(APPEND PKG_CONFIG_DIRS "/usr/local/lib/pkgconfig")

# Linuxbrew paths (if they exist)
if(EXISTS "/home/linuxbrew/.linuxbrew")
    list(APPEND PKG_CONFIG_DIRS "/home/linuxbrew/.linuxbrew/lib/pkgconfig")
    list(APPEND PKG_CONFIG_DIRS "/home/linuxbrew/.linuxbrew/share/pkgconfig")
endif()

# Homebrew paths on macOS
if(APPLE)
    # ARM Mac
    if(EXISTS "/opt/homebrew/opt")
        list(APPEND PKG_CONFIG_DIRS "/opt/homebrew/lib/pkgconfig")
    endif()
    # Intel Mac
    if(EXISTS "/usr/local/opt")
        list(APPEND PKG_CONFIG_DIRS "/usr/local/lib/pkgconfig")
    endif()
endif()

# Additional system paths that might exist
list(APPEND PKG_CONFIG_DIRS "/opt/local/lib/pkgconfig")
list(APPEND PKG_CONFIG_DIRS "${CMAKE_INSTALL_PREFIX}/lib/pkgconfig")

# Join all paths with colons
string(REPLACE ";" ":" PKG_CONFIG_PATH_STRING "${PKG_CONFIG_DIRS}")

# Set environment variables globally so nested processes inherit them
# This is the key: we use set(ENV{...}) to modify the CMake process's environment
# which is then inherited by all child processes (including Zig builds)
set(ENV{PKG_CONFIG} "${PKG_CONFIG_PROGRAM}")
set(ENV{PKG_CONFIG_PATH} "${PKG_CONFIG_PATH_STRING}")

message(STATUS "pkg-config setup:")
message(STATUS "  PKG_CONFIG: ${PKG_CONFIG_PROGRAM}")
message(STATUS "  PKG_CONFIG_PATH: ${PKG_CONFIG_PATH_STRING}")
