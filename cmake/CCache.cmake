# =============================================================================
# CCache.cmake - Compiler cache configuration for faster rebuilds
# =============================================================================
# Finds and configures ccache (compiler cache) if available, which caches
# compilation results to significantly speed up rebuild times.
#
# Configuration:
# - USE_CCACHE: Option to enable/disable ccache (default: ON)
# - Automatically skips if compiler launcher is already set (e.g., by Homebrew LLVM)
# - Configures ccache with optimal settings (2GB cache, compression level 6)
#
# Must be included AFTER project() to configure compiler launcher.
# =============================================================================

# Find and enable ccache if available (unless explicitly disabled)
option(USE_CCACHE "Use ccache for compilation if available" ON)

if(USE_CCACHE)
    find_program(CCACHE_PROGRAM ccache)
    if(CCACHE_PROGRAM)
        # Only set ccache if not already configured (e.g., by Homebrew LLVM setup)
        if(NOT CMAKE_C_COMPILER_LAUNCHER)
            set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE_PROGRAM}" CACHE STRING "C compiler launcher" FORCE)
            message(STATUS "${BoldGreen}Using ccache: ${CCACHE_PROGRAM}${ColorReset}")

            # Configure ccache for optimal performance
            execute_process(COMMAND ${CCACHE_PROGRAM} --set-config max_size=2G)
            execute_process(COMMAND ${CCACHE_PROGRAM} --set-config compression=true)
            execute_process(COMMAND ${CCACHE_PROGRAM} --set-config compression_level=6)
        else()
            message(STATUS "Compiler launcher already set: ${CMAKE_C_COMPILER_LAUNCHER}")
        endif()
    else()
        message(STATUS "ccache not found - install with: sudo pacman -S ccache")
    endif()
endif()
