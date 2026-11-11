# =============================================================================
# mdns Configuration Module
# =============================================================================
# This module handles mdns build integration and compatibility patches.
#
# The mdns library (https://github.com/mjansson/mdns) includes a main() function
# for standalone testing which causes 5-second delays in our test suite due to
# DNS-SD discovery. We patch it to wrap main() in #ifndef MDNS_NO_MAIN.
#
# Prerequisites:
#   - cmake/utils/Patching.cmake must be included
#
# Outputs:
#   - Applies mdns-0-exclude-main.patch
#   - Applies mdns-1-windows-ioctl-command.patch on Windows
#   - Sets MDNS_NO_MAIN compile definition in Libraries.cmake
# =============================================================================

include(${CMAKE_SOURCE_DIR}/cmake/utils/Patching.cmake)

function(configure_mdns)
    set(DEP_DIR "${CMAKE_SOURCE_DIR}/deps/ascii-chat-deps/mdns")
    set(PATCHES_DIR "${CMAKE_SOURCE_DIR}/cmake/deps/patches")

    include(${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../utils/SourceDependency.cmake)
    asciichat_dependency_defaults()
    set(MDNS_BUILD_EXAMPLE OFF)
    # Upstream's minimum CMake version predates the current deprecation policy.
    set(CMAKE_POLICY_VERSION_MINIMUM 3.10)
    add_subdirectory("${DEP_DIR}" "${CMAKE_BINARY_DIR}/_deps/mdns" EXCLUDE_FROM_ALL)

    # Apply patch #0: Exclude main() function
    apply_patch(
        TARGET_DIR "${DEP_DIR}"
        PATCH_FILE "${PATCHES_DIR}/mdns-0-exclude-main.patch"
        PATCH_NUM 0
        DESCRIPTION "Wrap main() in #ifndef MDNS_NO_MAIN to exclude from library builds"
        ASSUME_UNCHANGED
            mdns.c
    )

    # Winsock accepts a signed long command, while FIONBIO is unsigned.
    apply_patch(
        TARGET_DIR "${DEP_DIR}"
        PATCH_FILE "${PATCHES_DIR}/mdns-1-windows-ioctl-command.patch"
        PATCH_NUM 1
        DESCRIPTION "Make the Windows ioctl command conversion explicit"
        PLATFORM WIN32
        ASSUME_UNCHANGED mdns.h
    )

    # Verify the source file exists
    if(NOT EXISTS "${DEP_DIR}/mdns.c")
        message(FATAL_ERROR "mdns source file not found")
    endif()
endfunction()
