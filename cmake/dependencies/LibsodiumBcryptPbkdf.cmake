# =============================================================================
# libsodium-bcrypt-pbkdf Configuration Module
# =============================================================================
# This module handles libsodium-bcrypt-pbkdf dependency patching and setup.
#
# Prerequisites:
#   - cmake/utils/Patching.cmake must be included
#
# Outputs:
#   - Applies libsodium-bcrypt-pbkdf-fix-ubsan.patch if needed
#   - Applies Windows compatibility patch for sys/param.h
# =============================================================================

include(${CMAKE_SOURCE_DIR}/cmake/utils/Patching.cmake)

function(configure_libsodium_bcrypt_pbkdf)
    set(DEP_DIR "${CMAKE_SOURCE_DIR}/deps/libsodium-bcrypt-pbkdf")
    set(PATCHES_DIR "${CMAKE_SOURCE_DIR}/cmake/dependencies/patches")

    # Apply patch #0: Windows sys/param.h fix (Windows only)
    apply_patch(
        TARGET_DIR "${DEP_DIR}"
        PATCH_FILE "${PATCHES_DIR}/libsodium-bcrypt-pbkdf-windows.patch"
        PATCH_NUM 0
        DESCRIPTION "Fix missing sys/param.h on Windows"
        PLATFORM WIN32
        ASSUME_UNCHANGED
            src/openbsd-compat/bcrypt_pbkdf.c
    )

    # Apply patch #1: UBSAN fix (all platforms)
    apply_patch(
        TARGET_DIR "${DEP_DIR}"
        PATCH_FILE "${PATCHES_DIR}/libsodium-bcrypt-pbkdf-fix-ubsan.patch"
        PATCH_NUM 1
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
