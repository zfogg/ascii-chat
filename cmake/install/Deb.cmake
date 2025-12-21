# =============================================================================
# DEB Package Configuration (Debian/Ubuntu)
# =============================================================================
# Configures CPack to generate .deb packages for Debian and Ubuntu
#
# Prerequisites:
#   - dpkg-deb must be available in PATH (CPack uses this internally)
#   - CPack variables must be set (CPACK_PACKAGE_NAME, etc.)
#   - Must be included after basic CPack configuration
#
# Outputs:
#   - Adds "DEB" to CPACK_GENERATOR if dpkg-deb is found
#   - Configures DEB-specific CPack variables
# =============================================================================

include(${CMAKE_SOURCE_DIR}/cmake/utils/CPackGenerator.cmake)

enable_cpack_generator(
    NAME "DEB"
    PLATFORM UNIX_NOT_APPLE
    REQUIRED_TOOL ASCIICHAT_DPKG_DEB_EXECUTABLE
    TOOL_DISPLAY_NAME "dpkg-deb"
)

if(NOT DEB_GENERATOR_ENABLED)
    return()
endif()

# =============================================================================
# DEB Package Configuration
# =============================================================================

# Note: CPACK_DEBIAN_PACKAGE_SECTION, CPACK_DEBIAN_PACKAGE_PRIORITY, and
# CPACK_DEBIAN_PACKAGE_HOMEPAGE are set in Install.cmake BEFORE include(CPack)

# Enable component-based packaging - creates separate .deb for each component group
set(CPACK_DEB_COMPONENT_INSTALL ON)

# Determine architecture
set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "amd64")
if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
    set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "arm64")
endif()

# =============================================================================
# Runtime Package (ascii-chat) - The executable
# =============================================================================
# Note: Component group is "RuntimeGroup" so variables use RUNTIMEGROUP (uppercase)
set(CPACK_DEBIAN_RUNTIMEGROUP_PACKAGE_NAME "ascii-chat")
set(CPACK_DEBIAN_RUNTIMEGROUP_FILE_NAME "ascii-chat-${PROJECT_VERSION}-${CPACK_DEBIAN_PACKAGE_ARCHITECTURE}.deb")
set(CPACK_DEBIAN_RUNTIMEGROUP_PACKAGE_SECTION "net")
set(CPACK_DEBIAN_RUNTIMEGROUP_PACKAGE_DESCRIPTION "Terminal-based video chat with ASCII art conversion
 ascii-chat converts webcam video into ASCII art in real-time, enabling video
 chat right in your terminal. Supports multiple clients with video mixing and
 audio streaming capabilities.")

# =============================================================================
# Development Package (libasciichat-dev) - Libraries and headers
# =============================================================================
# Note: Component group is "DevelopmentGroup" so variables use DEVELOPMENTGROUP (uppercase)
set(CPACK_DEBIAN_DEVELOPMENTGROUP_PACKAGE_NAME "libasciichat-dev")
set(CPACK_DEBIAN_DEVELOPMENTGROUP_FILE_NAME "libasciichat-dev-${PROJECT_VERSION}-${CPACK_DEBIAN_PACKAGE_ARCHITECTURE}.deb")
set(CPACK_DEBIAN_DEVELOPMENTGROUP_PACKAGE_SECTION "libdevel")
set(CPACK_DEBIAN_DEVELOPMENTGROUP_PACKAGE_DESCRIPTION "Development files for libasciichat
 This package contains the static and shared libraries, C headers, pkg-config
 files, and CMake configuration files needed to build applications using
 libasciichat.")
set(CPACK_DEBIAN_DEVELOPMENTGROUP_PACKAGE_DEPENDS "ascii-chat (= ${PROJECT_VERSION}), libportaudio2, libzstd1, libsodium23")

# =============================================================================
# Documentation Package (libasciichat-doc) - HTML docs
# =============================================================================
# Note: Documentation is part of DevelopmentGroup, not a separate component group
set(CPACK_DEBIAN_DOCUMENTATION_PACKAGE_NAME "libasciichat-doc")
set(CPACK_DEBIAN_DOCUMENTATION_FILE_NAME "libasciichat-doc-${PROJECT_VERSION}-all.deb")
set(CPACK_DEBIAN_DOCUMENTATION_PACKAGE_ARCHITECTURE "all")
set(CPACK_DEBIAN_DOCUMENTATION_PACKAGE_SECTION "doc")
set(CPACK_DEBIAN_DOCUMENTATION_PACKAGE_DESCRIPTION "Developer documentation for libasciichat
 This package contains HTML API documentation and examples for developing
 applications with libasciichat.")

# =============================================================================
# Manpages Package (included in -dev) - API manpages
# =============================================================================
# Note: Manpages is part of DevelopmentGroup, not a separate component group
set(CPACK_DEBIAN_MANPAGES_PACKAGE_NAME "libasciichat-dev")
set(CPACK_DEBIAN_MANPAGES_FILE_NAME "libasciichat-dev-${PROJECT_VERSION}-${CPACK_DEBIAN_PACKAGE_ARCHITECTURE}.deb")
set(CPACK_DEBIAN_MANPAGES_PACKAGE_SECTION "libdevel")

# Note: CPACK_DEBIAN_PACKAGE_DEPENDS is set in Install.cmake before include(CPack)

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
