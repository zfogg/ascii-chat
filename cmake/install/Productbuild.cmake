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

# Installation location
set(CPACK_PACKAGING_INSTALL_PREFIX "/usr/local")

# Override CPACK_PACKAGE_INSTALL_DIRECTORY for macOS
# This variable is primarily for Windows installers and causes issues with productbuild
set(CPACK_PACKAGE_INSTALL_DIRECTORY "" CACHE STRING "Installation directory override for productbuild" FORCE)

# Note: Welcome and ReadMe files are created in Install.cmake before include(CPack)
# This is required because CPack needs them to be set before it's included

# Background image for installer
if(EXISTS "${CMAKE_SOURCE_DIR}/images/installer_icon.png")
    set(CPACK_PRODUCTBUILD_BACKGROUND "${CMAKE_SOURCE_DIR}/images/installer_icon.png")
endif()

# Resources directory (can contain background images, license files, etc.)
# set(CPACK_PRODUCTBUILD_RESOURCES_DIR "${CMAKE_SOURCE_DIR}/packaging/resources")

# Code signing identity (if you want to sign the package)
 set(CPACK_PRODUCTBUILD_IDENTITY_NAME "Zachary Fogg")

# Keychain to use for signing
set(CPACK_PRODUCTBUILD_KEYCHAIN_PATH "~/Library/Keychains/ascii-chat-master.keychain")

set(CPACK_PRODUCTBUILD_DOMAINS true)

message(STATUS "${Yellow}CPack:${ColorReset} productbuild configuration complete")
