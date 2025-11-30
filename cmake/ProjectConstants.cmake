# =============================================================================
# Project Constants
# =============================================================================
# Central location for all project metadata used throughout the build system.
# This file should be included early in the build process (before CPack setup).
#
# Usage:
#   include(${CMAKE_SOURCE_DIR}/cmake/ProjectConstants.cmake)
#
# =============================================================================

# =============================================================================
# Project Identity
# =============================================================================

set(PROJECT_NAME_FULL "ascii-chat" CACHE STRING "Full project name")
set(PROJECT_DISPLAY_NAME "ascii-chat" CACHE STRING "Display name for installers")

# Bundle/Package identifiers (reverse DNS notation)
set(PROJECT_BUNDLE_ID "gg.zfo.ascii-chat" CACHE STRING "Bundle identifier for macOS")
set(PROJECT_PACKAGE_ID "gg.zfo.ascii-chat" CACHE STRING "Package identifier")

# =============================================================================
# Author Information
# =============================================================================

set(PROJECT_AUTHOR_NAME "Zachary Fogg" CACHE STRING "Project author name")
set(PROJECT_AUTHOR_EMAIL "me@zfo.gg" CACHE STRING "Project author email")
set(PROJECT_AUTHOR_USERNAME "zfogg" CACHE STRING "Author's username/handle")

# Vendor name (for package systems)
set(PROJECT_VENDOR "zfogg" CACHE STRING "Vendor name for packages")

# Maintainer info (for packages)
set(PROJECT_MAINTAINER "${PROJECT_AUTHOR_NAME} <${PROJECT_AUTHOR_EMAIL}>" CACHE STRING "Package maintainer")
set(PROJECT_MAINTAINER_FALLBACK "${PROJECT_NAME_FULL} <${PROJECT_AUTHOR_EMAIL}>" CACHE STRING "Fallback maintainer")

# =============================================================================
# Project URLs
# =============================================================================

set(PROJECT_HOMEPAGE_URL "https://github.com/${PROJECT_AUTHOR_USERNAME}/${PROJECT_NAME_FULL}" CACHE STRING "Project homepage")
set(PROJECT_REPOSITORY_URL "${PROJECT_HOMEPAGE_URL}" CACHE STRING "Source repository URL")
set(PROJECT_ISSUES_URL "${PROJECT_HOMEPAGE_URL}/issues" CACHE STRING "Issue tracker URL")
set(PROJECT_RELEASES_URL "${PROJECT_HOMEPAGE_URL}/releases" CACHE STRING "Releases page URL")

# Contact information
set(PROJECT_CONTACT "${PROJECT_HOMEPAGE_URL}" CACHE STRING "Project contact URL")
set(PROJECT_SUPPORT_URL "${PROJECT_HOMEPAGE_URL}" CACHE STRING "Support URL")

# =============================================================================
# Project Descriptions
# =============================================================================

# Short summary with emoji (for display/marketing)
set(PROJECT_DESCRIPTION_SUMMARY "📸 Video chat in your terminal 🔠" CACHE STRING "Short project description")

# Full description (for package metadata)
set(PROJECT_DESCRIPTION_FULL
    "ascii-chat is a terminal video chat application that converts video into ASCII art. It has audio support, renders to monochrome/16color/256color/truecolor, has end-to-end encryption, allows multiple clients (3+) to connect and displays them in an video grid like Zoom and Google Hangouts, and has many other neat little features."
    CACHE STRING "Full project description"
)

# One-line description (for installers)
set(PROJECT_DESCRIPTION_ONELINE "${PROJECT_DESCRIPTION_SUMMARY}" CACHE STRING "One-line description")

# =============================================================================
# Legal Information
# =============================================================================

set(PROJECT_LICENSE "MIT" CACHE STRING "Project license")
set(PROJECT_LICENSE_SPDX "MIT" CACHE STRING "SPDX License identifier")
set(PROJECT_LICENSE_FILE "${CMAKE_SOURCE_DIR}/LICENSE.txt" CACHE FILEPATH "License file path")

# Copyright notice
set(PROJECT_COPYRIGHT "Copyright (c) 2013 ${PROJECT_AUTHOR_NAME}" CACHE STRING "Copyright notice")

# =============================================================================
# Package-Specific Identifiers
# =============================================================================

# WiX Upgrade GUID (NEVER CHANGE THIS!)
# This GUID identifies the product family for upgrade detection
set(CPACK_WIX_UPGRADE_GUID "A1B2C3D4-E5F6-7890-ABCD-EF1234567890" CACHE STRING "WiX upgrade GUID" FORCE)

# WiX Product GUID (auto-generated per version)
set(CPACK_WIX_PRODUCT_GUID "*" CACHE STRING "WiX product GUID" FORCE)

# WiX UI reference (feature tree for component selection)
set(CPACK_WIX_UI_REF "WixUI_FeatureTree" CACHE STRING "WiX UI dialog set" FORCE)

# =============================================================================
# Platform-Specific Scripts
# =============================================================================

if(WIN32)
    set(PROJECT_INSTALL_DEPS_SCRIPT "./scripts/install-deps.ps1" CACHE STRING "Dependency installation script")
else()
    set(PROJECT_INSTALL_DEPS_SCRIPT "./scripts/install-deps.sh" CACHE STRING "Dependency installation script")
endif()

# =============================================================================
# CPack Configuration Variables
# =============================================================================
# NOTE: These are set here to ensure consistency across all CPack generators

set(CPACK_PACKAGE_NAME "${PROJECT_NAME_FULL}" CACHE STRING "Package name" FORCE)
set(CPACK_PACKAGE_VENDOR "${PROJECT_VENDOR}" CACHE STRING "Package vendor" FORCE)
set(CPACK_PACKAGE_CONTACT "${PROJECT_CONTACT}" CACHE STRING "Package contact" FORCE)
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION_SUMMARY}" CACHE STRING "Package summary" FORCE)
set(CPACK_PACKAGE_DESCRIPTION "${PROJECT_DESCRIPTION_FULL}" CACHE STRING "Package description" FORCE)
set(CPACK_PACKAGE_HOMEPAGE_URL "${PROJECT_HOMEPAGE_URL}" CACHE STRING "Package homepage" FORCE)

# Debian-specific: Set maintainer in email format for DEB packages
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "${PROJECT_MAINTAINER}" CACHE STRING "DEB package maintainer" FORCE)

# Installation directory (without version suffix)
set(CPACK_PACKAGE_INSTALL_DIRECTORY "${PROJECT_NAME_FULL}" CACHE STRING "Installation directory" FORCE)

# License file
if(EXISTS "${PROJECT_LICENSE_FILE}")
    set(CPACK_RESOURCE_FILE_LICENSE "${PROJECT_LICENSE_FILE}" CACHE FILEPATH "License file for installers" FORCE)
endif()

