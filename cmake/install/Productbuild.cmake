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

if(NOT APPLE)
    return()
endif()

# Use centralized ASCIICHAT_PRODUCTBUILD_EXECUTABLE from FindPrograms.cmake
if(NOT ASCIICHAT_PRODUCTBUILD_EXECUTABLE)
    message(STATUS "${Red}CPack:${ColorReset} productbuild generator disabled (${BoldBlue}productbuild${ColorReset} not found)")
    return()
endif()

# Add productbuild to generator list
list(APPEND CPACK_GENERATOR "productbuild")
# Force update the cache so it persists
set(CPACK_GENERATOR "${CPACK_GENERATOR}" CACHE STRING "CPack generators" FORCE)
message(STATUS "${Yellow}CPack:${ColorReset} productbuild generator enabled (${BoldBlue}productbuild${ColorReset} found)")

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
