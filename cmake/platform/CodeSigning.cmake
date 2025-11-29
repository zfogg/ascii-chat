# =============================================================================
# macOS Code Signing Module
# =============================================================================
# This module provides code signing support for macOS executables.
#
# Configuration:
#   CODESIGNING_IDENTITY - The identity to use for code signing
#                          Defaults to $ENV{CODESIGNING_IDENTITY}
#                          Set to empty string "" to disable code signing
#
# Usage:
#   include(${CMAKE_SOURCE_DIR}/cmake/platform/CodeSigning.cmake)
#   codesign_target(target_name)
#
# =============================================================================

# Only proceed on macOS
if(NOT APPLE)
    # Define no-op function for non-macOS platforms
    function(codesign_target target_name)
        # No-op on non-macOS platforms
    endfunction()
    return()
endif()

# =============================================================================
# Code Signing Identity Configuration
# =============================================================================

# If CODESIGNING_IDENTITY is set via environment but not in cache, use the environment value
# This allows users to set the identity via environment variable without explicitly passing -D
if(DEFINED ENV{CODESIGNING_IDENTITY} AND NOT DEFINED CACHE{CODESIGNING_IDENTITY})
    set(CODESIGNING_IDENTITY "$ENV{CODESIGNING_IDENTITY}" CACHE STRING
        "Code signing identity for macOS executables (e.g., 'Developer ID Application: Your Name')")
elseif(NOT DEFINED CACHE{CODESIGNING_IDENTITY})
    # No environment variable and no cache - create empty cache entry
    set(CODESIGNING_IDENTITY "" CACHE STRING
        "Code signing identity for macOS executables (e.g., 'Developer ID Application: Your Name')")
endif()
# If cache already exists, it will be used as-is (user can override with -DCODESIGNING_IDENTITY=...)

# Check if code signing is available
set(CODESIGNING_ENABLED FALSE)

if(CODESIGNING_IDENTITY)
    # Verify the identity exists in the keychain
    execute_process(
        COMMAND security find-identity -v -p codesigning
        OUTPUT_VARIABLE CODESIGNING_IDENTITIES
        ERROR_QUIET
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )

    if(CODESIGNING_IDENTITIES MATCHES "${CODESIGNING_IDENTITY}")
        set(CODESIGNING_ENABLED TRUE)
        message(STATUS "Code signing enabled with identity: ${BoldGreen}${CODESIGNING_IDENTITY}${ColorReset}")
    else()
        message(WARNING "Code signing identity '${CODESIGNING_IDENTITY}' not found in keychain. "
                        "Code signing disabled. Available identities:\n${CODESIGNING_IDENTITIES}")
    endif()
else()
    message(STATUS "Code signing disabled (no CODESIGNING_IDENTITY set)")
endif()

# =============================================================================
# Code Signing Function
# =============================================================================

# Function to add code signing to a target
# Usage: codesign_target(target_name [ENTITLEMENTS entitlements.plist])
function(codesign_target target_name)
    if(NOT CODESIGNING_ENABLED)
        return()
    endif()

    # Parse optional arguments
    cmake_parse_arguments(CODESIGN "" "ENTITLEMENTS" "" ${ARGN})

    # Build codesign command
    set(CODESIGN_CMD codesign --force --sign "${CODESIGNING_IDENTITY}")

    # Add timestamp for distribution builds
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        list(APPEND CODESIGN_CMD --timestamp)
    endif()

    # Add hardened runtime for notarization compatibility
    list(APPEND CODESIGN_CMD --options runtime)

    # Add entitlements if specified
    if(CODESIGN_ENTITLEMENTS)
        if(EXISTS "${CODESIGN_ENTITLEMENTS}")
            list(APPEND CODESIGN_CMD --entitlements "${CODESIGN_ENTITLEMENTS}")
        elseif(EXISTS "${CMAKE_SOURCE_DIR}/${CODESIGN_ENTITLEMENTS}")
            list(APPEND CODESIGN_CMD --entitlements "${CMAKE_SOURCE_DIR}/${CODESIGN_ENTITLEMENTS}")
        else()
            message(WARNING "Entitlements file not found: ${CODESIGN_ENTITLEMENTS}")
        endif()
    endif()

    # Add verbose flag for debugging
    list(APPEND CODESIGN_CMD -v)

    # Add the target binary (using generator expression for correct path)
    list(APPEND CODESIGN_CMD "$<TARGET_FILE:${target_name}>")

    # Add post-build command to sign the executable
    add_custom_command(TARGET ${target_name} POST_BUILD
        COMMAND ${CODESIGN_CMD}
        COMMENT "Code signing ${target_name} with identity: ${CODESIGNING_IDENTITY}"
        VERBATIM
    )

    message(STATUS "Code signing configured for target: ${target_name}")
endfunction()

# =============================================================================
# Verification Function
# =============================================================================

# Function to verify code signature (useful for debugging)
# Usage: codesign_verify(target_name)
function(codesign_verify target_name)
    if(NOT CODESIGNING_ENABLED)
        return()
    endif()

    add_custom_command(TARGET ${target_name} POST_BUILD
        COMMAND codesign --verify --deep --strict "$<TARGET_FILE:${target_name}>"
        COMMAND codesign --display --verbose=4 "$<TARGET_FILE:${target_name}>"
        COMMENT "Verifying code signature for ${target_name}"
        VERBATIM
    )
endfunction()
