# =============================================================================
# ProductBuild Package Configuration (macOS)
# =============================================================================
# Configures CPack to generate .pkg installers for macOS using productbuild
#
# Prerequisites:
#   - productbuild must be available in PATH (part of Xcode Command Line Tools)
#   - CPack variables must be set (CPACK_PACKAGE_NAME, etc.)
#   - Must be included after basic CPack configuration
#
# Outputs:
#   - Adds "productbuild" to CPACK_GENERATOR if productbuild is found
#   - Configures productbuild-specific CPack variables
# =============================================================================

include(${CMAKE_SOURCE_DIR}/cmake/utils/CPackGenerator.cmake)

enable_cpack_generator(
    NAME "productbuild"
    PLATFORM APPLE
    REQUIRED_TOOL ASCIICHAT_PRODUCTBUILD_EXECUTABLE
    TOOL_DISPLAY_NAME "productbuild"
)

if(NOT productbuild_GENERATOR_ENABLED)
    return()
endif()

# =============================================================================
# ProductBuild Package Configuration
# =============================================================================

# Package identifier (reverse DNS notation)
set(CPACK_PRODUCTBUILD_IDENTIFIER "${PROJECT_BUNDLE_ID}")

# Note: CPACK_PACKAGING_INSTALL_PREFIX is set per-generator in CPackProjectConfig.cmake
# This allows different prefixes for productbuild (/usr/local) vs STGZ/TGZ (/)

# Override CPACK_PACKAGE_INSTALL_DIRECTORY for macOS
# This variable is primarily for Windows installers and causes issues with productbuild
set(CPACK_PACKAGE_INSTALL_DIRECTORY "" CACHE STRING "Installation directory override for productbuild" FORCE)

# Note: Welcome and ReadMe files are created in Install.cmake before include(CPack)
# This is required because CPack needs them to be set before it's included

# Resources directory containing installer assets (background images, etc.)
# CPACK_PRODUCTBUILD_BACKGROUND must be relative to this directory
if(EXISTS "${CMAKE_SOURCE_DIR}/images")
    set(CPACK_PRODUCTBUILD_RESOURCES_DIR "${CMAKE_SOURCE_DIR}/images")
    if(EXISTS "${CMAKE_SOURCE_DIR}/images/installer_icon.png")
        set(CPACK_PRODUCTBUILD_BACKGROUND "installer_icon.png")
    endif()
endif()

# Code signing configuration
# Only sign if explicitly enabled via ASCIICHAT_CODESIGN_IDENTITY
# GitHub runners don't have signing credentials, so signing is disabled by default
if(DEFINED ASCIICHAT_CODESIGN_IDENTITY AND NOT ASCIICHAT_CODESIGN_IDENTITY STREQUAL "")
    set(CPACK_PRODUCTBUILD_IDENTITY_NAME "${ASCIICHAT_CODESIGN_IDENTITY}")
    message(STATUS "${Yellow}CPack:${ColorReset} productbuild signing enabled with identity: ${ASCIICHAT_CODESIGN_IDENTITY}")

    # Keychain to use for signing (optional)
    if(DEFINED ASCIICHAT_CODESIGN_KEYCHAIN AND NOT ASCIICHAT_CODESIGN_KEYCHAIN STREQUAL "")
        set(CPACK_PRODUCTBUILD_KEYCHAIN_PATH "${ASCIICHAT_CODESIGN_KEYCHAIN}")
        message(STATUS "${Yellow}CPack:${ColorReset} productbuild using keychain: ${ASCIICHAT_CODESIGN_KEYCHAIN}")
    endif()
else()
    message(STATUS "${Yellow}CPack:${ColorReset} productbuild signing disabled (set ASCIICHAT_CODESIGN_IDENTITY to enable)")
endif()

set(CPACK_PRODUCTBUILD_DOMAINS true)

message(STATUS "${Yellow}CPack:${ColorReset} productbuild configuration complete")
