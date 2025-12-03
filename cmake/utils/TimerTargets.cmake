# =============================================================================
# Timer Targets
# =============================================================================
# This module sets up custom targets for global build timing that tracks the
# entire build process from first compilation to final linking.
#
# Usage:
#   - Include this file early (before Libraries.cmake) to create build-timer-start
#   - All library modules will depend on build-timer-start
#   - Executables.cmake creates build-timer-end after all targets
#
# Functions:
#   add_timer_targets(NAME <name> [COMMENT_START <msg>] [COMMENT_END <msg>])
#   timer_start_command(<name> <output_var>)
#   timer_end_command(<name> <output_var>)
#
# Output:
#   - build-timer-start target (runs before any compilation)
#   - Total build time displayed after all targets complete
# =============================================================================

# Guard against multiple inclusions (functions should only be defined once)
if(DEFINED _ASCIICHAT_TIMER_TARGETS_INCLUDED)
    return()
endif()
set(_ASCIICHAT_TIMER_TARGETS_INCLUDED TRUE)

# =============================================================================
# Helper: Generate timer start command
# =============================================================================
# Generates the cmake command arguments for starting a timer
# Usage: timer_start_command("my-target" _cmd)
#        Then use: COMMAND ${_cmd}
function(timer_start_command name output_var)
    set(${output_var}
        ${CMAKE_COMMAND}
            -DACTION=start
            -DTARGET_NAME=${name}
            -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
            -DCMAKE_BINARY_DIR=${CMAKE_BINARY_DIR}
            -P ${CMAKE_SOURCE_DIR}/cmake/utils/Timer.cmake
        PARENT_SCOPE
    )
endfunction()

# =============================================================================
# Helper: Generate timer end command
# =============================================================================
# Generates the cmake command arguments for ending a timer
# Usage: timer_end_command("my-target" _cmd)
#        Then use: COMMAND ${_cmd}
function(timer_end_command name output_var)
    set(${output_var}
        ${CMAKE_COMMAND}
            -DACTION=end
            -DTARGET_NAME=${name}
            -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
            -DCMAKE_BINARY_DIR=${CMAKE_BINARY_DIR}
            -P ${CMAKE_SOURCE_DIR}/cmake/utils/Timer.cmake
        PARENT_SCOPE
    )
endfunction()

# =============================================================================
# Helper: Add timer start/end targets for a named operation
# =============================================================================
# Creates <name>-timer-start and <name>-timer-end custom targets
#
# Usage:
#   add_timer_targets(
#       NAME defer-all
#       [COMMENT_START "Starting defer transformation"]
#       [COMMENT_END "Finished defer transformation"]
#   )
#
# Creates targets:
#   - defer-all-timer-start
#   - defer-all-timer-end
#
function(add_timer_targets)
    cmake_parse_arguments(_TT "" "NAME;COMMENT_START;COMMENT_END" "" ${ARGN})

    if(NOT _TT_NAME)
        message(FATAL_ERROR "add_timer_targets: NAME is required")
    endif()

    if(NOT _TT_COMMENT_START)
        set(_TT_COMMENT_START "Starting ${_TT_NAME} timing block")
    endif()
    if(NOT _TT_COMMENT_END)
        set(_TT_COMMENT_END "Finishing ${_TT_NAME} timing block")
    endif()

    set(_start_target "${_TT_NAME}-timer-start")
    set(_end_target "${_TT_NAME}-timer-end")

    # Create start target if it doesn't exist
    if(NOT TARGET ${_start_target})
        timer_start_command(${_TT_NAME} _start_cmd)
        add_custom_target(${_start_target}
            COMMAND ${_start_cmd}
            COMMENT "${_TT_COMMENT_START}"
            VERBATIM
        )
    endif()

    # Create end target if it doesn't exist
    if(NOT TARGET ${_end_target})
        timer_end_command(${_TT_NAME} _end_cmd)
        add_custom_target(${_end_target}
            COMMAND ${_end_cmd}
            COMMENT "${_TT_COMMENT_END}"
            VERBATIM
        )
        # End depends on start
        add_dependencies(${_end_target} ${_start_target})
    endif()
endfunction()

# =============================================================================
# Global Build Timer - Start Marker
# =============================================================================
# This target runs BEFORE any compilation begins to record the build start time
# Use add_custom_command with OUTPUT to create a real file dependency
# This prevents Ninja from always considering it out-of-date
timer_start_command(build-total _build_timer_start_cmd)
add_custom_command(
    OUTPUT "${CMAKE_BINARY_DIR}/.build_timer_start.stamp"
    COMMAND ${_build_timer_start_cmd}
    COMMAND ${CMAKE_COMMAND} -E touch "${CMAKE_BINARY_DIR}/.build_timer_start.stamp"
    COMMENT "Starting build timer..."
    VERBATIM
)

# Create the target that depends on the stamp file
add_custom_target(build-timer-start ALL
    DEPENDS "${CMAKE_BINARY_DIR}/.build_timer_start.stamp"
)
