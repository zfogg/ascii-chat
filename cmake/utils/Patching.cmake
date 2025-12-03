# =============================================================================
# Patching Utilities
# =============================================================================
# Common functions for applying patches to dependencies.
#
# Usage:
#   apply_patch(
#       TARGET_DIR "/path/to/dep"
#       PATCH_FILE "/path/to/patch.patch"
#       PATCH_NUM 0
#       DESCRIPTION "Fix for Windows compatibility"
#       [PLATFORM WIN32|UNIX|APPLE]  # Optional: only apply on specific platform
#       [ASSUME_UNCHANGED "file1.c" "file2.c"]  # Optional: files to mark assume-unchanged
#   )
#
# The function uses a marker file system to avoid re-applying patches.
# Marker files are stored in ${ASCIICHAT_DEPS_CACHE_ROOT}/<dep_name>.patch_<num>
# =============================================================================

# Apply a single patch to a dependency
#
# Arguments:
#   TARGET_DIR     - Directory containing the dependency (working directory for git)
#   PATCH_FILE     - Path to the patch file
#   PATCH_NUM      - Patch number (0, 1, 2, etc.) for ordering and marker files
#   DESCRIPTION    - Human-readable description of what the patch does
#   PLATFORM       - (Optional) Only apply on WIN32, UNIX, or APPLE
#   ASSUME_UNCHANGED - (Optional) List of files to mark as assume-unchanged in git
#
# Returns:
#   Sets ${DEP_NAME}_PATCH_${PATCH_NUM}_APPLIED to TRUE/FALSE in parent scope
#
function(apply_patch)
    cmake_parse_arguments(
        PATCH                           # prefix
        ""                              # options (flags)
        "TARGET_DIR;PATCH_FILE;PATCH_NUM;DESCRIPTION;PLATFORM"  # single-value args
        "ASSUME_UNCHANGED"              # multi-value args
        ${ARGN}
    )

    # Validate required arguments
    if(NOT PATCH_TARGET_DIR)
        message(FATAL_ERROR "apply_patch: TARGET_DIR is required")
    endif()
    if(NOT PATCH_PATCH_FILE)
        message(FATAL_ERROR "apply_patch: PATCH_FILE is required")
    endif()
    if(NOT DEFINED PATCH_PATCH_NUM)
        message(FATAL_ERROR "apply_patch: PATCH_NUM is required")
    endif()
    if(NOT PATCH_DESCRIPTION)
        message(FATAL_ERROR "apply_patch: DESCRIPTION is required")
    endif()

    # Check platform restriction
    if(PATCH_PLATFORM)
        if(PATCH_PLATFORM STREQUAL "WIN32" AND NOT WIN32)
            return()
        elseif(PATCH_PLATFORM STREQUAL "UNIX" AND NOT UNIX)
            return()
        elseif(PATCH_PLATFORM STREQUAL "APPLE" AND NOT APPLE)
            return()
        endif()
    endif()

    # Extract dependency name from directory path
    get_filename_component(DEP_NAME "${PATCH_TARGET_DIR}" NAME)

    # Create marker file path
    set(PATCH_MARKER "${ASCIICHAT_DEPS_CACHE_ROOT}/${DEP_NAME}.patch_${PATCH_PATCH_NUM}")

    # Result variable name
    set(RESULT_VAR "${DEP_NAME}_PATCH_${PATCH_PATCH_NUM}_APPLIED")

    # Check if this specific patch was already applied
    if(EXISTS "${PATCH_MARKER}")
        # Patch already applied, just ensure assume-unchanged is set
        if(PATCH_ASSUME_UNCHANGED)
            foreach(FILE_PATH ${PATCH_ASSUME_UNCHANGED})
                execute_process(
                    COMMAND git update-index --assume-unchanged "${FILE_PATH}"
                    WORKING_DIRECTORY "${PATCH_TARGET_DIR}"
                    OUTPUT_QUIET
                    ERROR_QUIET
                )
            endforeach()
        endif()
        set(${RESULT_VAR} TRUE PARENT_SCOPE)
        return()
    endif()

    # Log what we're doing
    message(STATUS "Applying ${DEP_NAME} patch #${PATCH_PATCH_NUM}: ${PATCH_DESCRIPTION}...")

    # Check if patch can be applied
    execute_process(
        COMMAND git apply --check "${PATCH_PATCH_FILE}"
        WORKING_DIRECTORY "${PATCH_TARGET_DIR}"
        RESULT_VARIABLE PATCH_CHECK_RESULT
        OUTPUT_QUIET
        ERROR_QUIET
    )

    if(PATCH_CHECK_RESULT EQUAL 0)
        # Patch can be applied cleanly
        execute_process(
            COMMAND git apply "${PATCH_PATCH_FILE}"
            WORKING_DIRECTORY "${PATCH_TARGET_DIR}"
            RESULT_VARIABLE PATCH_APPLY_RESULT
            ERROR_VARIABLE PATCH_ERROR
        )

        if(PATCH_APPLY_RESULT EQUAL 0)
            message(STATUS "  ${DEP_NAME} patch #${PATCH_PATCH_NUM} applied successfully")
            file(WRITE "${PATCH_MARKER}" "${PATCH_DESCRIPTION}\nApplied: ${PATCH_PATCH_FILE}\n")
            set(${RESULT_VAR} TRUE PARENT_SCOPE)
        else()
            message(WARNING "  Failed to apply ${DEP_NAME} patch #${PATCH_PATCH_NUM}: ${PATCH_ERROR}")
            set(${RESULT_VAR} FALSE PARENT_SCOPE)
        endif()
    else()
        # Patch already applied or not needed
        message(STATUS "  ${DEP_NAME} patch #${PATCH_PATCH_NUM} already applied or not needed")
        file(WRITE "${PATCH_MARKER}" "${PATCH_DESCRIPTION}\nSkipped (already applied)\n")
        set(${RESULT_VAR} TRUE PARENT_SCOPE)
    endif()

    # Mark files as assume-unchanged regardless of whether patch was just applied
    if(PATCH_ASSUME_UNCHANGED)
        foreach(FILE_PATH ${PATCH_ASSUME_UNCHANGED})
            execute_process(
                COMMAND git update-index --assume-unchanged "${FILE_PATH}"
                WORKING_DIRECTORY "${PATCH_TARGET_DIR}"
                OUTPUT_QUIET
                ERROR_QUIET
            )
        endforeach()
    endif()
