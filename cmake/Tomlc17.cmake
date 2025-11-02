# =============================================================================
# Tomlc17 Configuration Module
# =============================================================================
# This module handles tomlc17 dependency patching and setup.
#
# Prerequisites:
#   - None (runs early in build process)
#
# Outputs:
#   - Applies tomlc17-fix-align8-overflow.patch if needed
#   - Sets up tomlc17 source files for compilation
# =============================================================================

function(configure_tomlc17)
    set(TOMLC17_DIR "${CMAKE_SOURCE_DIR}/deps/tomlc17")
    set(TOMLC17_PATCH "${CMAKE_SOURCE_DIR}/deps/tomlc17-fix-align8-overflow.patch")
    set(TOMLC17_SOURCE "${TOMLC17_DIR}/src/tomlc17.c")
    set(PATCH_MARKER "${TOMLC17_DIR}/.patch_applied")

    # Check if patch needs to be applied
    if(NOT EXISTS "${PATCH_MARKER}")
        message(STATUS "Applying tomlc17 fix-align8-overflow patch...")

        # Try to apply the patch
        execute_process(
            COMMAND git apply --check "${TOMLC17_PATCH}"
            WORKING_DIRECTORY "${TOMLC17_DIR}"
            RESULT_VARIABLE PATCH_CHECK_RESULT
            OUTPUT_QUIET
            ERROR_QUIET
        )

        if(PATCH_CHECK_RESULT EQUAL 0)
            # Patch can be applied cleanly
            execute_process(
                COMMAND git apply "${TOMLC17_PATCH}"
                WORKING_DIRECTORY "${TOMLC17_DIR}"
                RESULT_VARIABLE PATCH_RESULT
                OUTPUT_VARIABLE PATCH_OUTPUT
                ERROR_VARIABLE PATCH_ERROR
            )

            if(PATCH_RESULT EQUAL 0)
                # Create marker file to indicate patch was applied
                file(WRITE "${PATCH_MARKER}" "Patch applied at ${CMAKE_CURRENT_LIST_FILE}\n")

                # Tell git to ignore the patched file changes
                execute_process(
                    COMMAND git update-index --assume-unchanged src/tomlc17.c
                    WORKING_DIRECTORY "${TOMLC17_DIR}"
                    OUTPUT_QUIET
                    ERROR_QUIET
                )

                message(STATUS "tomlc17 patch applied successfully")
            else()
                message(WARNING "Failed to apply tomlc17 patch: ${PATCH_ERROR}")
            endif()
        else()
            # Patch already applied or source differs
            message(STATUS "tomlc17 patch already applied or not needed")
            file(WRITE "${PATCH_MARKER}" "Patch skipped (already applied or not needed)\n")

            # Still set assume-unchanged to hide the modifications
            execute_process(
                COMMAND git update-index --assume-unchanged src/tomlc17.c
                WORKING_DIRECTORY "${TOMLC17_DIR}"
                OUTPUT_QUIET
                ERROR_QUIET
            )
        endif()
    else()
        # Patch marker exists, but ensure assume-unchanged is still set
        # (in case the user ran 'git update-index --no-assume-unchanged')
        execute_process(
            COMMAND git update-index --assume-unchanged src/tomlc17.c
            WORKING_DIRECTORY "${TOMLC17_DIR}"
            OUTPUT_QUIET
            ERROR_QUIET
        )
    endif()

    # Verify the source file exists
    if(NOT EXISTS "${TOMLC17_SOURCE}")
        message(FATAL_ERROR "tomlc17 source file not found: ${TOMLC17_SOURCE}")
    endif()
endfunction()
