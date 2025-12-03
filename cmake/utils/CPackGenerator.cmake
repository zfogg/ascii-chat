# =============================================================================
# CPack Generator Utility
# =============================================================================
# Provides a reusable function for enabling CPack generators with consistent
# platform checks, tool validation, and status messaging.
#
# Usage:
#   enable_cpack_generator(
#       NAME <generator_name>           # e.g., "DEB", "RPM", "WIX", "productbuild"
#       [PLATFORM <platform>]           # UNIX, APPLE, WIN32, UNIX_NOT_APPLE, ANY
#       [REQUIRED_TOOL <var_name>]      # e.g., ASCIICHAT_DPKG_DEB_EXECUTABLE
#       [TOOL_DISPLAY_NAME <name>]      # Human-readable name for messages
#       [ALWAYS_AVAILABLE]              # Skip tool check, generator is built-in
#       [SKIP_IF_EXISTS]                # Don't add if already in CPACK_GENERATOR
#   )
#
# Example:
#   enable_cpack_generator(
#       NAME "DEB"
#       PLATFORM UNIX_NOT_APPLE
#       REQUIRED_TOOL ASCIICHAT_DPKG_DEB_EXECUTABLE
#       TOOL_DISPLAY_NAME "dpkg-deb"
#   )
#
# Returns:
#   Sets <NAME>_GENERATOR_ENABLED to TRUE/FALSE in parent scope
# =============================================================================

# Guard against multiple inclusions
if(DEFINED _ASCIICHAT_CPACK_GENERATOR_INCLUDED)
    return()
endif()
set(_ASCIICHAT_CPACK_GENERATOR_INCLUDED TRUE)

