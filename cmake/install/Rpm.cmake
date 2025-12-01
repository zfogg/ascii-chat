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

# Use centralized ASCIICHAT_RPMBUILD_EXECUTABLE from FindPrograms.cmake
if(NOT ASCIICHAT_RPMBUILD_EXECUTABLE)
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

# Note: CPACK_RPM_PACKAGE_LICENSE, CPACK_RPM_PACKAGE_GROUP, and
# CPACK_RPM_PACKAGE_DESCRIPTION are set in Install.cmake BEFORE include(CPack)

set(CPACK_RPM_PACKAGE_NAME "ascii-chat")
set(CPACK_RPM_PACKAGE_ICON "${CMAKE_SOURCE_DIR}/images/installer_icon.png")
set(CPACK_RPM_PACKAGE_ARCHITECTURE "x86_64")
if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
    set(CPACK_RPM_PACKAGE_ARCHITECTURE "aarch64")
endif()

# Include license file in RPM package using %license directive
# This makes the license visible in package managers and ensures compliance
# The %license directive is preferred over %doc for license files
install(FILES "${CPACK_RESOURCE_FILE_LICENSE}"
    DESTINATION "share/doc/ascii-chat"
    COMPONENT applications
    RENAME LICENSE
)

# Note: CPACK_RPM_PACKAGE_REQUIRES is set in Install.cmake before include(CPack)

# Use our custom package naming (matches CPACK_PACKAGE_FILE_NAME)
set(CPACK_RPM_FILE_NAME "${CPACK_PACKAGE_FILE_NAME}.rpm")

# Vendor info (optional)
if(DEFINED ENV{RPM_VENDOR})
    set(CPACK_RPM_PACKAGE_VENDOR "$ENV{RPM_VENDOR}")
else()
    set(CPACK_RPM_PACKAGE_VENDOR "${CPACK_PACKAGE_VENDOR}")
endif()

message(STATUS "${Yellow}CPack:${ColorReset} RPM configuration complete")
