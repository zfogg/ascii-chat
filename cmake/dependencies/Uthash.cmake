# =============================================================================
# Uthash Configuration Module
# =============================================================================
# This module handles uthash dependency setup.
#
# Uthash is a header-only hash table library for C structures. It's a single
# header file that provides hash table functionality via macros.
#
# Prerequisites:
#   - None (runs early in build process)
#
# Outputs:
#   - Configures uthash include directory for use
#   - No library linking required (header-only)
# =============================================================================

function(configure_uthash)
    set(UTHASH_DIR "${CMAKE_SOURCE_DIR}/deps/uthash")
    set(UTHASH_HEADER "${UTHASH_DIR}/src/uthash.h")

    # Verify the header file exists
    if(NOT EXISTS "${UTHASH_HEADER}")
        message(FATAL_ERROR "uthash header file not found: ${UTHASH_HEADER}")
    endif()

    message(STATUS "Configured ${BoldGreen}uthash${ColorReset} from ${BoldCyan}${UTHASH_DIR}${ColorReset}")
endfunction()

