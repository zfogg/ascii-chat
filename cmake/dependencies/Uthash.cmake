# =============================================================================
# Uthash Configuration Module
# =============================================================================
# This module handles uthash dependency setup.
#
# Uthash is a header-only hash table library for C structures. It's bundled
# in lib/uthash/ with a wrapper that adds ascii-chat specific customizations:
# - Custom allocators for memory debugging
# - UBSan-safe hash functions
#
# Directory structure:
#   lib/uthash/uthash.h       - Wrapper with customizations
#   lib/uthash/upstream/*.h   - Raw upstream uthash headers
#
# Prerequisites:
#   - None (runs early in build process)
#
# Outputs:
#   - Configures uthash include directory for use
#   - No library linking required (header-only)
# =============================================================================

function(configure_uthash)
    set(UTHASH_DIR "${CMAKE_SOURCE_DIR}/lib/uthash")
    set(UTHASH_HEADER "${UTHASH_DIR}/uthash.h")
    set(UTHASH_UPSTREAM "${UTHASH_DIR}/upstream/uthash.h")

    # Verify the wrapper header exists
    if(NOT EXISTS "${UTHASH_HEADER}")
        message(FATAL_ERROR "uthash wrapper header not found: ${UTHASH_HEADER}")
    endif()

    # Verify the upstream header exists
    if(NOT EXISTS "${UTHASH_UPSTREAM}")
        message(FATAL_ERROR "uthash upstream header not found: ${UTHASH_UPSTREAM}")
    endif()

    message(STATUS "Configured ${BoldGreen}uthash${ColorReset} from ${BoldCyan}${UTHASH_DIR}${ColorReset} (bundled)")
endfunction()

