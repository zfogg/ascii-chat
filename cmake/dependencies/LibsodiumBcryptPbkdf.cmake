# =============================================================================
# libsodium-bcrypt-pbkdf Configuration Module
# =============================================================================
# This module handles libsodium-bcrypt-pbkdf dependency patching and setup.
#
# Prerequisites:
#   - None (runs early in build process)
#
# Outputs:
#   - Applies libsodium-bcrypt-pbkdf-fix-ubsan.patch if needed
#   - Applies Windows compatibility patch for sys/param.h
#   - Sets up libsodium-bcrypt-pbkdf source files for compilation
# =============================================================================

function(configure_libsodium_bcrypt_pbkdf)
    set(LIBSODIUM_BCRYPT_PBKDF_DIR "${CMAKE_SOURCE_DIR}/deps/libsodium-bcrypt-pbkdf")
    set(LIBSODIUM_BCRYPT_PBKDF_PATCH "${CMAKE_SOURCE_DIR}/cmake/dependencies/patches/libsodium-bcrypt-pbkdf-fix-ubsan.patch")
    set(LIBSODIUM_BCRYPT_PBKDF_PATCH_WINDOWS "${CMAKE_SOURCE_DIR}/cmake/dependencies/patches/libsodium-bcrypt-pbkdf-windows.patch")
    set(LIBSODIUM_BCRYPT_PBKDF_SOURCE "${LIBSODIUM_BCRYPT_PBKDF_DIR}/src/openbsd-compat/blowfish.c")
    set(PATCH_MARKER "${ASCIICHAT_DEPS_CACHE_ROOT}/libsodium-bcrypt-pbkdf.patch_applied")

    # Check if patch needs to be applied
    if(NOT EXISTS "${PATCH_MARKER}")
        # Apply Windows patch first (sys/param.h fix) - only on Windows
        if(WIN32)
            message(STATUS "Applying libsodium-bcrypt-pbkdf Windows patch (sys/param.h fix)...")
            execute_process(
                COMMAND git apply --check "${LIBSODIUM_BCRYPT_PBKDF_PATCH_WINDOWS}"
                WORKING_DIRECTORY "${LIBSODIUM_BCRYPT_PBKDF_DIR}"
                RESULT_VARIABLE PATCH_WIN_CHECK_RESULT
                OUTPUT_QUIET
                ERROR_QUIET
            )

            if(PATCH_WIN_CHECK_RESULT EQUAL 0)
                execute_process(
                    COMMAND git apply "${LIBSODIUM_BCRYPT_PBKDF_PATCH_WINDOWS}"
                    WORKING_DIRECTORY "${LIBSODIUM_BCRYPT_PBKDF_DIR}"
                    RESULT_VARIABLE PATCH_WIN_RESULT
                    ERROR_VARIABLE PATCH_WIN_ERROR
                )
                if(PATCH_WIN_RESULT EQUAL 0)
                    message(STATUS "libsodium-bcrypt-pbkdf Windows patch applied successfully")
                else()
                    message(WARNING "Failed to apply libsodium-bcrypt-pbkdf Windows patch: ${PATCH_WIN_ERROR}")
                endif()
            else()
                message(STATUS "libsodium-bcrypt-pbkdf Windows patch already applied or not needed")
            endif()
        endif()

        message(STATUS "Applying libsodium-bcrypt-pbkdf fix-ubsan patch...")

        # Try to apply the patch
        execute_process(
            COMMAND git apply --check "${LIBSODIUM_BCRYPT_PBKDF_PATCH}"
            WORKING_DIRECTORY "${LIBSODIUM_BCRYPT_PBKDF_DIR}"
            RESULT_VARIABLE PATCH_CHECK_RESULT
            OUTPUT_QUIET
            ERROR_QUIET
        )

        if(PATCH_CHECK_RESULT EQUAL 0)
            # Patch can be applied cleanly
            execute_process(
                COMMAND git apply "${LIBSODIUM_BCRYPT_PBKDF_PATCH}"
                WORKING_DIRECTORY "${LIBSODIUM_BCRYPT_PBKDF_DIR}"
                RESULT_VARIABLE PATCH_RESULT
                OUTPUT_VARIABLE PATCH_OUTPUT
                ERROR_VARIABLE PATCH_ERROR
            )

            if(PATCH_RESULT EQUAL 0)
                # Create marker file to indicate patch was applied
                file(WRITE "${PATCH_MARKER}" "Patch applied at ${CMAKE_CURRENT_LIST_FILE}\n")

                # Tell git to ignore the patched file changes
                execute_process(
                    COMMAND git update-index --assume-unchanged src/openbsd-compat/blowfish.c
                    WORKING_DIRECTORY "${LIBSODIUM_BCRYPT_PBKDF_DIR}"
                    OUTPUT_QUIET
                    ERROR_QUIET
                )
                execute_process(
                    COMMAND git update-index --assume-unchanged src/openbsd-compat/bcrypt_pbkdf.c
                    WORKING_DIRECTORY "${LIBSODIUM_BCRYPT_PBKDF_DIR}"
                    OUTPUT_QUIET
                    ERROR_QUIET
                )

                message(STATUS "libsodium-bcrypt-pbkdf patch applied successfully")
            else()
                message(WARNING "Failed to apply libsodium-bcrypt-pbkdf patch: ${PATCH_ERROR}")
            endif()
        else()
            # Patch already applied or source differs
            message(STATUS "libsodium-bcrypt-pbkdf patch already applied or not needed")
            file(WRITE "${PATCH_MARKER}" "Patch skipped (already applied or not needed)\n")

            # Still set assume-unchanged to hide the modifications
            execute_process(
                COMMAND git update-index --assume-unchanged src/openbsd-compat/blowfish.c
                WORKING_DIRECTORY "${LIBSODIUM_BCRYPT_PBKDF_DIR}"
                OUTPUT_QUIET
                ERROR_QUIET
            )
            execute_process(
                COMMAND git update-index --assume-unchanged src/openbsd-compat/bcrypt_pbkdf.c
                WORKING_DIRECTORY "${LIBSODIUM_BCRYPT_PBKDF_DIR}"
                OUTPUT_QUIET
                ERROR_QUIET
            )
        endif()
    else()
        # Patch marker exists, but ensure assume-unchanged is still set
        # (in case the user ran 'git update-index --no-assume-unchanged')
        execute_process(
            COMMAND git update-index --assume-unchanged src/openbsd-compat/blowfish.c
            WORKING_DIRECTORY "${LIBSODIUM_BCRYPT_PBKDF_DIR}"
            OUTPUT_QUIET
            ERROR_QUIET
        )
        execute_process(
            COMMAND git update-index --assume-unchanged src/openbsd-compat/bcrypt_pbkdf.c
            WORKING_DIRECTORY "${LIBSODIUM_BCRYPT_PBKDF_DIR}"
            OUTPUT_QUIET
            ERROR_QUIET
        )
    endif()

    # Verify the source files exist
    if(NOT EXISTS "${LIBSODIUM_BCRYPT_PBKDF_SOURCE}")
        message(FATAL_ERROR "libsodium-bcrypt-pbkdf source file not found: ${LIBSODIUM_BCRYPT_PBKDF_SOURCE}")
    endif()
endfunction()