endfunction()


# Convenience function to apply multiple patches to a dependency
#
# Usage:
#   apply_patches(
#       TARGET_DIR "/path/to/dep"
#       PATCHES
#           "0;/path/to/patch0.patch;Description 0;[PLATFORM]"
#           "1;/path/to/patch1.patch;Description 1;[PLATFORM]"
#       ASSUME_UNCHANGED "file1.c" "file2.c"
#   )
#
function(apply_patches)
    cmake_parse_arguments(
        PATCHES                         # prefix
        ""                              # options
        "TARGET_DIR"                    # single-value args
        "PATCHES;ASSUME_UNCHANGED"      # multi-value args
        ${ARGN}
    )

    if(NOT PATCHES_TARGET_DIR)
        message(FATAL_ERROR "apply_patches: TARGET_DIR is required")
    endif()

    foreach(PATCH_SPEC ${PATCHES_PATCHES})
        # Parse the semicolon-separated patch spec: "num;file;description;platform"
        string(REPLACE ";" ";" PATCH_PARTS "${PATCH_SPEC}")
        list(LENGTH PATCH_PARTS PARTS_LEN)

        if(PARTS_LEN LESS 3)
            message(FATAL_ERROR "apply_patches: Invalid patch spec: ${PATCH_SPEC}")
        endif()

        list(GET PATCH_PARTS 0 PATCH_NUM)
        list(GET PATCH_PARTS 1 PATCH_FILE)
        list(GET PATCH_PARTS 2 PATCH_DESC)

        # Optional platform
        set(PATCH_PLAT "")
        if(PARTS_LEN GREATER 3)
            list(GET PATCH_PARTS 3 PATCH_PLAT)
        endif()

        # Build apply_patch call
        if(PATCH_PLAT)
            apply_patch(
                TARGET_DIR "${PATCHES_TARGET_DIR}"
                PATCH_FILE "${PATCH_FILE}"
                PATCH_NUM ${PATCH_NUM}
                DESCRIPTION "${PATCH_DESC}"
                PLATFORM "${PATCH_PLAT}"
                ASSUME_UNCHANGED ${PATCHES_ASSUME_UNCHANGED}
            )
        else()
            apply_patch(
                TARGET_DIR "${PATCHES_TARGET_DIR}"
                PATCH_FILE "${PATCH_FILE}"
                PATCH_NUM ${PATCH_NUM}
                DESCRIPTION "${PATCH_DESC}"
                ASSUME_UNCHANGED ${PATCHES_ASSUME_UNCHANGED}
            )
        endif()
    endforeach()
endfunction()
