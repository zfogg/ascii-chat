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
# Output:
#   - build-timer-start target (runs before any compilation)
#   - Total build time displayed after all targets complete
# =============================================================================

# =============================================================================
# Global Build Timer - Start Marker
# =============================================================================
# This target runs BEFORE any compilation begins to record the build start time
# Use add_custom_command with OUTPUT to create a real file dependency
# This prevents Ninja from always considering it out-of-date
add_custom_command(
    OUTPUT "${CMAKE_BINARY_DIR}/.build_timer_start.stamp"
    COMMAND ${CMAKE_COMMAND} -DACTION=start -DTARGET_NAME=build-total -DSOURCE_DIR=${CMAKE_SOURCE_DIR} -P ${CMAKE_SOURCE_DIR}/cmake/utils/Timer.cmake
    COMMAND ${CMAKE_COMMAND} -E touch "${CMAKE_BINARY_DIR}/.build_timer_start.stamp"
    COMMENT "Starting build timer..."
    VERBATIM
)

# Create the target that depends on the stamp file
add_custom_target(build-timer-start ALL
    DEPENDS "${CMAKE_BINARY_DIR}/.build_timer_start.stamp"
)
