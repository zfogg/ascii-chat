# =============================================================================
# CPack Project Configuration File
# =============================================================================
# This file is processed at CPack runtime (not configure time) and allows
# generator-specific configuration. Variables set here override those from
# the CMakeLists.txt/Install.cmake at packaging time.
#
# Key difference from CMakeLists.txt:
#   - CMakeLists.txt: processed once at configure time
#   - CPackProjectConfig.cmake: processed for EACH generator at package time
#
# This allows different generators to have different install prefixes:
#   - STGZ/TGZ: "/" (relative paths for Homebrew compatibility)
#   - productbuild: "/usr/local" (standard macOS .pkg location)
#   - DEB/RPM: "/usr" (standard Linux FHS location)
# =============================================================================

# STGZ (Self-extracting TGZ) and TGZ (plain tar.gz) should use relative paths
# This creates archives with: bin/ascii-chat, lib/libasciichat.dylib, etc.
# Users can then install to any prefix: ./installer.sh --prefix=/custom/path
# Homebrew uses: --prefix=/opt/homebrew/Cellar/ascii-chat/VERSION
if(CPACK_GENERATOR MATCHES "STGZ|TGZ|TXZ|TBZ2|ZIP")
    set(CPACK_PACKAGING_INSTALL_PREFIX "/")
    message(STATUS "CPack: ${CPACK_GENERATOR} using install prefix: ${CPACK_PACKAGING_INSTALL_PREFIX}")
endif()

# productbuild (.pkg) should install to /usr/local for standard macOS behavior
# This is the expected location for command-line tools on macOS
if(CPACK_GENERATOR STREQUAL "productbuild")
    set(CPACK_PACKAGING_INSTALL_PREFIX "/usr/local")
    message(STATUS "CPack: ${CPACK_GENERATOR} using install prefix: ${CPACK_PACKAGING_INSTALL_PREFIX}")
endif()

# DEB and RPM should use /usr for Linux FHS compliance
if(CPACK_GENERATOR MATCHES "DEB|RPM")
    set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
    message(STATUS "CPack: ${CPACK_GENERATOR} using install prefix: ${CPACK_PACKAGING_INSTALL_PREFIX}")
endif()

# Windows installers don't use CPACK_PACKAGING_INSTALL_PREFIX
# They use CPACK_PACKAGE_INSTALL_DIRECTORY instead
