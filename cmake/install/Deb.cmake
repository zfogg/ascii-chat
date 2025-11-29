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

set(CPACK_DEBIAN_PACKAGE_NAME "ascii-chat")
set(CPACK_DEBIAN_PACKAGE_DESCRIPTION "${CPACK_PACKAGE_DESCRIPTION_SUMMARY}")
set(CPACK_DEBIAN_PACKAGE_SECTION "net")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
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

message(STATUS "${Yellow}CPack:${ColorReset} DEB configuration complete")
