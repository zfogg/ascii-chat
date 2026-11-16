# =============================================================================
# CCache.cmake - Compiler cache configuration for faster rebuilds
# =============================================================================
# Finds and configures ccache (compiler cache) if available, which caches
# compilation results to significantly speed up rebuild times.
#
# Configuration:
# - USE_CCACHE: Option to enable/disable ccache (default: ON)
# - Preserves custom launchers and configures existing ccache launchers
# - Uses a 12GB cache and fast compression without changing global configuration
#
# Must be included AFTER project() to configure compiler launcher.
# =============================================================================

# Find and enable ccache if available (unless explicitly disabled)
option(USE_CCACHE "Use ccache for compilation if available" ON)

if(USE_CCACHE)
    # Use centralized ASCIICHAT_CCACHE_EXECUTABLE from FindPrograms.cmake
    if(ASCIICHAT_CCACHE_EXECUTABLE)
        # Only set ccache if not already configured (e.g., by Homebrew LLVM setup)
        if(NOT CMAKE_C_COMPILER_LAUNCHER)
            set(CMAKE_C_COMPILER_LAUNCHER "${ASCIICHAT_CCACHE_EXECUTABLE}" CACHE STRING "C compiler launcher" FORCE)
            message(STATUS "${BoldGreen}Using ccache: ${ASCIICHAT_CCACHE_EXECUTABLE}${ColorReset}")
        else()
            message(STATUS "Compiler launcher already set: ${BoldCyan}${CMAKE_C_COMPILER_LAUNCHER}${ColorReset}")
        endif()
    else()
        if(DEFINED CMAKE_C_COMPILER_LAUNCHER AND CMAKE_C_COMPILER_LAUNCHER MATCHES "ccache")
            message(STATUS "${BoldYellow}ccache${ColorReset} not found; disabling compiler launcher.")
            unset(CMAKE_C_COMPILER_LAUNCHER CACHE)
        else()
            message(STATUS "${BoldYellow}ccache${ColorReset} not found - install with: ${Cyan}sudo ${BoldBlue}pacman${ColorReset} -S ${BoldBlue}ccache${ColorReset}")
        endif()
    endif()
endif()

# Emscripten runs a Python driver (a batch entry point on Windows). Wrap its
# underlying Clang invocation so ccache sees a native compiler executable.
if(USE_CCACHE AND EMSCRIPTEN AND ASCIICHAT_CCACHE_EXECUTABLE)
    set(_asciichat_wasm_cache_launcher
        "${CMAKE_COMMAND}" -E env
        "CCACHE_MAXSIZE=12G" "CCACHE_COMPRESS=true" "CCACHE_COMPRESSLEVEL=1"
        "EM_COMPILER_WRAPPER=${ASCIICHAT_CCACHE_EXECUTABLE}")
    foreach(_language C CXX)
        if(NOT CMAKE_${_language}_COMPILER_LAUNCHER OR
           CMAKE_${_language}_COMPILER_LAUNCHER MATCHES "ccache")
            set(CMAKE_${_language}_COMPILER_LAUNCHER "${_asciichat_wasm_cache_launcher}"
                CACHE STRING "${_language} compiler launcher" FORCE)
        endif()
    endforeach()
endif()

# LLVM setup may already have selected ccache before this module runs.
# Apply project settings to existing launchers as well as newly selected ones.
if(USE_CCACHE AND NOT EMSCRIPTEN AND CMAKE_C_COMPILER_LAUNCHER MATCHES "ccache")
    execute_process(COMMAND "${ASCIICHAT_CCACHE_EXECUTABLE}" --help
        OUTPUT_VARIABLE _asciichat_ccache_help ERROR_QUIET)
    if(_asciichat_ccache_help MATCHES "KEY=VALUE")
        # Native configuration arguments avoid an extra CMake process per file.
        set(_asciichat_ccache_launcher "${ASCIICHAT_CCACHE_EXECUTABLE}"
            "max_size=12G" "compression=true" "compression_level=1")
        set(ASCIICHAT_CCACHE_KEY_VALUE_SUPPORTED TRUE)
    else()
        set(_asciichat_ccache_launcher
            "${CMAKE_COMMAND}" -E env
            "CCACHE_MAXSIZE=12G" "CCACHE_COMPRESS=true" "CCACHE_COMPRESSLEVEL=1"
            "${ASCIICHAT_CCACHE_EXECUTABLE}")
        set(ASCIICHAT_CCACHE_KEY_VALUE_SUPPORTED FALSE)
    endif()
    set(CMAKE_C_COMPILER_LAUNCHER "${_asciichat_ccache_launcher}" CACHE STRING "C compiler launcher" FORCE)
    if(NOT CMAKE_CXX_COMPILER_LAUNCHER OR CMAKE_CXX_COMPILER_LAUNCHER MATCHES "ccache")
        set(CMAKE_CXX_COMPILER_LAUNCHER "${_asciichat_ccache_launcher}" CACHE STRING "C++ compiler launcher" FORCE)
    endif()
endif()
