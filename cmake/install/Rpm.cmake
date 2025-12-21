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

include(${CMAKE_SOURCE_DIR}/cmake/utils/CPackGenerator.cmake)

enable_cpack_generator(
    NAME "RPM"
    PLATFORM UNIX_NOT_APPLE
    REQUIRED_TOOL ASCIICHAT_RPMBUILD_EXECUTABLE
    TOOL_DISPLAY_NAME "rpmbuild"
)

if(NOT RPM_GENERATOR_ENABLED)
    return()
endif()

# =============================================================================
# RPM Package Configuration
# =============================================================================

# Note: CPACK_RPM_PACKAGE_LICENSE, CPACK_RPM_PACKAGE_GROUP, and
# CPACK_RPM_PACKAGE_DESCRIPTION are set in Install.cmake BEFORE include(CPack)

# Enable component-based packaging - creates separate .rpm for each component group
set(CPACK_RPM_COMPONENT_INSTALL ON)

# Determine architecture
set(CPACK_RPM_PACKAGE_ARCHITECTURE "x86_64")
if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
    set(CPACK_RPM_PACKAGE_ARCHITECTURE "aarch64")
endif()

# =============================================================================
# Runtime Package (ascii-chat) - The executable
# =============================================================================
# Note: Component group is "RuntimeGroup" so variables use RUNTIMEGROUP (uppercase)
set(CPACK_RPM_RUNTIMEGROUP_PACKAGE_NAME "ascii-chat")
set(CPACK_RPM_RUNTIMEGROUP_FILE_NAME "ascii-chat-${PROJECT_VERSION}-${CPACK_RPM_PACKAGE_ARCHITECTURE}.rpm")
set(CPACK_RPM_RUNTIMEGROUP_PACKAGE_GROUP "Applications/Networking")
set(CPACK_RPM_RUNTIMEGROUP_PACKAGE_DESCRIPTION "Terminal-based video chat with ASCII art conversion.
ascii-chat converts webcam video into ASCII art in real-time, enabling video
chat right in your terminal. Supports multiple clients with video mixing and
audio streaming capabilities.")

# =============================================================================
# Development Package (libasciichat-devel) - Libraries and headers
# =============================================================================
# Note: Component group is "DevelopmentGroup" so variables use DEVELOPMENTGROUP (uppercase)
set(CPACK_RPM_DEVELOPMENTGROUP_PACKAGE_NAME "libasciichat-devel")
set(CPACK_RPM_DEVELOPMENTGROUP_FILE_NAME "libasciichat-devel-${PROJECT_VERSION}-${CPACK_RPM_PACKAGE_ARCHITECTURE}.rpm")
set(CPACK_RPM_DEVELOPMENTGROUP_PACKAGE_GROUP "Development/Libraries")
set(CPACK_RPM_DEVELOPMENTGROUP_PACKAGE_DESCRIPTION "Development files for libasciichat.
This package contains the static and shared libraries, C headers, pkg-config
files, and CMake configuration files needed to build applications using
libasciichat.")
set(CPACK_RPM_DEVELOPMENTGROUP_PACKAGE_REQUIRES "ascii-chat = ${PROJECT_VERSION}, portaudio, libzstd, libsodium")

# =============================================================================
# Documentation Package (libasciichat-doc) - HTML docs
# =============================================================================
# Note: Documentation is part of DevelopmentGroup, not a separate component group
set(CPACK_RPM_DOCUMENTATION_PACKAGE_NAME "libasciichat-doc")
set(CPACK_RPM_DOCUMENTATION_FILE_NAME "libasciichat-doc-${PROJECT_VERSION}-noarch.rpm")
set(CPACK_RPM_DOCUMENTATION_PACKAGE_ARCHITECTURE "noarch")
set(CPACK_RPM_DOCUMENTATION_PACKAGE_GROUP "Documentation")
set(CPACK_RPM_DOCUMENTATION_PACKAGE_DESCRIPTION "Developer documentation for libasciichat.
This package contains HTML API documentation and examples for developing
applications with libasciichat.")

# =============================================================================
# Manpages Package (included in -devel) - API manpages
# =============================================================================
# Note: Manpages is part of DevelopmentGroup, not a separate component group
set(CPACK_RPM_MANPAGES_PACKAGE_NAME "libasciichat-devel")
set(CPACK_RPM_MANPAGES_FILE_NAME "libasciichat-devel-${PROJECT_VERSION}-${CPACK_RPM_PACKAGE_ARCHITECTURE}.rpm")
set(CPACK_RPM_MANPAGES_PACKAGE_GROUP "Development/Libraries")

# Include license file in RPM package using %license directive
# This makes the license visible in package managers and ensures compliance
# The %license directive is preferred over %doc for license files
install(FILES "${CPACK_RESOURCE_FILE_LICENSE}"
    DESTINATION "share/doc/ascii-chat"
    COMPONENT Runtime
    RENAME LICENSE
)

# Note: CPACK_RPM_PACKAGE_REQUIRES is set in Install.cmake before include(CPack)

# Vendor info (optional)
if(DEFINED ENV{RPM_VENDOR})
    set(CPACK_RPM_PACKAGE_VENDOR "$ENV{RPM_VENDOR}")
else()
    set(CPACK_RPM_PACKAGE_VENDOR "${CPACK_PACKAGE_VENDOR}")
endif()

message(STATUS "${Yellow}CPack:${ColorReset} RPM configuration complete")
