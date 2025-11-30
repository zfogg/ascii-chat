# =============================================================================
# DEB Package Configuration (Debian/Ubuntu)
# =============================================================================
# Configures CPack to generate .deb packages for Debian and Ubuntu
#
# Prerequisites:
#   - dpkg-buildpackage must be available in PATH
#   - CPack variables must be set (CPACK_PACKAGE_NAME, etc.)
#   - Must be included after basic CPack configuration
#
# Outputs:
#   - Adds "DEB" to CPACK_GENERATOR if dpkg-buildpackage is found
#   - Configures DEB-specific CPack variables
# =============================================================================

if(NOT UNIX OR APPLE)
    return()
endif()

# Check for dpkg-deb (used by CPack to build .deb packages)
find_program(DPKG_DEB_EXECUTABLE dpkg-deb)
if(NOT DPKG_DEB_EXECUTABLE)
    message(STATUS "${Red}CPack:${ColorReset} DEB generator disabled (${BoldBlue}dpkg-deb${ColorReset} not found)")
    return()
endif()

# Add DEB to generator list
list(APPEND CPACK_GENERATOR "DEB")
# Force update the cache so it persists
set(CPACK_GENERATOR "${CPACK_GENERATOR}" CACHE STRING "CPack generators" FORCE)
message(STATUS "${Yellow}CPack:${ColorReset} DEB generator enabled (${BoldBlue}dpkg-deb${ColorReset} found)")

# =============================================================================
# DEB Package Configuration
# =============================================================================

# Note: CPACK_DEBIAN_PACKAGE_SECTION, CPACK_DEBIAN_PACKAGE_PRIORITY, and
# CPACK_DEBIAN_PACKAGE_HOMEPAGE are set in Install.cmake BEFORE include(CPack)

set(CPACK_DEBIAN_PACKAGE_NAME "ascii-chat")
set(CPACK_DEBIAN_PACKAGE_DESCRIPTION "${CPACK_PACKAGE_DESCRIPTION_SUMMARY}")
set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "amd64")
if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
    set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "arm64")
endif()

# Note: CPACK_DEBIAN_PACKAGE_DEPENDS is set in Install.cmake before include(CPack)

# Use our custom package naming (matches CPACK_PACKAGE_FILE_NAME)
set(CPACK_DEBIAN_FILE_NAME "${CPACK_PACKAGE_FILE_NAME}.deb")

# Maintainer info is set in ProjectConstants.cmake
# Can be overridden via environment variable DEBEMAIL
if(DEFINED ENV{DEBEMAIL})
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "$ENV{DEBEMAIL}")
endif()

# Generate Debian copyright file from template
# This is required by Debian policy (https://www.debian.org/doc/debian-policy/ch-docs.html#copyright)
# The copyright file must be installed to /usr/share/doc/<package>/copyright
configure_file(
    "${CMAKE_SOURCE_DIR}/cmake/install/copyright.in"
    "${CMAKE_BINARY_DIR}/generated/copyright"
    @ONLY
)

# Install copyright file to /usr/share/doc/ascii-chat/copyright
# This is required by Debian policy and shows license information in package managers
install(FILES "${CMAKE_BINARY_DIR}/generated/copyright"
    DESTINATION "share/doc/ascii-chat"
    COMPONENT applications
)

# Generate AppStream metadata file for Ubuntu App Center and other software centers
# This provides license information that GUI package managers can display
# Get current date for the release info
string(TIMESTAMP BUILD_DATE "%Y-%m-%d")
configure_file(
    "${CMAKE_SOURCE_DIR}/cmake/install/ascii-chat.metainfo.xml.in"
    "${CMAKE_BINARY_DIR}/generated/ascii-chat.metainfo.xml"
    @ONLY
)

# Install AppStream metadata to /usr/share/metainfo/
# This is used by GNOME Software, KDE Discover, Ubuntu App Center, etc.
install(FILES "${CMAKE_BINARY_DIR}/generated/ascii-chat.metainfo.xml"
    DESTINATION "share/metainfo"
    COMPONENT applications
)

message(STATUS "${Yellow}CPack:${ColorReset} DEB configuration complete")
