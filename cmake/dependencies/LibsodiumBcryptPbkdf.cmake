# =============================================================================
# libsodium-bcrypt-pbkdf Configuration Module
# =============================================================================
# This module handles libsodium-bcrypt-pbkdf dependency patching and setup.
#
# Note: Windows sys/param.h fix is now in the forked submodule
#       (zfogg/libsodium-bcrypt-pbkdf, fix-windows-sys-param branch)
#
# Prerequisites:
#   - cmake/utils/Patching.cmake must be included
#
# Outputs:
#   - Applies libsodium-bcrypt-pbkdf-0-fix-ubsan.patch if needed
# =============================================================================

include(${CMAKE_SOURCE_DIR}/cmake/utils/Patching.cmake)

function(configure_libsodium_bcrypt_pbkdf)
    set(DEP_DIR "${CMAKE_SOURCE_DIR}/deps/libsodium-bcrypt-pbkdf")
    set(PATCHES_DIR "${CMAKE_SOURCE_DIR}/cmake/dependencies/patches")

    # Apply patch #0: UBSAN fix (all platforms)
    apply_patch(
        TARGET_DIR "${DEP_DIR}"
        PATCH_FILE "${PATCHES_DIR}/libsodium-bcrypt-pbkdf-0-fix-ubsan.patch"
        PATCH_NUM 0
        DESCRIPTION "Fix undefined behavior sanitizer warnings"
        ASSUME_UNCHANGED
            src/openbsd-compat/blowfish.c
            src/openbsd-compat/bcrypt_pbkdf.c
    )

    # Verify the source files exist
    if(NOT EXISTS "${DEP_DIR}/src/openbsd-compat/blowfish.c")
        message(FATAL_ERROR "libsodium-bcrypt-pbkdf source file not found")
    endif()
endfunction()
