# =============================================================================
# Build Timing Utilities
# =============================================================================
# This module sets up global build timing that tracks the entire build process
# from first compilation to final linking.
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
add_custom_target(build-timer-start ALL
    COMMAND ${CMAKE_COMMAND} -DACTION=start -DTARGET_NAME=build-total -DSOURCE_DIR=${CMAKE_SOURCE_DIR} -P ${CMAKE_SOURCE_DIR}/cmake/utils/BuildTimer.cmake
    COMMAND_ECHO NONE
    VERBATIM
)
