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
#   - Applies tomlc17-fix-windows-deprecation.patch if needed
#   - Sets up tomlc17 source files for compilation
# =============================================================================

function(configure_tomlc17)
    set(TOMLC17_DIR "${CMAKE_SOURCE_DIR}/deps/tomlc17")
    set(TOMLC17_PATCH1 "${CMAKE_SOURCE_DIR}/cmake/tomlc17-fix-align8-overflow.patch")
    set(TOMLC17_PATCH2 "${CMAKE_SOURCE_DIR}/cmake/tomlc17-fix-windows-deprecation.patch")
    set(TOMLC17_SOURCE "${TOMLC17_DIR}/src/tomlc17.c")
    set(PATCH_MARKER "${CMAKE_SOURCE_DIR}/.deps-cache/tomlc17.patches_applied")

    # Check if patches need to be applied
    if(NOT EXISTS "${PATCH_MARKER}")
        set(PATCHES_APPLIED FALSE)

        # Apply patch 1: align8 overflow fix
        message(STATUS "Applying tomlc17 fix-align8-overflow patch...")
        execute_process(
            COMMAND git apply --check "${TOMLC17_PATCH1}"
            WORKING_DIRECTORY "${TOMLC17_DIR}"
            RESULT_VARIABLE PATCH1_CHECK_RESULT
            OUTPUT_QUIET
            ERROR_QUIET
        )

        if(PATCH1_CHECK_RESULT EQUAL 0)
            execute_process(
                COMMAND git apply "${TOMLC17_PATCH1}"
                WORKING_DIRECTORY "${TOMLC17_DIR}"
                RESULT_VARIABLE PATCH1_RESULT
                ERROR_VARIABLE PATCH1_ERROR
            )
            if(PATCH1_RESULT EQUAL 0)
                message(STATUS "tomlc17 align8 overflow patch applied successfully")
                set(PATCHES_APPLIED TRUE)
            else()
                message(WARNING "Failed to apply tomlc17 align8 overflow patch: ${PATCH1_ERROR}")
            endif()
        else()
            message(STATUS "tomlc17 align8 overflow patch already applied or not needed")
            set(PATCHES_APPLIED TRUE)
        endif()

        # Apply patch 2: Windows deprecation fix
        message(STATUS "Applying tomlc17 fix-windows-deprecation patch...")
        execute_process(
            COMMAND git apply --check "${TOMLC17_PATCH2}"
            WORKING_DIRECTORY "${TOMLC17_DIR}"
            RESULT_VARIABLE PATCH2_CHECK_RESULT
            OUTPUT_QUIET
            ERROR_QUIET
        )

        if(PATCH2_CHECK_RESULT EQUAL 0)
            execute_process(
                COMMAND git apply "${TOMLC17_PATCH2}"
                WORKING_DIRECTORY "${TOMLC17_DIR}"
                RESULT_VARIABLE PATCH2_RESULT
                ERROR_VARIABLE PATCH2_ERROR
            )
            if(PATCH2_RESULT EQUAL 0)
                message(STATUS "tomlc17 Windows deprecation patch applied successfully")
                set(PATCHES_APPLIED TRUE)
            else()
                message(WARNING "Failed to apply tomlc17 Windows deprecation patch: ${PATCH2_ERROR}")
            endif()
        else()
            message(STATUS "tomlc17 Windows deprecation patch already applied or not needed")
            set(PATCHES_APPLIED TRUE)
        endif()

        # Create marker file if any patches were applied
        if(PATCHES_APPLIED)
            file(WRITE "${PATCH_MARKER}" "Patches applied at ${CMAKE_CURRENT_LIST_FILE}\n")

            # Tell git to ignore the patched file changes
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