function(enable_cpack_generator)
    # Parse arguments
    set(_options ALWAYS_AVAILABLE SKIP_IF_EXISTS)
    set(_one_value_args NAME PLATFORM REQUIRED_TOOL TOOL_DISPLAY_NAME)
    set(_multi_value_args)
    cmake_parse_arguments(_GEN "${_options}" "${_one_value_args}" "${_multi_value_args}" ${ARGN})

    # Validate required arguments
    if(NOT _GEN_NAME)
        message(FATAL_ERROR "enable_cpack_generator: NAME argument is required")
    endif()

    # Default platform to ANY
    if(NOT _GEN_PLATFORM)
        set(_GEN_PLATFORM "ANY")
    endif()

    # Default tool display name to the variable name without prefix
    if(NOT _GEN_TOOL_DISPLAY_NAME AND _GEN_REQUIRED_TOOL)
        string(REGEX REPLACE "^ASCIICHAT_" "" _GEN_TOOL_DISPLAY_NAME "${_GEN_REQUIRED_TOOL}")
        string(REGEX REPLACE "_EXECUTABLE$" "" _GEN_TOOL_DISPLAY_NAME "${_GEN_TOOL_DISPLAY_NAME}")
        string(TOLOWER "${_GEN_TOOL_DISPLAY_NAME}" _GEN_TOOL_DISPLAY_NAME)
    endif()

    # Initialize result variable
    set(${_GEN_NAME}_GENERATOR_ENABLED FALSE PARENT_SCOPE)

    # ==========================================================================
    # Platform Check
    # ==========================================================================
    set(_platform_ok FALSE)

    if(_GEN_PLATFORM STREQUAL "ANY")
        set(_platform_ok TRUE)
    elseif(_GEN_PLATFORM STREQUAL "UNIX")
        if(UNIX)
            set(_platform_ok TRUE)
        endif()
    elseif(_GEN_PLATFORM STREQUAL "APPLE")
        if(APPLE)
            set(_platform_ok TRUE)
        endif()
    elseif(_GEN_PLATFORM STREQUAL "WIN32")
        if(WIN32)
            set(_platform_ok TRUE)
        endif()
    elseif(_GEN_PLATFORM STREQUAL "UNIX_NOT_APPLE")
        if(UNIX AND NOT APPLE)
            set(_platform_ok TRUE)
        endif()
    elseif(_GEN_PLATFORM STREQUAL "LINUX")
        if(UNIX AND NOT APPLE)
            set(_platform_ok TRUE)
        endif()
    else()
        message(WARNING "enable_cpack_generator: Unknown platform '${_GEN_PLATFORM}' for ${_GEN_NAME}")
    endif()

    if(NOT _platform_ok)
        return()
    endif()

    # ==========================================================================
    # Skip if already exists (optional)
    # ==========================================================================
    if(_GEN_SKIP_IF_EXISTS)
        if("${_GEN_NAME}" IN_LIST CPACK_GENERATOR)
            set(${_GEN_NAME}_GENERATOR_ENABLED TRUE PARENT_SCOPE)
            return()
        endif()
    endif()

    # ==========================================================================
    # Tool Availability Check
    # ==========================================================================
    if(NOT _GEN_ALWAYS_AVAILABLE)
        if(_GEN_REQUIRED_TOOL)
            if(NOT ${_GEN_REQUIRED_TOOL})
                message(STATUS "${Red}CPack:${ColorReset} ${_GEN_NAME} generator disabled (${BoldBlue}${_GEN_TOOL_DISPLAY_NAME}${ColorReset} not found)")
                return()
            endif()
        endif()
    endif()

    # ==========================================================================
    # Enable Generator
    # ==========================================================================
    list(APPEND CPACK_GENERATOR "${_GEN_NAME}")
    # Force update the cache so it persists
    set(CPACK_GENERATOR "${CPACK_GENERATOR}" CACHE STRING "CPack generators" FORCE)
    # Also set in parent scope for immediate use
    set(CPACK_GENERATOR "${CPACK_GENERATOR}" PARENT_SCOPE)

    # Status message
    if(_GEN_ALWAYS_AVAILABLE)
        message(STATUS "${Yellow}CPack:${ColorReset} ${_GEN_NAME} generator enabled (always available)")
    elseif(_GEN_REQUIRED_TOOL)
        message(STATUS "${Yellow}CPack:${ColorReset} ${_GEN_NAME} generator enabled (${BoldBlue}${_GEN_TOOL_DISPLAY_NAME}${ColorReset} found)")
    else()
        message(STATUS "${Yellow}CPack:${ColorReset} ${_GEN_NAME} generator enabled")
    endif()

    set(${_GEN_NAME}_GENERATOR_ENABLED TRUE PARENT_SCOPE)
endfunction()

# =============================================================================
# Convenience wrapper for common archive generators
# =============================================================================
# Enables TGZ/ZIP based on platform with appropriate tool checks
#
# Usage:
#   enable_archive_generators()
#
function(enable_archive_generators)
    if(UNIX AND NOT APPLE)
        # Linux: TGZ always available
        enable_cpack_generator(NAME "TGZ" PLATFORM UNIX_NOT_APPLE ALWAYS_AVAILABLE)
        # ZIP requires zip command
        enable_cpack_generator(
            NAME "ZIP"
            PLATFORM UNIX_NOT_APPLE
            REQUIRED_TOOL ASCIICHAT_ZIP_EXECUTABLE
            TOOL_DISPLAY_NAME "zip"
        )
    elseif(APPLE)
        # macOS: TGZ always available
        enable_cpack_generator(NAME "TGZ" PLATFORM APPLE ALWAYS_AVAILABLE)
    elseif(WIN32)
        # Windows: ZIP always available (CMake's built-in tar)
        enable_cpack_generator(NAME "ZIP" PLATFORM WIN32 ALWAYS_AVAILABLE SKIP_IF_EXISTS)
    else()
        # Fallback
        enable_cpack_generator(NAME "TGZ" ALWAYS_AVAILABLE)
    endif()
endfunction()

