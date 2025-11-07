# =============================================================================
# RPM Package Configuration (RedHat/Fedora/SUSE)
# =============================================================================
# Configures CPack to generate .rpm packages for RedHat-based distributions
#
# Prerequisites:
#   - rpmbuild must be available in PATH
#   - CPack variables must be set (CPACK_PACKAGE_NAME, etc.)
#   - Must be included after basic CPack configuration
#
# Outputs:
#   - Adds "RPM" to CPACK_GENERATOR if rpmbuild is found
#   - Configures RPM-specific CPack variables
# =============================================================================

if(NOT UNIX OR APPLE)
    return()
endif()

# Check for rpmbuild
find_program(RPMBUILD_EXECUTABLE rpmbuild)
if(NOT RPMBUILD_EXECUTABLE)
    message(STATUS "${Red}CPack:${ColorReset} RPM generator disabled (${BoldBlue}rpmbuild${ColorReset} not found)")
    return()
endif()

# Add RPM to generator list
list(APPEND CPACK_GENERATOR "RPM")
# Force update the cache so it persists
set(CPACK_GENERATOR "${CPACK_GENERATOR}" CACHE STRING "CPack generators" FORCE)
message(STATUS "${Yellow}CPack:${ColorReset} RPM generator enabled (${BoldBlue}rpmbuild${ColorReset} found)")

# =============================================================================
# RPM Package Configuration
# =============================================================================

set(CPACK_RPM_PACKAGE_NAME "ascii-chat")
set(CPACK_RPM_PACKAGE_DESCRIPTION "${CPACK_PACKAGE_DESCRIPTION_SUMMARY}")
set(CPACK_RPM_PACKAGE_GROUP "Applications/Networking")
set(CPACK_RPM_PACKAGE_LICENSE "MIT" CACHE STRING "RPM package license" FORCE)
set(CPACK_RPM_PACKAGE_ICON "${CMAKE_SOURCE_DIR}/images/installer_icon.png")
set(CPACK_RPM_PACKAGE_ARCHITECTURE "x86_64")
if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
    set(CPACK_RPM_PACKAGE_ARCHITECTURE "aarch64")
endif()

# Dependencies for shared library component (libasciichat.so)
# The static executable has no runtime dependencies
# These are only needed if the Development component is installed
set(CPACK_RPM_PACKAGE_REQUIRES "")
set(CPACK_RPM_DEVELOPMENT_PACKAGE_REQUIRES "portaudio, alsa-lib, zstd, libsodium, mimalloc")

# Use RPM default package naming (ascii-chat-version-release.arch.rpm)
set(CPACK_RPM_FILE_NAME RPM-DEFAULT)

# Vendor info (optional)
if(DEFINED ENV{RPM_VENDOR})
    set(CPACK_RPM_PACKAGE_VENDOR "$ENV{RPM_VENDOR}")
else()
    set(CPACK_RPM_PACKAGE_VENDOR "${CPACK_PACKAGE_VENDOR}")
endif()

message(STATUS "${Yellow}CPack:${ColorReset} RPM configuration complete")
