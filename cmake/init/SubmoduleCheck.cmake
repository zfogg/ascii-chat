# =============================================================================
# Git Submodule Initialization Check
# =============================================================================
# Automatically initializes and updates git submodules if they are not cloned
# This prevents build failures when submodules are missing
#
# Prerequisites:
#   - CMAKE_SOURCE_DIR must be set (available after cmake_minimum_required)
#   - Git must be available in PATH
#
# This runs at CMake configure time (not build time) to ensure submodules
# exist before any CMake code tries to access them
# =============================================================================

# Import color definitions for terminal output
include(${CMAKE_SOURCE_DIR}/cmake/utils/Colors.cmake)

function(check_and_init_git_submodules)
    set(REQUIRED_SUBMODULES
        "deps/ascii-chat-deps/bearssl/Makefile"
        "deps/ascii-chat-deps/libsodium-bcrypt-pbkdf/Makefile"
        "deps/ascii-chat-deps/sokol/sokol_app.h"
        "deps/ascii-chat-deps/uthash/src/utarray.h"
        "deps/ascii-chat-deps/ngtcp2/CMakeLists.txt"
        "deps/ascii-chat-deps/nghttp3/CMakeLists.txt"
        "deps/ascii-chat-deps/nghttp3/lib/sfparse/sfparse.c"
    )
    set(MISSING_SUBMODULES FALSE)
    foreach(SUBMODULE ${REQUIRED_SUBMODULES})
        if(NOT EXISTS "${CMAKE_SOURCE_DIR}/${SUBMODULE}")
            set(MISSING_SUBMODULES TRUE)
        endif()
    endforeach()
    # Check if we're in a git repository (local development)
    if(NOT EXISTS "${CMAKE_SOURCE_DIR}/.git")
        # Not a git repo - must be Docker/Coolify build with pre-cloned submodules
        # Verify critical submodule files exist
        if(MISSING_SUBMODULES)
            message(FATAL_ERROR "Critical submodules not found in build context.\n"
                                "This is a Docker/Coolify build and deps/ascii-chat-deps must be included.\n"
                                "Ensure Coolify has 'Submodules' enabled and the build context includes deps/.")
        endif()
        message(STATUS "Docker/Coolify build detected, submodules already in build context")
        return()
    endif()

    # Local development: .git exists, initialize submodules if needed
    find_program(GIT_EXECUTABLE git REQUIRED)

    # Existing checkouts can have older submodules without newly added ones.
    if(MISSING_SUBMODULES)
        message(STATUS "${Yellow}Git submodules not initialized. Running: git submodule update --init --recursive${ColorReset}")

        execute_process(
            COMMAND "${GIT_EXECUTABLE}" submodule update --init --recursive
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            RESULT_VARIABLE SUBMODULE_RESULT
            OUTPUT_VARIABLE SUBMODULE_OUTPUT
            ERROR_VARIABLE SUBMODULE_ERROR
        )

        if(NOT SUBMODULE_RESULT EQUAL 0)
            message(FATAL_ERROR "Failed to initialize git submodules.\n"
                                "Command: git submodule update --init --recursive\n"
                                "Error: ${SUBMODULE_ERROR}")
        endif()

        message(STATUS "${Green}Git submodules initialized successfully${ColorReset}")
    endif()

    # Verify critical submodules exist after initialization
    foreach(SUBMODULE ${REQUIRED_SUBMODULES})
        if(NOT EXISTS "${CMAKE_SOURCE_DIR}/${SUBMODULE}")
            message(FATAL_ERROR "${BoldRed}FATAL: Required git submodule file not found: ${SUBMODULE}${ColorReset}\n"
                                "This indicates the submodule clone or update failed.\n"
                                "Try manually running: git submodule update --init --recursive")
        endif()
    endforeach()
endfunction()

# Initialize submodules early in the configuration process
check_and_init_git_submodules()
