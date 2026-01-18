# =============================================================================
# Uthash Configuration Module
# =============================================================================
# This module handles uthash dependency setup.
#
# Uthash is a header-only hash table library for C structures. It's located
# in deps/ascii-chat-deps/uthash/ as a git submodule.
#
# Directory structure:
#   deps/ascii-chat-deps/uthash/src/*.h - Upstream uthash headers
#
# Prerequisites:
#   - uthash submodule must be initialized
#
# Outputs:
#   - Configures uthash include directory for use
#   - No library linking required (header-only)
# =============================================================================

function(configure_uthash)
    set(UTHASH_DIR "${CMAKE_SOURCE_DIR}/deps/ascii-chat-deps/uthash/src")
    set(UTHASH_HEADER "${UTHASH_DIR}/uthash.h")

    # Verify the header exists
    if(NOT EXISTS "${UTHASH_HEADER}")
        message(FATAL_ERROR "uthash header not found: ${UTHASH_HEADER}\nMake sure the uthash submodule is initialized: git submodule update --init --recursive")
    endif()

    message(STATUS "Configured ${BoldGreen}uthash${ColorReset} from ${BoldCyan}${UTHASH_DIR}${ColorReset} (submodule)")
endfunction()

