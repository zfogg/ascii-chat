# =============================================================================
# STGZ Package Configuration (Self-Extracting Shell Archive)
# =============================================================================
# Configures CPack to generate STGZ (.sh) self-extracting installers for Unix
#
# STGZ (Self-extracting TGZ) creates shell scripts that:
#   - Extract files to a user-specified directory (default: /usr/local)
#   - Work without package manager or root privileges
#   - Support --prefix=/custom/path for custom installation
#
# Prerequisites:
#   - Unix platform (Linux/macOS)
#   - CPack variables must be set (CPACK_PACKAGE_NAME, etc.)
#   - Must be included after basic CPack configuration
#
# Outputs:
#   - Adds "STGZ" to CPACK_GENERATOR on Unix platforms
#   - Configures STGZ-specific CPack variables
# =============================================================================

if(NOT UNIX)
    return()
endif()

# Add STGZ to generator list (always available on Unix)
list(APPEND CPACK_GENERATOR "STGZ")
message(STATUS "${Yellow}CPack:${ColorReset} STGZ generator enabled (always available)")

# =============================================================================
# STGZ Configuration
# =============================================================================

# Use custom STGZ header with FHS-compliant defaults (/usr/local, no subdirectory)
# CMake's default header uses current directory, not /usr/local
# CPack will substitute @CPACK_*@ variables and calculate header length automatically
if(EXISTS "${CMAKE_SOURCE_DIR}/cmake/install/CPackSTGZHeader.sh.in")
    set(CPACK_STGZ_HEADER_FILE "${CMAKE_SOURCE_DIR}/cmake/install/CPackSTGZHeader.sh.in" CACHE FILEPATH "Custom STGZ header with /usr/local default" FORCE)
    message(STATUS "${Yellow}CPack:${ColorReset} Using custom STGZ header: ${BoldBlue}CPackSTGZHeader.sh.in${ColorReset}")
endif()

# CPACK_SET_DESTDIR must be OFF for STGZ to use prefix-based installation
# This allows users to override with: ./installer.sh --prefix=/custom/path
set(CPACK_SET_DESTDIR OFF)

# This sets the default installation directory shown in the STGZ installer
# Users can override with: ./installer.sh --prefix=/custom/path
set(CPACK_INSTALL_PREFIX "/usr/local")
set(CPACK_PACKAGING_INSTALL_PREFIX "/usr/local")

message(STATUS "${Yellow}CPack:${ColorReset} STGZ configuration complete (default prefix: ${BoldBlue}/usr/local${ColorReset})")
