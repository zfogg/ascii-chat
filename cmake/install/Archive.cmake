# =============================================================================
# Archive Package Configuration (Cross-Platform)
# =============================================================================
# Configures CPack to generate archive packages (ZIP, TGZ, TBZ2, TXZ)
#
# Archive formats are always available and work on all platforms:
#   - TGZ (.tar.gz) - Unix/Linux standard
#   - ZIP (.zip) - Windows/cross-platform
#   - TBZ2 (.tar.bz2) - Better compression than TGZ
#   - TXZ (.tar.xz) - Best compression
#
# Prerequisites:
#   - CPack variables must be set (CPACK_PACKAGE_NAME, etc.)
#   - Must be included after basic CPack configuration
#
# Outputs:
#   - Adds archive generators to CPACK_GENERATOR
# =============================================================================

include(${CMAKE_SOURCE_DIR}/cmake/utils/CPackGenerator.cmake)

# =============================================================================
# Platform-Specific Archive Selection
# =============================================================================
# Uses the convenience function from CPackGenerator.cmake
enable_archive_generators()

# =============================================================================
# Component-Based Archive Packaging
# =============================================================================
# Create separate archives for each component:
#   - ascii-chat-VERSION-OS-ARCH.tar.gz (Runtime component)
#   - libasciichat-VERSION-OS-ARCH.tar.gz (Development + Documentation + Manpages)
set(CPACK_ARCHIVE_COMPONENT_INSTALL ON)

# Component-specific archive naming
# Note: Component groups use uppercase names in CPack variables
# Runtime package (ascii-chat) - RuntimeGroup component group
set(CPACK_ARCHIVE_RUNTIMEGROUP_FILE_NAME "ascii-chat-${PROJECT_VERSION}-${_PACKAGE_OS}-${_PACKAGE_ARCH}")

# Development package (libasciichat) - DevelopmentGroup component group
# Note: DevelopmentGroup includes Development, Documentation, and Manpages components
set(CPACK_ARCHIVE_DEVELOPMENTGROUP_FILE_NAME "libasciichat-${PROJECT_VERSION}-${_PACKAGE_OS}-${_PACKAGE_ARCH}")
set(CPACK_ARCHIVE_DOCUMENTATION_FILE_NAME "libasciichat-doc-${PROJECT_VERSION}-${_PACKAGE_OS}-${_PACKAGE_ARCH}")
set(CPACK_ARCHIVE_MANPAGES_FILE_NAME "libasciichat-${PROJECT_VERSION}-${_PACKAGE_OS}-${_PACKAGE_ARCH}")

# =============================================================================
# Source Package Configuration (Optional)
# =============================================================================
# Source packages contain the source code for distribution

set(CPACK_SOURCE_GENERATOR "TGZ")
if(UNIX AND NOT APPLE)
    # Use centralized ASCIICHAT_ZIP_EXECUTABLE from FindPrograms.cmake
    if(ASCIICHAT_ZIP_EXECUTABLE)
        list(APPEND CPACK_SOURCE_GENERATOR "ZIP")
    endif()
endif()

# Source package ignore patterns
set(CPACK_SOURCE_IGNORE_FILES
    "/build/"
    "/build_release/"
    "/\\.git/"
    "/deps/"
    "/\\.deps-cache/"
    "/\\.vscode/"
    "/\\.idea/"
    "/CMakeFiles/"
    "/CMakeCache\\.txt$"
    "/cmake_install\\.cmake$"
    "/\\.ninja_log$"
    "/\\.ninja_deps$"
    "/compile_commands\\.json$"
)

message(STATUS "${Yellow}CPack:${ColorReset} Archive configuration complete")
