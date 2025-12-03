# =============================================================================
# Tomlc17 Configuration Module
# =============================================================================
# This module handles tomlc17 dependency patching and setup.
#
# Prerequisites:
#   - cmake/utils/Patching.cmake must be included
#
# Outputs:
#   - Applies tomlc17-fix-align8-overflow.patch if needed
#   - Applies tomlc17-fix-windows-deprecation.patch if needed
# =============================================================================

include(${CMAKE_SOURCE_DIR}/cmake/utils/Patching.cmake)

function(configure_tomlc17)
    set(DEP_DIR "${CMAKE_SOURCE_DIR}/deps/tomlc17")
    set(PATCHES_DIR "${CMAKE_SOURCE_DIR}/cmake/dependencies/patches")

    # Apply patch #0: align8 overflow fix (all platforms)
    apply_patch(
        TARGET_DIR "${DEP_DIR}"
        PATCH_FILE "${PATCHES_DIR}/tomlc17-fix-align8-overflow.patch"
        PATCH_NUM 0
        DESCRIPTION "Fix align8 integer overflow"
        ASSUME_UNCHANGED
            src/tomlc17.c
    )

    # Apply patch #1: Windows deprecation fix (Windows only)
    apply_patch(
        TARGET_DIR "${DEP_DIR}"
        PATCH_FILE "${PATCHES_DIR}/tomlc17-fix-windows-deprecation.patch"
        PATCH_NUM 1
        DESCRIPTION "Fix Windows deprecation warnings"
        PLATFORM WIN32
        ASSUME_UNCHANGED
            src/tomlc17.c
    )

    # Verify the source file exists
    if(NOT EXISTS "${DEP_DIR}/src/tomlc17.c")
        message(FATAL_ERROR "tomlc17 source file not found: ${DEP_DIR}/src/tomlc17.c")
    endif()
endfunction()
