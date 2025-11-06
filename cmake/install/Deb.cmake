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

# Check for dpkg-buildpackage
find_program(DPKG_BUILDPACKAGE_EXECUTABLE dpkg-buildpackage)
if(NOT DPKG_BUILDPACKAGE_EXECUTABLE)
    message(STATUS "${Red}CPack:${ColorReset} DEB generator disabled (${BoldBlue}dpkg-buildpackage${ColorReset} not found)")
    return()
endif()

# Add DEB to generator list
list(APPEND CPACK_GENERATOR "DEB")
# Force update the cache so it persists
set(CPACK_GENERATOR "${CPACK_GENERATOR}" CACHE STRING "CPack generators" FORCE)
message(STATUS "${Yellow}CPack:${ColorReset} DEB generator enabled (${BoldBlue}dpkg-buildpackage${ColorReset} found)")

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

# Dependencies for shared library component (libasciichat.so)
# The static executable has no runtime dependencies
# These are only needed if the Development component is installed
set(CPACK_DEBIAN_PACKAGE_DEPENDS "")
set(CPACK_DEBIAN_DEVELOPMENT_PACKAGE_DEPENDS "libportaudio2, libasound2, libzstd1, libsodium23, libmimalloc2.0 | libmimalloc-dev")

# Use Debian default package naming (ascii-chat_version_arch.deb)
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)

# Maintainer info is set in ProjectConstants.cmake
# Can be overridden via environment variable DEBEMAIL
if(DEFINED ENV{DEBEMAIL})
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "$ENV{DEBEMAIL}")
endif()

message(STATUS "${Yellow}CPack:${ColorReset} DEB configuration complete")
