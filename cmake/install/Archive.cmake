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
